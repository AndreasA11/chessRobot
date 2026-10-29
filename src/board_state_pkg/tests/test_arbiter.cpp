#include <gtest/gtest.h>

#include "Arbiter.hpp"
#include "test_helpers.hpp"

using namespace testutil;
using AK = Action::Kind;
using DK = Decoding::Kind;

namespace {

const std::string kPromoFen = "8/P6k/8/8/8/8/8/K7 w - - 0 1";

Decoding confident(const Move& m, bool promo = false) {
    Decoding d;
    d.kind = DK::Confident;
    d.best = m;
    d.promotionUnspecified = promo;
    return d;
}

Decoding ofKind(DK k) {
    Decoding d;
    d.kind = k;
    return d;
}

Decoding ambiguous(const Move& a, const Move& b) {
    Decoding d;
    d.kind = DK::Ambiguous;
    d.best = a;
    d.runnerUp = b;
    return d;
}

Occupancy hardOf(const std::string& fen) {
    return Occupancy(boardFromFen(fen));
}

ArbiterConfig quick() {
    ArbiterConfig c;
    c.confirmFrames = 1;
    c.ambiguousFrameLimit = 3;
    c.noLegalFrameLimit = 3;
    c.unusableFrameLimit = 3;
    return c;
}

}  // namespace

// ------------------------------------------------------------ confident / commit

TEST(Arbiter, CommitsAfterDefaultConfirmFrames) {
    Arbiter arb;  // confirmFrames = 2
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf(kStartFen);

    EXPECT_EQ(arb.step(b, confident(mv("e2", "e4")), h).kind, AK::Wait);
    const Action a = arb.step(b, confident(mv("e2", "e4")), h);
    EXPECT_EQ(a.kind, AK::Commit);
    EXPECT_TRUE(sameMove(a.move, mv("e2", "e4")));
}

TEST(Arbiter, CommitsImmediatelyWhenConfirmFramesIsOne) {
    Arbiter arb(quick());
    const BoardState b = boardFromFen(kStartFen);
    EXPECT_EQ(arb.step(b, confident(mv("e2", "e4")), hardOf(kStartFen)).kind, AK::Commit);
}

TEST(Arbiter, StreakResetsWhenBestMoveChanges) {
    Arbiter arb;
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf(kStartFen);

    EXPECT_EQ(arb.step(b, confident(mv("e2", "e4")), h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, confident(mv("e2", "e3")), h).kind, AK::Wait);
    const Action a = arb.step(b, confident(mv("e2", "e3")), h);
    EXPECT_EQ(a.kind, AK::Commit);
    EXPECT_TRUE(sameMove(a.move, mv("e2", "e3")));
}

TEST(Arbiter, UnusableFrameDoesNotBreakStreak) {
    Arbiter arb;
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf(kStartFen);

    EXPECT_EQ(arb.step(b, confident(mv("e2", "e4")), h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, ofKind(DK::Unusable), h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, confident(mv("e2", "e4")), h).kind, AK::Commit);
}

TEST(Arbiter, CommitIsIssuedOnlyOncePerSituation) {
    Arbiter arb(quick());
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf(kStartFen);

    EXPECT_EQ(arb.step(b, confident(mv("e2", "e4")), h).kind, AK::Commit);
    EXPECT_EQ(arb.step(b, confident(mv("e2", "e4")), h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, confident(mv("e2", "e4")), h).kind, AK::Wait);

    arb.reset();
    EXPECT_EQ(arb.step(b, confident(mv("e2", "e4")), h).kind, AK::Commit);
}

TEST(Arbiter, NoChangeWaits) {
    Arbiter arb(quick());
    const BoardState b = boardFromFen(kStartFen);
    for(int i = 0; i < 20; ++i) {
        EXPECT_EQ(arb.step(b, ofKind(DK::NoChange), hardOf(kStartFen)).kind, AK::Wait);
    }
}

TEST(Arbiter, ConfidentButIllegalMoveAsksToCheckBoardNotBlamePlayer) {
    // Decoder/validator disagreement must not be reported as a human error
    Arbiter arb(quick());
    const BoardState b = boardFromFen(kStartFen);
    const Action a = arb.step(b, confident(mv("e2", "e5")), hardOf(kStartFen));
    EXPECT_EQ(a.kind, AK::AskCheckBoard);
}

// ------------------------------------------------------------ promotion

TEST(Arbiter, UnspecifiedPromotionAsksForPiece) {
    Arbiter arb(quick());
    const BoardState b = boardFromFen(kPromoFen);
    const Action a = arb.step(b, confident(mv("a7", "a8"), true), hardOf(kPromoFen));
    EXPECT_EQ(a.kind, AK::AskPromotion);
    EXPECT_TRUE(sameMove(a.move, mv("a7", "a8")));
}

// ------------------------------------------------------------ ambiguity

TEST(Arbiter, AmbiguousWaitsThenAsksWhichMove) {
    Arbiter arb(quick());  // ambiguousFrameLimit = 3
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf(kStartFen);
    const Decoding d = ambiguous(mv("e2", "e3"), mv("e2", "e4"));

    EXPECT_EQ(arb.step(b, d, h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, d, h).kind, AK::Wait);
    const Action a = arb.step(b, d, h);
    EXPECT_EQ(a.kind, AK::AskWhichMove);
    EXPECT_TRUE(sameMove(a.move, mv("e2", "e3")));
    EXPECT_TRUE(sameMove(a.alt, mv("e2", "e4")));

    // Asked once; no spamming while the situation is unchanged
    EXPECT_EQ(arb.step(b, d, h).kind, AK::Wait);
}

TEST(Arbiter, AmbiguityResolvedByLaterEvidenceNeverAsks) {
    Arbiter arb(quick());
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf(kStartFen);
    const Decoding d = ambiguous(mv("e2", "e3"), mv("e2", "e4"));

    EXPECT_EQ(arb.step(b, d, h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, d, h).kind, AK::Wait);
    const Action a = arb.step(b, confident(mv("e2", "e4")), h);
    EXPECT_EQ(a.kind, AK::Commit);
    EXPECT_TRUE(sameMove(a.move, mv("e2", "e4")));
}

TEST(Arbiter, AmbiguousCounterRestartsAfterInterruption) {
    Arbiter arb(quick());
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf(kStartFen);
    const Decoding d = ambiguous(mv("e2", "e3"), mv("e2", "e4"));

    arb.step(b, d, h);
    arb.step(b, d, h);
    arb.step(b, ofKind(DK::NoChange), h);   // kind change resets the count
    EXPECT_EQ(arb.step(b, d, h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, d, h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, d, h).kind, AK::AskWhichMove);
}

// ------------------------------------------------------------ no legal explanation

TEST(Arbiter, NoLegalExplanationIsPatientBeforeEscalating) {
    Arbiter arb(quick());  // noLegalFrameLimit = 3
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf(kStartFen);

    EXPECT_EQ(arb.step(b, ofKind(DK::NoLegalExplanation), h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, ofKind(DK::NoLegalExplanation), h).kind, AK::Wait);
    EXPECT_NE(arb.step(b, ofKind(DK::NoLegalExplanation), h).kind, AK::Wait);
}

TEST(Arbiter, PersistentRecognizableIllegalMoveIsReportedToPlayer) {
    // Knight g1 -> g3 is not a knight move
    Arbiter arb(quick());
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf("rnbqkbnr/pppppppp/8/8/8/6N1/PPPPPPPP/RNBQKB1R b KQkq - 1 1");

    arb.step(b, ofKind(DK::NoLegalExplanation), h);
    arb.step(b, ofKind(DK::NoLegalExplanation), h);
    const Action a = arb.step(b, ofKind(DK::NoLegalExplanation), h);

    EXPECT_EQ(a.kind, AK::ReportIllegal);
    EXPECT_TRUE(sameMove(a.move, mv("g1", "g3")));
    EXPECT_EQ(a.verdict, Diagnose::Verdict::IllegalPieceGeometry);
    EXPECT_FALSE(a.message.empty());
    EXPECT_FALSE(a.squares.empty());
}

TEST(Arbiter, UnrecognizablePatternAsksToCheckBoard) {
    // Two of White's pieces moved at once: inferMove reports an anomaly
    Arbiter arb(quick());
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf("rnbqkbnr/pppppppp/8/8/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 0 1");

    arb.step(b, ofKind(DK::NoLegalExplanation), h);
    arb.step(b, ofKind(DK::NoLegalExplanation), h);
    EXPECT_EQ(arb.step(b, ofKind(DK::NoLegalExplanation), h).kind, AK::AskCheckBoard);
}

TEST(Arbiter, HardViewLooksLegalButSoftDisagreesAsksToCheckBoard) {
    // Hard labels show a perfectly legal e2-e4; the decoder still found no explanation.
    // That is a perception problem, so the player must not be blamed.
    Arbiter arb(quick());
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");

    arb.step(b, ofKind(DK::NoLegalExplanation), h);
    arb.step(b, ofKind(DK::NoLegalExplanation), h);
    EXPECT_EQ(arb.step(b, ofKind(DK::NoLegalExplanation), h).kind, AK::AskCheckBoard);
}

TEST(Arbiter, RecoversWhenBoardIsFixedAfterEscalation) {
    Arbiter arb(quick());
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy bad = hardOf("rnbqkbnr/pppppppp/8/8/8/6N1/PPPPPPPP/RNBQKB1R b KQkq - 1 1");

    arb.step(b, ofKind(DK::NoLegalExplanation), bad);
    arb.step(b, ofKind(DK::NoLegalExplanation), bad);
    ASSERT_EQ(arb.step(b, ofKind(DK::NoLegalExplanation), bad).kind, AK::ReportIllegal);

    // Player puts things right; decoder now sees nothing out of place
    EXPECT_EQ(arb.step(b, ofKind(DK::NoChange), hardOf(kStartFen)).kind, AK::Wait);
}

// ------------------------------------------------------------ unusable / reset

TEST(Arbiter, LongBlockedViewRequestsClearViewOnce) {
    Arbiter arb(quick());  // unusableFrameLimit = 3
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf(kStartFen);

    EXPECT_EQ(arb.step(b, ofKind(DK::Unusable), h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, ofKind(DK::Unusable), h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, ofKind(DK::Unusable), h).kind, AK::RequestClearView);
    EXPECT_EQ(arb.step(b, ofKind(DK::Unusable), h).kind, AK::Wait);

    // A usable frame clears the condition
    arb.step(b, ofKind(DK::NoChange), h);
    arb.step(b, ofKind(DK::Unusable), h);
    arb.step(b, ofKind(DK::Unusable), h);
    EXPECT_EQ(arb.step(b, ofKind(DK::Unusable), h).kind, AK::RequestClearView);
}

TEST(Arbiter, ResetClearsAllCounters) {
    Arbiter arb(quick());
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy h = hardOf(kStartFen);
    const Decoding d = ambiguous(mv("e2", "e3"), mv("e2", "e4"));

    arb.step(b, d, h);
    arb.step(b, d, h);
    arb.reset();
    EXPECT_EQ(arb.step(b, d, h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, d, h).kind, AK::Wait);
    EXPECT_EQ(arb.step(b, d, h).kind, AK::AskWhichMove);
}