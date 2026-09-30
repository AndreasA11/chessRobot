#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "boardState.hpp"

namespace {

const std::string kStartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Play a sequence of UCI moves from a FEN and return the resulting FEN.
std::string play(const std::string& fen, const std::vector<std::string>& moves) {
    BoardState b = FEN::fromFEN(fen);
    for (const auto& m : moves) b.applyMove(UCI::parseUCI(m));
    return FEN::toFEN(b);
}

}  // namespace

// ------------------------------------------------------------------ FEN

TEST(FEN, StartPositionRoundTrips) {
    EXPECT_EQ(FEN::toFEN(FEN::fromFEN(kStartFen)), kStartFen);
}

TEST(FEN, ComplexPositionsRoundTrip) {
    const std::vector<std::string> fens = {
        // "Kiwipete": castling, pins, en passant tricks
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 3",
        "r3k2r/8/8/8/8/8/8/R3K2R b Kq - 12 40",
        "4k3/8/8/8/8/8/8/4K3 w - - 0 1",
    };
    for (const auto& fen : fens) EXPECT_EQ(FEN::toFEN(FEN::fromFEN(fen)), fen) << fen;
}

TEST(FEN, ParsesSideCastlingAndEnPassantFields) {
    BoardState b = FEN::fromFEN("4k3/8/8/3pP3/8/8/8/4K3 b - d6 7 20");
    EXPECT_EQ(b.sideToMove(), Color::Black);
    EXPECT_EQ(b.at({4, 4}), Piece::WP);  // e5
    EXPECT_EQ(b.at({3, 4}), Piece::BP);  // d5
    EXPECT_EQ(b.at({4, 0}), Piece::WK);  // e1
    EXPECT_EQ(b.at({4, 7}), Piece::BK);  // e8
    EXPECT_EQ(b.at({0, 0}), Piece::Empty);
}

TEST(FEN, MalformedInputThrows) {
    const std::vector<std::string> bad = {
        "",
        "not a fen",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1",                 // too few ranks
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR/8 w KQkq - 0 1",      // too many ranks
        "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",        // rank overflow
        "rnbqkbnr/ppppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",       // 9 pieces
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNX w KQkq - 0 1",        // bad piece
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1",        // bad side
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkz - 0 1",        // bad castling
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq z9 0 1",       // bad ep square
    };
    for (const auto& fen : bad) EXPECT_THROW(FEN::fromFEN(fen), std::invalid_argument) << fen;
}

// ------------------------------------------------------------------ UCI

TEST(UCI, ParsesNormalMove) {
    Move m = UCI::parseUCI("e2e4");
    EXPECT_EQ(m.from, (Square{4, 1}));
    EXPECT_EQ(m.to, (Square{4, 3}));
    EXPECT_EQ(m.promotion, Piece::Empty);
}

TEST(UCI, ParsesPromotionAsLowercaseTag) {
    EXPECT_EQ(UCI::parseUCI("e7e8q").promotion, Piece::BQ);
    EXPECT_EQ(UCI::parseUCI("a2a1n").promotion, Piece::BN);
    EXPECT_EQ(UCI::parseUCI("a2a1R").promotion, Piece::BR);  // case-insensitive input
}

TEST(UCI, MalformedInputThrows) {
    for (const std::string s : {"", "e2", "e2e", "e2e4e5", "i2e4", "e9e4", "e0e4", "e2e4k", "e7e8p",
                                "(none)", "0000"})
        EXPECT_THROW(UCI::parseUCI(s), std::invalid_argument) << s;
}

// ------------------------------------------------------------ describeMove

TEST(DescribeMove, QuietMove) {
    BoardState b = FEN::fromFEN(kStartFen);
    MoveInfo i = b.describeMove(UCI::parseUCI("g1f3"));
    EXPECT_EQ(i.moved, Piece::WN);
    EXPECT_EQ(i.captured, Piece::Empty);
    EXPECT_FALSE(i.isCastle);
    EXPECT_EQ(i.promotion, Piece::Empty);
}

TEST(DescribeMove, Capture) {
    BoardState b = FEN::fromFEN("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
    MoveInfo i = b.describeMove(UCI::parseUCI("e4d5"));
    EXPECT_EQ(i.moved, Piece::WP);
    EXPECT_EQ(i.captured, Piece::BP);
    EXPECT_EQ(i.captureSquare, (Square{3, 4}));
}

TEST(DescribeMove, EnPassantCaptureSquareDiffersFromDestination) {
    BoardState w = FEN::fromFEN("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    MoveInfo i = w.describeMove(UCI::parseUCI("e5d6"));
    EXPECT_EQ(i.captured, Piece::BP);
    EXPECT_EQ(i.captureSquare, (Square{3, 4}));  // d5, not d6

    BoardState bl = FEN::fromFEN("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1");
    MoveInfo j = bl.describeMove(UCI::parseUCI("d4e3"));
    EXPECT_EQ(j.captured, Piece::WP);
    EXPECT_EQ(j.captureSquare, (Square{4, 3}));  // e4, not e3
}

TEST(DescribeMove, PawnDiagonalWithoutEpSquareIsNotEnPassant) {
    // No ep target set, so a diagonal pawn move onto an empty square is not flagged as a capture
    BoardState b = FEN::fromFEN("4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1");
    EXPECT_EQ(b.describeMove(UCI::parseUCI("e5d6")).captured, Piece::Empty);
}

TEST(DescribeMove, CastlingAllFourWays) {
    BoardState b = FEN::fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");

    MoveInfo wk = b.describeMove(UCI::parseUCI("e1g1"));
    EXPECT_TRUE(wk.isCastle);
    EXPECT_EQ(wk.rookFrom, (Square{7, 0}));
    EXPECT_EQ(wk.rookTo, (Square{5, 0}));

    MoveInfo wq = b.describeMove(UCI::parseUCI("e1c1"));
    EXPECT_TRUE(wq.isCastle);
    EXPECT_EQ(wq.rookFrom, (Square{0, 0}));
    EXPECT_EQ(wq.rookTo, (Square{3, 0}));

    BoardState bl = FEN::fromFEN("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
    MoveInfo bk = bl.describeMove(UCI::parseUCI("e8g8"));
    EXPECT_TRUE(bk.isCastle);
    EXPECT_EQ(bk.rookFrom, (Square{7, 7}));
    EXPECT_EQ(bk.rookTo, (Square{5, 7}));

    MoveInfo bq = bl.describeMove(UCI::parseUCI("e8c8"));
    EXPECT_TRUE(bq.isCastle);
    EXPECT_EQ(bq.rookFrom, (Square{0, 7}));
    EXPECT_EQ(bq.rookTo, (Square{3, 7}));
}

TEST(DescribeMove, OrdinaryKingStepIsNotCastling) {
    BoardState b = FEN::fromFEN("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
    EXPECT_FALSE(b.describeMove(UCI::parseUCI("e1f1")).isCastle);
}

TEST(DescribeMove, PromotionIsColorizedToMover) {
    BoardState w = FEN::fromFEN("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1");
    EXPECT_EQ(w.describeMove(UCI::parseUCI("b7b8q")).promotion, Piece::WQ);
    BoardState b = FEN::fromFEN("4k3/8/8/8/8/8/1p6/4K3 b - - 0 1");
    EXPECT_EQ(b.describeMove(UCI::parseUCI("b2b1n")).promotion, Piece::BN);
}

TEST(DescribeMove, EmptySourceSquareThrows) {
    BoardState b = FEN::fromFEN(kStartFen);
    EXPECT_THROW(b.describeMove(UCI::parseUCI("e4e5")), std::invalid_argument);
}

TEST(DescribeMove, DoesNotMutateBoard) {
    BoardState b = FEN::fromFEN(kStartFen);
    b.describeMove(UCI::parseUCI("e2e4"));
    EXPECT_EQ(FEN::toFEN(b), kStartFen);
}

// --------------------------------------------------------------- applyMove

TEST(ApplyMove, DoublePawnPushSetsEnPassantAndFlipsSide) {
    EXPECT_EQ(play(kStartFen, {"e2e4"}),
              "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    EXPECT_EQ(play(kStartFen, {"e2e4", "c7c5"}),
              "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2");
}

TEST(ApplyMove, EnPassantTargetClearsAfterNextMove) {
    EXPECT_EQ(play(kStartFen, {"e2e4", "e7e5", "g1f3"}),
              "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2");
}

TEST(ApplyMove, EnPassantRemovesCapturedPawn) {
    EXPECT_EQ(play("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", {"e5d6"}), "4k3/8/3P4/8/8/8/8/4K3 b - - 0 1");
    EXPECT_EQ(play("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1", {"d4e3"}), "4k3/8/8/8/8/4p3/8/4K3 w - - 0 2");
}

TEST(ApplyMove, CastlingMovesKingAndRookAndDropsRights) {
    const std::string f = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
    EXPECT_EQ(play(f, {"e1g1"}), "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1");
    EXPECT_EQ(play(f, {"e1c1"}), "r3k2r/8/8/8/8/8/8/2KR3R b kq - 1 1");
    EXPECT_EQ(play(f, {"e1g1", "e8c8"}), "2kr3r/8/8/8/8/8/8/R4RK1 w - - 2 2");
    EXPECT_EQ(play(f, {"e1g1", "e8g8"}), "r4rk1/8/8/8/8/8/8/R4RK1 w - - 2 2");
}

TEST(ApplyMove, KingMoveDropsBothRightsForThatSide) {
    EXPECT_EQ(play("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", {"e1e2"}),
              "r3k2r/8/8/8/8/8/4K3/R6R b kq - 1 1");
}

TEST(ApplyMove, RookMoveDropsOnlyThatRook) {
    EXPECT_EQ(play("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", {"h1h2"}),
              "r3k2r/8/8/8/8/8/7R/R3K3 b Qkq - 1 1");
    EXPECT_EQ(play("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", {"a1a2"}),
              "r3k2r/8/8/8/8/8/R7/4K2R b Kkq - 1 1");
}

TEST(ApplyMove, CapturingRookOnCornerDropsOpponentRight) {
    // White bishop takes the h8 rook: black loses kingside castling
    EXPECT_EQ(play("r3k2r/8/8/8/8/8/1B6/R3K2R w KQkq - 0 1", {"b2h8"}),
              "r3k2B/8/8/8/8/8/8/R3K2R b KQq - 0 1");
}

TEST(ApplyMove, PromotionAllPiecesBothColors) {
    const std::string w = "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1";
    EXPECT_EQ(play(w, {"b7b8q"}), "1Q2k3/8/8/8/8/8/8/4K3 b - - 0 1");
    EXPECT_EQ(play(w, {"b7b8r"}), "1R2k3/8/8/8/8/8/8/4K3 b - - 0 1");
    EXPECT_EQ(play(w, {"b7b8b"}), "1B2k3/8/8/8/8/8/8/4K3 b - - 0 1");
    EXPECT_EQ(play(w, {"b7b8n"}), "1N2k3/8/8/8/8/8/8/4K3 b - - 0 1");
    EXPECT_EQ(play("4k3/8/8/8/8/8/6p1/4K3 b - - 0 1", {"g2g1n"}), "4k3/8/8/8/8/8/8/4K1n1 w - - 0 2");
}

TEST(ApplyMove, PromotionWithCaptureOnRookCorner) {
    EXPECT_EQ(play("r3k2r/1P6/8/8/8/8/8/4K3 w kq - 0 1", {"b7a8q"}), "Q3k2r/8/8/8/8/8/8/4K3 b k - 0 1");
}

TEST(ApplyMove, HalfmoveClockResetsOnPawnMoveAndCapture) {
    // Knight shuffles: clock counts up
    EXPECT_EQ(play(kStartFen, {"g1f3", "g8f6", "f3g1", "f6g8"}), kStartFen.substr(0, kStartFen.size() - 3) + "4 3");
    // Pawn move resets
    EXPECT_EQ(play(kStartFen, {"g1f3", "g8f6", "e2e4"}),
              "rnbqkb1r/pppppppp/5n2/8/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq e3 0 2");
    // Capture resets
    EXPECT_EQ(play("4k3/8/8/3p4/8/8/8/3RK3 w - - 9 30", {"d1d5"}), "4k3/8/8/3R4/8/8/8/4K3 b - - 0 30");
}

TEST(ApplyMove, FullmoveIncrementsAfterBlackOnly) {
    BoardState b = FEN::fromFEN(kStartFen);
    b.applyMove(UCI::parseUCI("e2e4"));
    EXPECT_EQ(FEN::toFEN(b).substr(FEN::toFEN(b).size() - 1), "1");
    b.applyMove(UCI::parseUCI("e7e5"));
    EXPECT_EQ(FEN::toFEN(b).substr(FEN::toFEN(b).size() - 1), "2");
}

// ------------------------------------------------------ whole-game sequences

TEST(Sequences, RuyLopezOpening) {
    EXPECT_EQ(play(kStartFen, {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6"}),
              "r1bqkbnr/1ppp1ppp/p1n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 4");
}

TEST(Sequences, ScholarsMate) {
    EXPECT_EQ(play(kStartFen, {"e2e4", "e7e5", "f1c4", "b8c6", "d1h5", "g8f6", "h5f7"}),
              "r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4");
}

TEST(Sequences, ShortCastleIntoGame) {
    // Italian: both sides castle kingside
    EXPECT_EQ(play(kStartFen, {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "e1g1", "f8c5", "d2d3", "e8g8"}),
              "r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2B1P3/3P1N2/PPP2PPP/RNBQ1RK1 w - - 1 6");
}
