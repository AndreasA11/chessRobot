#include <gtest/gtest.h>

#include <vector>

#include "Diagnose.hpp"
#include "test_helpers.hpp"

using namespace testutil;
using Diagnose::LegalMove;
using Diagnose::legalMoves;

namespace {

std::vector<LegalMove> movesOf(const std::string& fen) {
    return legalMoves(boardFromFen(fen));
}

bool contains(const std::vector<LegalMove>& v, const Move& m) {
    for(const LegalMove& lm : v) {
        if(sameMove(lm.move, m)) {
            return true;
        }
    }
    return false;
}

int promotionCount(const std::vector<LegalMove>& v) {
    int n = 0;
    for(const LegalMove& lm : v) {
        n += lm.promotionUnspecified ? 1 : 0;
    }
    return n;
}

}  // namespace

TEST(LegalMoves, StartPositionHasTwentyMoves) {
    EXPECT_EQ(movesOf(kStartFen).size(), 20u);
}

TEST(LegalMoves, OnlySideToMoveMovesAppear) {
    const BoardState b = boardFromFen(
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    const auto moves = legalMoves(b);
    EXPECT_EQ(moves.size(), 20u);
    for(const LegalMove& lm : moves) {
        const Piece p = b.at(lm.move.from);
        ASSERT_FALSE(BoardState::isEmpty(p));
        EXPECT_EQ(BoardState::colorOf(p), Color::Black);
    }
}

TEST(LegalMoves, EveryMoveValidatesAsLegalOrAmbiguousPromotion) {
    const BoardState b = boardFromFen(kStartFen);
    for(const LegalMove& lm : legalMoves(b)) {
        const auto v = Diagnose::diagnoseMove(b, lm.move).verdict;
        EXPECT_TRUE(v == Diagnose::Verdict::Legal ||
                    v == Diagnose::Verdict::AmbiguousPromotion);
    }
}

TEST(LegalMoves, NoCastlingWithoutRights) {
    // Rook a1: 10 moves, king e1: 5 moves
    const auto moves = movesOf("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
    EXPECT_EQ(moves.size(), 15u);
    EXPECT_FALSE(contains(moves, mv("e1", "c1")));
}

TEST(LegalMoves, BothCastlingMovesPresentWhenAllowed) {
    const auto moves = movesOf("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
    EXPECT_TRUE(contains(moves, mv("e1", "g1")));
    EXPECT_TRUE(contains(moves, mv("e1", "c1")));
}

TEST(LegalMoves, CastlingThroughAttackedSquareIsExcluded) {
    // Black rook on f8 attacks f1, so kingside castling is illegal; queenside is fine
    const auto moves = movesOf("5r1k/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    EXPECT_FALSE(contains(moves, mv("e1", "g1")));
    EXPECT_TRUE(contains(moves, mv("e1", "c1")));
}

TEST(LegalMoves, PinnedPieceHasNoMoves) {
    // Bishop e2 is pinned by the rook on e7: only the 4 king moves remain
    const auto moves = movesOf("4k3/4r3/8/8/8/8/4B3/4K3 w - - 0 1");
    EXPECT_EQ(moves.size(), 4u);
    EXPECT_FALSE(contains(moves, mv("e2", "d3")));
}

TEST(LegalMoves, EnPassantIsIncluded) {
    const auto moves = movesOf("rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3");
    EXPECT_TRUE(contains(moves, mv("d4", "e3")));
}

TEST(LegalMoves, CheckmateHasNoMoves) {
    EXPECT_TRUE(movesOf("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3").empty());
}

TEST(LegalMoves, StalemateHasNoMoves) {
    EXPECT_TRUE(movesOf("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1").empty());
}

TEST(LegalMoves, PromotionAppearsOnceAndIsFlagged) {
    // Pawn a7 -> a8 (one entry, not four) plus king a1 -> a2, b1, b2
    const auto moves = movesOf("8/P6k/8/8/8/8/8/K7 w - - 0 1");
    EXPECT_EQ(moves.size(), 4u);
    EXPECT_EQ(promotionCount(moves), 1);
    ASSERT_TRUE(contains(moves, mv("a7", "a8")));
    for(const LegalMove& lm : moves) {
        if(sameMove(lm.move, mv("a7", "a8"))) {
            EXPECT_TRUE(lm.promotionUnspecified);
            EXPECT_EQ(lm.move.promotion, Piece::Empty);
        }
    }
}

// Regression for the diagnoseMove ordering bug: before the fix, a promotion that
// leaves the king in check was reported as AmbiguousPromotion and slipped in.
TEST(LegalMoves, PromotionThatLeavesKingInCheckIsExcluded) {
    // White king e1 is in check from the rook on h1. Neither b7-b8 nor b7xa8
    // addresses it, so only the king moves d2, e2, f2 are legal.
    const auto moves = movesOf("r6k/1P6/8/8/8/8/8/4K2r w - - 0 1");
    EXPECT_EQ(moves.size(), 3u);
    EXPECT_EQ(promotionCount(moves), 0);
    EXPECT_FALSE(contains(moves, mv("b7", "b8")));
    EXPECT_FALSE(contains(moves, mv("b7", "a8")));
}