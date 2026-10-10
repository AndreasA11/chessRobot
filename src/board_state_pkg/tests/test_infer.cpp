#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "BoardState.hpp"
#include "Occupancy.hpp"
#include "Diagnose.hpp"
#include "Infer.hpp"

using Infer::Inference;
using Kind = Infer::Inference::Kind;

namespace {

const std::string kStart = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

Square sq(const std::string& s) { return Square{s[0] - 'a', s[1] - '1'}; }  // {file, rank}

// Apply `uci` to `fen`, then ask Infer what happened using only the occupancy.
Inference infer(const std::string& fen, const std::string& uci) {
    const BoardState before = FEN::fromFEN(fen);
    BoardState after = before;
    after.applyMove(UCI::parseUCI(uci));
    return Infer::inferMove(before, Occupancy(after));
}

void expectCandidate(const Inference& r, const std::string& from, const std::string& to) {
    ASSERT_EQ(r.kind, Kind::Candidate);
    EXPECT_EQ(r.move.from, sq(from));
    EXPECT_EQ(r.move.to, sq(to));
}

bool contains(const std::vector<Square>& v, Square s) { return std::find(v.begin(), v.end(), s) != v.end(); }

}  // namespace

// ------------------------------------------------------------ simple results

TEST(Infer, NoChange) {
    const BoardState b = FEN::fromFEN(kStart);
    Inference r = Infer::inferMove(b, Occupancy(b));
    EXPECT_EQ(r.kind, Kind::NoChange);
    EXPECT_TRUE(r.changed.empty());
}

TEST(Infer, QuietMoves) {
    expectCandidate(infer(kStart, "e2e4"), "e2", "e4");
    expectCandidate(infer(kStart, "g1f3"), "g1", "f3");
    expectCandidate(infer("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "e7e5"), "e7", "e5");
}

TEST(Infer, ChangedListsBothSquares) {
    Inference r = infer(kStart, "e2e4");
    EXPECT_EQ(r.changed.size(), 2u);
    EXPECT_TRUE(contains(r.changed, sq("e2")));
    EXPECT_TRUE(contains(r.changed, sq("e4")));
}

TEST(Infer, Captures) {
    expectCandidate(infer("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5"), "e4", "d5");  // white takes
    expectCandidate(infer("4k3/8/8/3p4/4P3/8/8/4K3 b - - 0 1", "d5e4"), "d5", "e4");  // black takes
    expectCandidate(infer("4k3/8/8/3p4/8/8/8/3RK3 w - - 0 1", "d1d5"), "d1", "d5");   // rook takes
}

// ------------------------------------------------------------------ castling

TEST(Infer, CastlingAllFourWays) {
    const std::string w = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
    const std::string b = "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1";
    expectCandidate(infer(w, "e1g1"), "e1", "g1");
    expectCandidate(infer(w, "e1c1"), "e1", "c1");
    expectCandidate(infer(b, "e8g8"), "e8", "g8");
    expectCandidate(infer(b, "e8c8"), "e8", "c8");
}

TEST(Infer, CastlingReportsAllFourChangedSquares) {
    Inference r = infer("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1");
    EXPECT_EQ(r.changed.size(), 4u);
}

TEST(Infer, TwoSquareShuffleWithoutAKingIsNotCastling) {
    // rook h1->f1 and rook a1->c1 at once: two vacated, two filled, no king
    const BoardState b = FEN::fromFEN("4k3/8/8/8/8/8/8/R3K2R w - - 0 1");
    Occupancy o(b);
    o.set(sq("a1"), Cell::Empty);
    o.set(sq("h1"), Cell::Empty);
    o.set(sq("c1"), Cell::White);
    o.set(sq("f1"), Cell::White);
    EXPECT_EQ(Infer::inferMove(b, o).kind, Kind::Anomaly);
}

// ---------------------------------------------------------------- en passant

TEST(Infer, EnPassantBothColors) {
    expectCandidate(infer("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6"), "e5", "d6");
    expectCandidate(infer("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1", "d4e3"), "d4", "e3");
}

TEST(Infer, EnPassantFootprintWithWrongVictimIsAnomaly) {
    // my pawn leaves e5, d6 is filled, but the vanished enemy piece is not the pawn beside it
    const BoardState b = FEN::fromFEN("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    Occupancy o(b);
    o.set(sq("e5"), Cell::Empty);
    o.set(sq("d6"), Cell::White);
    o.set(sq("e8"), Cell::Empty);  // the black king vanished instead of the d5 pawn
    EXPECT_EQ(Infer::inferMove(b, o).kind, Kind::Anomaly);
}

// ----------------------------------------------------------------- promotion

TEST(Infer, PromotionLeavesPieceUnspecified) {
    Inference r = infer("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b7b8q");
    expectCandidate(r, "b7", "b8");
    EXPECT_EQ(r.move.promotion, Piece::Empty);
}

TEST(Infer, PromotionByCapture) {
    expectCandidate(infer("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7b8q"), "a7", "b8");
}

TEST(Infer, PromotionFlowsIntoDiagnoseAsAmbiguous) {
    const BoardState b = FEN::fromFEN("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1");
    BoardState after = b;
    after.applyMove(UCI::parseUCI("b7b8q"));
    Inference r = Infer::inferMove(b, Occupancy(after));
    ASSERT_EQ(r.kind, Kind::Candidate);
    EXPECT_EQ(Diagnose::diagnoseMove(b, r.move).verdict, Diagnose::Verdict::AmbiguousPromotion);
}

// ------------------------------------------------------- in-progress / errors

TEST(Infer, PieceLiftedButNotPlaced) {
    const BoardState b = FEN::fromFEN(kStart);
    Occupancy o(b);
    o.set(sq("e2"), Cell::Empty);
    Inference r = Infer::inferMove(b, o);
    EXPECT_EQ(r.kind, Kind::PieceLifted);
    ASSERT_EQ(r.changed.size(), 1u);
    EXPECT_EQ(r.changed[0], sq("e2"));
}

TEST(Infer, KnockedNeighborIsAnomaly) {
    const BoardState b = FEN::fromFEN(kStart);
    BoardState after = b;
    after.applyMove(UCI::parseUCI("e2e4"));
    Occupancy o(after);
    o.set(sq("d2"), Cell::Empty);  // d2 pawn knocked off the board
    Inference r = Infer::inferMove(b, o);
    EXPECT_EQ(r.kind, Kind::Anomaly);
    EXPECT_TRUE(contains(r.changed, sq("d2")));
}

TEST(Infer, TwoMovesAtOnceIsAnomaly) {
    const BoardState b = FEN::fromFEN(kStart);
    BoardState after = b;
    after.applyMove(UCI::parseUCI("e2e4"));
    Occupancy o(after);
    o.set(sq("g1"), Cell::Empty);
    o.set(sq("f3"), Cell::White);
    EXPECT_EQ(Infer::inferMove(b, o).kind, Kind::Anomaly);
}

TEST(Infer, ExtraOpponentPieceAppearingIsAnomaly) {
    const BoardState b = FEN::fromFEN(kStart);
    Occupancy o(b);
    o.set(sq("e4"), Cell::Black);
    EXPECT_EQ(Infer::inferMove(b, o).kind, Kind::Anomaly);
}

TEST(Infer, MovingTheOpponentsPieceIsAnomaly) {
    // White to move, but a black pawn was pushed
    const BoardState b = FEN::fromFEN(kStart);
    BoardState other = FEN::fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1");
    other.applyMove(UCI::parseUCI("e7e5"));
    EXPECT_EQ(Infer::inferMove(b, Occupancy(other)).kind, Kind::Anomaly);
}

TEST(Infer, SameCallTwiceGivesSameAnswer) {
    // inferMove must be a pure function (no leftover state between calls)
    const BoardState b = FEN::fromFEN(kStart);
    BoardState after = b;
    after.applyMove(UCI::parseUCI("e2e4"));
    const Occupancy o(after);
    Inference a = Infer::inferMove(b, o);
    Inference c = Infer::inferMove(b, o);
    EXPECT_EQ(a.kind, c.kind);
    EXPECT_EQ(a.move.from, c.move.from);
    EXPECT_EQ(a.move.to, c.move.to);
}

// ------------------------------------------------- pipeline: infer + diagnose

TEST(InferPipeline, IllegalMoveStillBecomesACandidateThenGetsDiagnosed) {
    // Bishop dragged straight up the board. applyMove assumes legality, so it simply relocates the piece.
    const std::string fen = "4k3/8/8/8/8/8/8/2B1K3 w - - 0 1";
    const BoardState b = FEN::fromFEN(fen);
    BoardState after = b;
    after.applyMove(UCI::parseUCI("c1c3"));
    Inference r = Infer::inferMove(b, Occupancy(after));
    expectCandidate(r, "c1", "c3");
    EXPECT_EQ(Diagnose::diagnoseMove(b, r.move).verdict, Diagnose::Verdict::IllegalPieceGeometry);
}

TEST(InferPipeline, PinnedPieceMoveIsCaughtAfterInference) {
    const std::string fen = "4k3/4r3/8/8/8/8/4N3/4K3 w - - 0 1";
    const BoardState b = FEN::fromFEN(fen);
    BoardState after = b;
    after.applyMove(UCI::parseUCI("e2c3"));
    Inference r = Infer::inferMove(b, Occupancy(after));
    expectCandidate(r, "e2", "c3");
    EXPECT_EQ(Diagnose::diagnoseMove(b, r.move).verdict, Diagnose::Verdict::LeavesKingInCheck);
}

// Every legal move, in several positions, must be recovered exactly from the resulting occupancy.
TEST(InferPipeline, EveryLegalMoveRoundTrips) {
    struct Case {
        std::string fen;
        int expected;   // legal moves, counting each from-to pair once
    };
    const std::vector<Case> cases = {
        {kStart, 20},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 48},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 14},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 6},
        {"r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 6},
        // 44 legal moves, but d7xc8 promotes four ways and this test only tries 'q',
        // so the four collapse into one from-to pair: 44 - 3 = 41.
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 41},
        {"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", 7},
        {"4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1", 7},
    };
 
    for (const auto& c : cases) {
        const BoardState b = FEN::fromFEN(c.fen);
        int checked = 0;
        for (int fr = 0; fr < 8; ++fr)
            for (int ff = 0; ff < 8; ++ff)
                for (int tr = 0; tr < 8; ++tr)
                    for (int tf = 0; tf < 8; ++tf) {
                        const Square from{ff, fr}, to{tf, tr};
                        std::string u = BoardState::squareToString(from) + BoardState::squareToString(to);
                        const Piece p = b.at(from);
                        if (p != Piece::Empty && BoardState::typeOf(p) == 'P' && (tr == 0 || tr == 7)) u += 'q';
                        const Move m = UCI::parseUCI(u);
                        if (Diagnose::diagnoseMove(b, m).verdict != Diagnose::Verdict::Legal) continue;
 
                        BoardState after = b;
                        after.applyMove(m);
                        Inference r = Infer::inferMove(b, Occupancy(after));
                        ASSERT_EQ(r.kind, Kind::Candidate) << c.fen << " " << u;
                        EXPECT_EQ(r.move.from, from) << c.fen << " " << u;
                        EXPECT_EQ(r.move.to, to) << c.fen << " " << u;
                        EXPECT_EQ(r.move.promotion, Piece::Empty) << c.fen << " " << u;
                        ++checked;
                    }
        EXPECT_EQ(checked, c.expected) << "wrong number of legal moves for " << c.fen;
    }
}