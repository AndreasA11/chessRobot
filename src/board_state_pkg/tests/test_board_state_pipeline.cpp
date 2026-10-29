
#include "Arbiter.hpp"
#include "Decode.hpp"
#include "test_helpers.hpp"

#include <gtest/gtest.h>

#include <iostream>
#include <random>
#include <string>
#include <vector>



using namespace testutil;


// End-to-end tests of the board_state_pkg pipeline, no ROS and no images:
//
//   true game -> synthetic SoftObs frames -> MoveDecoder -> Arbiter -> BoardState
//
// A simulated human oracle answers any question the Arbiter asks (promotion piece,
// "A or B?", "check the board"). Every committed move is compared against the
// true game, so a wrong silent commit is detected immediately.



namespace {

using AK = Action::Kind;

// ------------------------------------------------------------------ frames

struct Frame {
    SoftObs obs;
    bool usable = true;
};

struct FrameOptions {
    int unusable = 0;       // hand in view before the move settles
    int lifted = 0;         // piece picked up, not yet placed
    int settled = 3;        // frames of the final position
    double weakRate = 0.0;  // per-square chance of a low-confidence misread
    double strongRate = 0.0;// per-square chance of a confident misread
};

// Corrupt a clean observation of `truth`. Weak errors put 55% on a wrong class
// (true class keeps 40%); strong errors put 97% on a wrong class.
SoftObs corrupt(const Occupancy& truth, std::mt19937& rng, double weakRate, double strongRate) {
    SoftObs obs = SoftObs::fromHard(truth, 0.99f);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::uniform_int_distribution<int> pick(0, 1);
    const Cell all[3] = {Cell::Empty, Cell::White, Cell::Black};

    for(int r = 0; r < 8; ++r) {
        for(int f = 0; f < 8; ++f) {
            const Square s{f, r};
            const double u = uni(rng);
            if(u >= strongRate + weakRate) {
                continue;
            }
            const bool strong = (u < strongRate);

            const Cell t = truth.cellAt(s);
            Cell wrong[2];
            int n = 0;
            for(Cell c : all) {
                if(c != t) {
                    wrong[n++] = c;
                }
            }
            const Cell w = wrong[pick(rng)];
            const Cell o = (w == wrong[0]) ? wrong[1] : wrong[0];

            float probs[3] = {0, 0, 0};
            probs[SoftObs::classIndex(w)] = strong ? 0.97f : 0.55f;
            probs[SoftObs::classIndex(t)] = strong ? 0.02f : 0.40f;
            probs[SoftObs::classIndex(o)] = strong ? 0.01f : 0.05f;
            obs.set(s, probs[0], probs[1], probs[2]);
        }
    }
    return obs;
}

std::vector<Frame> makeFrames(const BoardState& before, const Move& m, const BoardState& after,
                              const FrameOptions& o, std::mt19937& rng) {
    std::vector<Frame> frames;

    for(int i = 0; i < o.unusable; ++i) {
        Frame f;
        f.obs = SoftObs::fromHard(Occupancy(before));
        f.usable = false;
        frames.push_back(f);
    }
    if(o.lifted > 0) {
        Occupancy lift(before);
        lift.set(m.from, Cell::Empty);
        for(int i = 0; i < o.lifted; ++i) {
            Frame f;
            f.obs = SoftObs::fromHard(lift);
            frames.push_back(f);
        }
    }
    for(int i = 0; i < o.settled; ++i) {
        Frame f;
        f.obs = corrupt(Occupancy(after), rng, o.weakRate, o.strongRate);
        frames.push_back(f);
    }
    return frames;
}

// ------------------------------------------------------------------ pipeline harness

struct Stats {
    int moves = 0;
    int commits = 0;
    int wrongCommits = 0;  // committed something other than the true move (silent errors)
    int stuck = 0;         // move never resolved, even after extra frames
    int askedPromotion = 0;
    int askedWhichMove = 0;
    int askedCheckBoard = 0;
    int reportedIllegal = 0;
    int askedClearView = 0;

    // Questions that mean the system struggled (a promotion question is expected)
    int interventions() const { return askedWhichMove + askedCheckBoard + reportedIllegal; }
};

class Pipeline {
public:
    explicit Pipeline(const std::string& fen, ArbiterConfig cfg = {})
        : truth_(boardFromFen(fen)), committed_(boardFromFen(fen)), arb_(cfg) {
        dec_.reset(committed_);
    }

    // Play one true move: the camera produces `frames`; if they do not settle the
    // move, the last usable frame keeps repeating, as a real camera would.
    void play(const Move& trueMove, const std::vector<Frame>& frames) {
        BoardState truthAfter = truth_;
        truthAfter.applyMove(trueMove);
        ++stats.moves;

        bool resolved = false;
        const Frame* lastUsable = nullptr;
        for(const Frame& f : frames) {
            if(f.usable) {
                lastUsable = &f;
            }
            if(feed(f, trueMove, truthAfter)) {
                resolved = true;
                break;
            }
        }
        for(int i = 0; !resolved && lastUsable && i < 20; ++i) {
            resolved = feed(*lastUsable, trueMove, truthAfter);
        }
        if(!resolved) {
            ++stats.stuck;
            committed_ = truthAfter;  // resync so the rest of the script can run
        }

        truth_ = truthAfter;
        dec_.reset(committed_);
        arb_.reset();
    }

    const BoardState& truth() const { return truth_; }
    const BoardState& committed() const { return committed_; }

    Stats stats;

private:
    bool feed(const Frame& f, const Move& trueMove, const BoardState& truthAfter) {
        const Decoding d = dec_.observe(f.obs, f.usable);
        const Action a = arb_.step(committed_, d, f.obs.hardLabels());

        switch(a.kind) {
            case AK::Wait:
                return false;
            case AK::RequestClearView:
                ++stats.askedClearView;
                return false;
            case AK::Commit:
                return commit(a.move, truthAfter);
            case AK::AskPromotion: {
                ++stats.askedPromotion;
                Move m = a.move;
                m.promotion = trueMove.promotion;  // the human chooses the piece
                return commit(m, truthAfter);
            }
            case AK::AskWhichMove: {
                ++stats.askedWhichMove;
                // The human picks the right option (or enters the true move if neither is)
                return commit(trueMove, truthAfter);
            }
            case AK::AskCheckBoard:
                ++stats.askedCheckBoard;
                return commit(trueMove, truthAfter);
            case AK::ReportIllegal:
                ++stats.reportedIllegal;
                return commit(trueMove, truthAfter);
        }
        return false;
    }

    bool commit(const Move& m, const BoardState& truthAfter) {
        BoardState after = committed_;
        after.applyMove(m);
        ++stats.commits;
        if(FEN::toFEN(after) == FEN::toFEN(truthAfter)) {
            committed_ = after;
        } else {
            ++stats.wrongCommits;
            committed_ = truthAfter;  // resync so later moves are still meaningful
        }
        return true;
    }

    BoardState truth_;
    BoardState committed_;
    MoveDecoder dec_;
    Arbiter arb_;
};

// Replay a UCI move list through the pipeline, checking sync after every move.
void playGame(Pipeline& p, const std::vector<std::string>& ucis, const FrameOptions& o,
              std::mt19937& rng) {
    for(size_t i = 0; i < ucis.size(); ++i) {
        const Move m = UCI::parseUCI(ucis[i]);
        ASSERT_TRUE(Diagnose::diagnoseMove(p.truth(), m).verdict == Diagnose::Verdict::Legal)
            << "script move " << i << " (" << ucis[i] << ") is not legal in the script";

        BoardState after = p.truth();
        after.applyMove(m);
        p.play(m, makeFrames(p.truth(), m, after, o, rng));

        ASSERT_EQ(FEN::toFEN(p.committed()), FEN::toFEN(p.truth()))
            << "committed board diverged after move " << i << " (" << ucis[i] << ")";
    }
}

// Feed frames straight to decoder + arbiter and collect every action
// (for scenarios where the true game cannot be replayed, e.g. an illegal human move).
std::vector<Action> runRaw(const BoardState& b, const std::vector<Frame>& frames,
                           ArbiterConfig cfg = {}) {
    MoveDecoder dec;
    Arbiter arb(cfg);
    dec.reset(b);
    std::vector<Action> actions;
    for(const Frame& f : frames) {
        const Decoding d = dec.observe(f.obs, f.usable);
        actions.push_back(arb.step(b, d, f.obs.hardLabels()));
    }
    return actions;
}

// Morphy vs. Duke of Brunswick and Count Isouard, 1858 ("Opera Game").
// 33 plies with captures, a queenside castle, checks, and a mating finish.
const std::vector<std::string> kOperaGame = {
    "e2e4", "e7e5", "g1f3", "d7d6", "d2d4", "c8g4", "d4e5", "g4f3", "d1f3", "d6e5",
    "f1c4", "g8f6", "f3b3", "d8e7", "b1c3", "c7c6", "c1g5", "b7b5", "c3b5", "c6b5",
    "c4b5", "b8d7", "e1c1", "a8d8", "d1d7", "d8d7", "h1d1", "e7e6", "b5d7", "f6d7",
    "b3b8", "d7b8", "d1d8"};

}  // namespace

// ------------------------------------------------------------------ full games

TEST(Pipeline, OperaGameCleanFrames) {
    std::mt19937 rng(1);
    Pipeline p(kStartFen);
    FrameOptions o;
    o.settled = 3;
    playGame(p, kOperaGame, o, rng);

    EXPECT_EQ(p.stats.moves, 33);
    EXPECT_EQ(p.stats.commits, 33);
    EXPECT_EQ(p.stats.wrongCommits, 0);
    EXPECT_EQ(p.stats.stuck, 0);
    EXPECT_EQ(p.stats.interventions(), 0);
    EXPECT_TRUE(Diagnose::legalMoves(p.committed()).empty()) << "game ends in checkmate";
}

TEST(Pipeline, OperaGameWithHandsAndLiftedPieces) {
    std::mt19937 rng(2);
    Pipeline p(kStartFen);
    FrameOptions o;
    o.unusable = 3;
    o.lifted = 2;
    o.settled = 4;
    playGame(p, kOperaGame, o, rng);

    EXPECT_EQ(p.stats.wrongCommits, 0);
    EXPECT_EQ(p.stats.stuck, 0);
    EXPECT_EQ(p.stats.interventions(), 0);
}

TEST(Pipeline, OperaGameWithWeakNoise) {
    // 5% of squares per frame are misread at low confidence (about 3 per frame)
    std::mt19937 rng(2024);
    Pipeline p(kStartFen);
    FrameOptions o;
    o.unusable = 1;
    o.lifted = 1;
    o.settled = 4;
    o.weakRate = 0.05;
    playGame(p, kOperaGame, o, rng);

    std::cout << "[weak noise] commits=" << p.stats.commits
              << " wrong=" << p.stats.wrongCommits
              << " interventions=" << p.stats.interventions() << "\n";

    EXPECT_EQ(p.stats.wrongCommits, 0);  // the safety property: never a silent wrong commit
    EXPECT_EQ(p.stats.stuck, 0);
    EXPECT_EQ(FEN::toFEN(p.committed()), FEN::toFEN(p.truth()));
}

// ------------------------------------------------------------------ special moves

TEST(Pipeline, CastlingBothSides) {
    std::mt19937 rng(3);
    Pipeline p("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
    FrameOptions o;
    o.lifted = 1;
    playGame(p, {"e1g1", "e8c8"}, o, rng);

    EXPECT_EQ(p.stats.wrongCommits, 0);
    EXPECT_EQ(p.stats.interventions(), 0);
}

TEST(Pipeline, EnPassant) {
    std::mt19937 rng(4);
    Pipeline p("rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3");
    FrameOptions o;
    o.lifted = 1;
    playGame(p, {"d4e3"}, o, rng);

    EXPECT_EQ(p.stats.wrongCommits, 0);
    EXPECT_EQ(p.stats.interventions(), 0);
}

TEST(Pipeline, PromotionAsksForPieceThenCommits) {
    std::mt19937 rng(5);
    Pipeline p("8/P6k/8/8/8/8/8/K7 w - - 0 1");
    FrameOptions o;
    playGame(p, {"a7a8q"}, o, rng);

    EXPECT_EQ(p.stats.askedPromotion, 1);
    EXPECT_EQ(p.stats.wrongCommits, 0);
    EXPECT_EQ(p.stats.interventions(), 0);
}

// ------------------------------------------------------------------ uncertainty and human help

TEST(Pipeline, EvenSplitFramesAskTheHumanWhichMove) {
    // The camera cannot tell e3 from e4: 50/50 on both squares, on every frame
    const BoardState start = boardFromFen(kStartFen);

    SoftObs split = SoftObs::fromHard(Occupancy(start));
    split.set(sq("e2"), 0.98f, 0.01f, 0.01f);
    split.set(sq("e3"), 0.5f, 0.5f, 0.0f);
    split.set(sq("e4"), 0.5f, 0.5f, 0.0f);

    std::vector<Frame> frames;
    for(int i = 0; i < 8; ++i) {
        Frame f;
        f.obs = split;
        frames.push_back(f);
    }

    Pipeline p(kStartFen);
    p.play(UCI::parseUCI("e2e4"), frames);

    EXPECT_EQ(p.stats.askedWhichMove, 1);
    EXPECT_EQ(p.stats.wrongCommits, 0);  // asked instead of guessing
    EXPECT_EQ(p.stats.stuck, 0);
}

TEST(Pipeline, IllegalHumanMoveIsReportedNotCommitted) {
    // The human plays Ng1-g3, which is not a knight move. Frames show it on g3.
    const BoardState start = boardFromFen(kStartFen);
    const SoftObs bad = obsFromFen("rnbqkbnr/pppppppp/8/8/8/6N1/PPPPPPPP/RNBQKB1R b KQkq - 1 1");

    std::vector<Frame> frames;
    for(int i = 0; i < 12; ++i) {
        Frame f;
        f.obs = bad;
        frames.push_back(f);
    }

    const ArbiterConfig cfg;
    const std::vector<Action> actions = runRaw(start, frames, cfg);

    int firstAction = -1;
    for(size_t i = 0; i < actions.size(); ++i) {
        if(actions[i].kind != AK::Wait) {
            firstAction = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(firstAction, 0) << "never reported the illegal move";
    EXPECT_GE(firstAction + 1, cfg.noLegalFrameLimit) << "blamed the player too early";

    const Action& a = actions[firstAction];
    EXPECT_EQ(a.kind, AK::ReportIllegal);
    EXPECT_TRUE(sameMove(a.move, mv("g1", "g3")));
    EXPECT_TRUE(a.verdict == Diagnose::Verdict::IllegalPieceGeometry);

    // Nothing else was issued after the single report
    for(size_t i = firstAction + 1; i < actions.size(); ++i) {
        EXPECT_EQ(actions[i].kind, AK::Wait);
    }
}

TEST(Pipeline, PieceLiftedAndPutBackProducesNoAction) {
    const BoardState start = boardFromFen(kStartFen);

    Occupancy lift(start);
    lift.set(sq("e2"), Cell::Empty);

    std::vector<Frame> frames;
    // Evidence of "something changed" decays one lifted frame at a time, so the
    // total number of NoLegalExplanation frames (lifted + decay) must stay under the
    // Arbiter's noLegalFrameLimit (6). 2 lifted frames -> about 4 such frames.
    for(int i = 0; i < 2; ++i) {
        Frame f;
        f.obs = SoftObs::fromHard(lift);
        frames.push_back(f);
    }
    for(int i = 0; i < 4; ++i) {
        Frame f;
        f.obs = SoftObs::fromHard(Occupancy(start));
        frames.push_back(f);
    }

    for(const Action& a : runRaw(start, frames)) {
        EXPECT_EQ(a.kind, AK::Wait);
    }
}

TEST(Pipeline, PieceLeftLiftedEventuallyAsksToCheckTheBoard) {
    const BoardState start = boardFromFen(kStartFen);

    Occupancy lift(start);
    lift.set(sq("e2"), Cell::Empty);

    std::vector<Frame> frames;
    for(int i = 0; i < 12; ++i) {
        Frame f;
        f.obs = SoftObs::fromHard(lift);
        frames.push_back(f);
    }

    bool asked = false;
    for(const Action& a : runRaw(start, frames)) {
        if(a.kind == AK::AskCheckBoard) {
            asked = true;
        }
        EXPECT_NE(a.kind, AK::Commit);
    }
    EXPECT_TRUE(asked);
}