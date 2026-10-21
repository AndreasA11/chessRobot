#include <gtest/gtest.h>

#include <cmath>

#include "Decode.hpp"
#include "test_helpers.hpp"

using namespace testutil;
using Kind = Decoding::Kind;

namespace {

const std::string kAfterE4Black =
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";

// Reset a decoder on `before`, feed one clean frame of `after`, return the verdict.
Decoding decodeClean(const std::string& before, const std::string& after) {
    MoveDecoder dec;
    dec.reset(boardFromFen(before));
    return dec.observe(obsFromFen(after));
}

}  // namespace

// ---------------------------------------------------------------- enumeration

TEST(MoveDecoderEnumeration, StartPositionHasTwentyMoves) {
    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    EXPECT_EQ(dec.legalMoveCount(), 20);
}

TEST(MoveDecoderEnumeration, BlackToMoveHasTwentyMoves) {
    MoveDecoder dec;
    dec.reset(boardFromFen(kAfterE4Black));
    EXPECT_EQ(dec.legalMoveCount(), 20);
}

TEST(MoveDecoderEnumeration, ExcludesCastlingWithoutRights) {
    // Rook a1 has 10 moves, king e1 has 5, castling is not allowed: 15 total
    MoveDecoder dec;
    dec.reset(boardFromFen("4k3/8/8/8/8/8/8/R3K3 w - - 0 1"));
    EXPECT_EQ(dec.legalMoveCount(), 15);
}

TEST(MoveDecoderEnumeration, ExcludesMovesOfPinnedPiece) {
    // Bishop e2 is pinned to the king by the rook on e7: only the 4 king moves remain
    MoveDecoder dec;
    dec.reset(boardFromFen("4k3/4r3/8/8/8/8/4B3/4K3 w - - 0 1"));
    EXPECT_EQ(dec.legalMoveCount(), 4);
}

TEST(MoveDecoderEnumeration, CheckmatedSideHasNoMoves) {
    MoveDecoder dec;
    dec.reset(boardFromFen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3"));
    EXPECT_EQ(dec.legalMoveCount(), 0);
}

TEST(MoveDecoderEnumeration, NoLegalMovesReportsNoLegalExplanation) {
    MoveDecoder dec;
    dec.reset(boardFromFen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3"));
    const Decoding d = dec.observe(obsFromFen(kStartFen));
    EXPECT_EQ(d.kind, Kind::NoLegalExplanation);
}

TEST(MoveDecoderEnumeration, CallingObserveBeforeResetIsSafe) {
    MoveDecoder dec;
    const Decoding d = dec.observe(obsFromFen(kStartFen));
    EXPECT_EQ(d.kind, Kind::NoLegalExplanation);
}

// ---------------------------------------------------------------- clean moves

TEST(MoveDecoderClean, PawnDoublePush) {
    const Decoding d = decodeClean(
        kStartFen, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    EXPECT_EQ(d.kind, Kind::Confident);
    EXPECT_TRUE(sameMove(d.best, mv("e2", "e4")));
    EXPECT_FALSE(d.promotionUnspecified);
    EXPECT_GT(d.margin, 3.0);
}

TEST(MoveDecoderClean, KnightMove) {
    const Decoding d = decodeClean(
        kStartFen, "rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq - 1 1");
    EXPECT_EQ(d.kind, Kind::Confident);
    EXPECT_TRUE(sameMove(d.best, mv("g1", "f3")));
}

TEST(MoveDecoderClean, BlackMove) {
    const Decoding d = decodeClean(
        kAfterE4Black, "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2");
    EXPECT_EQ(d.kind, Kind::Confident);
    EXPECT_TRUE(sameMove(d.best, mv("e7", "e5")));
}

TEST(MoveDecoderClean, CaptureIsDetectedByColorFlip) {
    // e4xd5: the destination stays occupied but flips black -> white
    const Decoding d = decodeClean(
        "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2",
        "rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2");
    EXPECT_EQ(d.kind, Kind::Confident);
    EXPECT_TRUE(sameMove(d.best, mv("e4", "d5")));
}

TEST(MoveDecoderClean, KingSideCastling) {
    const Decoding d = decodeClean(
        "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1",
        "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R4RK1 b kq - 1 1");
    EXPECT_EQ(d.kind, Kind::Confident);
    EXPECT_TRUE(sameMove(d.best, mv("e1", "g1")));
}

TEST(MoveDecoderClean, QueenSideCastling) {
    const Decoding d = decodeClean(
        "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1",
        "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/2KR3R b kq - 1 1");
    EXPECT_EQ(d.kind, Kind::Confident);
    EXPECT_TRUE(sameMove(d.best, mv("e1", "c1")));
}

TEST(MoveDecoderClean, EnPassant) {
    // Black pawn d4 captures e3 en passant; the white pawn on e4 disappears
    const Decoding d = decodeClean(
        "rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3",
        "rnbqkbnr/ppp1pppp/8/8/8/4p3/PPPP1PPP/RNBQKBNR w KQkq - 0 4");
    EXPECT_EQ(d.kind, Kind::Confident);
    EXPECT_TRUE(sameMove(d.best, mv("d4", "e3")));
}

TEST(MoveDecoderClean, EnPassantScoresVictimSquare) {
    // Same move, but the captured pawn still appears on e4: evidence must be weaker
    const std::string before = "rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3";

    MoveDecoder good;
    good.reset(boardFromFen(before));
    const Decoding g = good.observe(obsFromFen(
        "rnbqkbnr/ppp1pppp/8/8/8/4p3/PPPP1PPP/RNBQKBNR w KQkq - 0 4"));

    MoveDecoder bad;
    bad.reset(boardFromFen(before));
    const Decoding b = bad.observe(obsFromFen(
        "rnbqkbnr/ppp1pppp/8/8/4P3/4p3/PPPP1PPP/RNBQKBNR w KQkq - 0 4"));

    EXPECT_GT(g.bestScore, b.bestScore + 4.0);
}

TEST(MoveDecoderClean, PromotionIsFlaggedUnspecified) {
    const Decoding d = decodeClean(
        "8/P6k/8/8/8/8/8/K7 w - - 0 1",
        "Q7/7k/8/8/8/8/8/K7 b - - 0 1");
    EXPECT_EQ(d.kind, Kind::Confident);
    EXPECT_TRUE(sameMove(d.best, mv("a7", "a8")));
    EXPECT_TRUE(d.promotionUnspecified);
    EXPECT_EQ(d.best.promotion, Piece::Empty);
}

// ---------------------------------------------------------------- no change / unusable

TEST(MoveDecoderStates, UnchangedBoardIsNoChange) {
    const Decoding d = decodeClean(kStartFen, kStartFen);
    EXPECT_EQ(d.kind, Kind::NoChange);
}

TEST(MoveDecoderStates, LiftedPieceIsNeverConfident) {
    // e2 pawn picked up but not yet placed: the frame must not commit anything
    SoftObs obs = obsFromFen(kStartFen);
    obs.set(sq("e2"), 0.99f, 0.005f, 0.005f);

    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    const Decoding d = dec.observe(obs);
    EXPECT_NE(d.kind, Kind::Confident);
}

TEST(MoveDecoderStates, UnusableFrameIsNotScored) {
    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));

    const SoftObs after = obsFromFen(kAfterE4Black);
    const Decoding u = dec.observe(after, false);
    EXPECT_EQ(u.kind, Kind::Unusable);
    EXPECT_EQ(u.framesUsed, 0);

    // The unusable frame left no evidence behind
    const Decoding d = dec.observe(after, true);
    EXPECT_EQ(d.kind, Kind::Confident);
    EXPECT_EQ(d.framesUsed, 1);
}

// ---------------------------------------------------------------- noise robustness

TEST(MoveDecoderNoise, MisreadOnIrrelevantSquareIsIgnored) {
    // a6 is empty but reads 52% black. No white move touches a6, so it cancels out.
    SoftObs obs = obsFromFen(kAfterE4Black);
    obs.set(sq("a6"), 0.46f, 0.02f, 0.52f);

    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    const Decoding d = dec.observe(obs);
    EXPECT_EQ(d.kind, Kind::Confident);
    EXPECT_TRUE(sameMove(d.best, mv("e2", "e4")));
}

TEST(MoveDecoderNoise, ConfidentlyWrongIrrelevantSquareIsIgnored) {
    SoftObs obs = obsFromFen(kAfterE4Black);
    obs.set(sq("a6"), 0.0f, 0.0f, 1.0f);

    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    const Decoding d = dec.observe(obs);
    EXPECT_EQ(d.kind, Kind::Confident);
    EXPECT_TRUE(sameMove(d.best, mv("e2", "e4")));
}

TEST(MoveDecoderNoise, ZeroProbabilitiesStayFinite) {
    // Probability floor must keep scores finite even for exact zeros on relevant squares
    SoftObs obs = obsFromFen(kAfterE4Black);
    obs.set(sq("e4"), 0.0f, 0.0f, 1.0f);  // true class (white) has probability 0

    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    const Decoding d = dec.observe(obs);
    EXPECT_TRUE(std::isfinite(d.bestScore));
}

TEST(MoveDecoderNoise, WeakEvidenceBelowMinScoreIsNotConfident) {
    // A barely-there signal on both squares must not trigger a commit
    SoftObs obs = obsFromFen(kStartFen);
    obs.set(sq("e2"), 0.55f, 0.45f, 0.0f);
    obs.set(sq("e4"), 0.45f, 0.55f, 0.0f);

    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    const Decoding d = dec.observe(obs);
    EXPECT_NE(d.kind, Kind::Confident);
}

// ---------------------------------------------------------------- ambiguity and multi-frame

namespace {

// e2 clearly empty; e3/e4 split by (pE3, pE4) probability of "white"
SoftObs e3e4Obs(float whiteOnE3, float whiteOnE4) {
    SoftObs obs = obsFromFen(kStartFen);
    obs.set(sq("e2"), 0.98f, 0.01f, 0.01f);
    obs.set(sq("e3"), 1.0f - whiteOnE3, whiteOnE3, 0.0f);
    obs.set(sq("e4"), 1.0f - whiteOnE4, whiteOnE4, 0.0f);
    return obs;
}

}  // namespace

TEST(MoveDecoderAmbiguity, EvenSplitBetweenE3AndE4IsAmbiguous) {
    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    const Decoding d = dec.observe(e3e4Obs(0.5f, 0.5f));

    EXPECT_EQ(d.kind, Kind::Ambiguous);
    const bool pairIsE3E4 =
        (sameMove(d.best, mv("e2", "e3")) && sameMove(d.runnerUp, mv("e2", "e4"))) ||
        (sameMove(d.best, mv("e2", "e4")) && sameMove(d.runnerUp, mv("e2", "e3")));
    EXPECT_TRUE(pairIsE3E4);
    EXPECT_NEAR(d.margin, 0.0, 1e-6);
}

TEST(MoveDecoderAmbiguity, MildSingleFrameLeanIsStillAmbiguous) {
    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    const Decoding d = dec.observe(e3e4Obs(0.4f, 0.6f));  // favors e4 by ~0.8 nats
    EXPECT_EQ(d.kind, Kind::Ambiguous);
    EXPECT_TRUE(sameMove(d.best, mv("e2", "e4")));
}

TEST(MoveDecoderMultiFrame, RepeatedWeakEvidenceEventuallyResolves) {
    // Each frame favors e4 by ln(0.6/0.4)*2 ~= 0.81 nats; margin of 3.0 needs 4 frames
    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    const SoftObs frame = e3e4Obs(0.4f, 0.6f);

    for(int i = 1; i <= 3; ++i) {
        const Decoding d = dec.observe(frame);
        EXPECT_EQ(d.kind, Kind::Ambiguous) << "frame " << i;
        EXPECT_EQ(d.framesUsed, i);
    }
    const Decoding d = dec.observe(frame);
    EXPECT_EQ(d.kind, Kind::Confident);
    EXPECT_TRUE(sameMove(d.best, mv("e2", "e4")));
    EXPECT_EQ(d.framesUsed, 4);
}

TEST(MoveDecoderMultiFrame, ContradictoryFramesDoNotResolve) {
    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    const SoftObs towardE4 = e3e4Obs(0.4f, 0.6f);
    const SoftObs towardE3 = e3e4Obs(0.6f, 0.4f);

    Decoding d;
    for(int i = 0; i < 10; ++i) {
        d = dec.observe(i % 2 == 0 ? towardE4 : towardE3);
    }
    EXPECT_EQ(d.kind, Kind::Ambiguous);
    EXPECT_NEAR(d.margin, 0.0, 1e-6);
}

// ---------------------------------------------------------------- state handling

TEST(MoveDecoderState, ResetClearsEvidenceAndFrameCount) {
    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    dec.observe(obsFromFen(kAfterE4Black));
    dec.observe(obsFromFen(kAfterE4Black));

    dec.reset(boardFromFen(kStartFen));
    const Decoding d = dec.observe(obsFromFen(kStartFen));
    EXPECT_EQ(d.framesUsed, 1);
    EXPECT_EQ(d.kind, Kind::NoChange);
}

TEST(MoveDecoderState, ClearEvidenceKeepsLegalMoveCache) {
    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    const int count = dec.legalMoveCount();

    dec.observe(obsFromFen(kAfterE4Black));
    dec.clearEvidence();

    EXPECT_EQ(dec.legalMoveCount(), count);
    const Decoding d = dec.observe(obsFromFen(kStartFen));
    EXPECT_EQ(d.framesUsed, 1);
    EXPECT_EQ(d.kind, Kind::NoChange);
}

TEST(MoveDecoderState, ResetOnNewPositionDecodesNextMove) {
    MoveDecoder dec;
    dec.reset(boardFromFen(kStartFen));
    const Decoding first = dec.observe(obsFromFen(kAfterE4Black));
    ASSERT_EQ(first.kind, Kind::Confident);

    // Commit, then decode black's reply from the new position
    dec.reset(boardFromFen(kAfterE4Black));
    const Decoding second = dec.observe(
        obsFromFen("rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2"));
    EXPECT_EQ(second.kind, Kind::Confident);
    EXPECT_TRUE(sameMove(second.best, mv("e7", "e5")));
}