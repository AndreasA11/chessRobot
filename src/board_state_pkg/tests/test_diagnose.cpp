#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "BoardState.hpp"
#include "Diagnose.hpp"

using Diagnose::Diagnosis;
using Diagnose::Verdict;

namespace {

const std::string kStart = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

Square sq(const std::string& s) { return Square{s[0] - 'a', s[1] - '1'}; }  // {file, rank}

Diagnosis diag(const std::string& fen, const std::string& uci) {
    return Diagnose::diagnoseMove(FEN::fromFEN(fen), UCI::parseUCI(uci));
}

// Every from/to pair (adding 'q' for pawn moves to the last rank) that diagnoseMove calls Legal.
std::vector<std::string> legalMoves(const BoardState& b) {
    std::vector<std::string> out;
    for (int fr = 0; fr < 8; ++fr)
        for (int ff = 0; ff < 8; ++ff)
            for (int tr = 0; tr < 8; ++tr)
                for (int tf = 0; tf < 8; ++tf) {
                    const Square from{ff, fr}, to{tf, tr};
                    std::string u = BoardState::squareToString(from) + BoardState::squareToString(to);
                    const Piece p = b.at(from);
                    if (p != Piece::Empty && BoardState::typeOf(p) == 'P' && (tr == 0 || tr == 7)) u += 'q';
                    if (Diagnose::diagnoseMove(b, UCI::parseUCI(u)).verdict == Verdict::Legal)
                        out.push_back(u);
                }
    return out;
}

// perft(1) count: a legal promotion counts as 4 (q, r, b, n)
int perft1(const std::string& fen) {
    int n = 0;
    for (const auto& u : legalMoves(FEN::fromFEN(fen))) n += (u.size() == 5) ? 4 : 1;
    return n;
}

}  // namespace

// ------------------------------------------------------------ generic checks

TEST(Diagnose, LegalOpeningMoves) {
    EXPECT_EQ(diag(kStart, "e2e4").verdict, Verdict::Legal);
    EXPECT_EQ(diag(kStart, "e2e3").verdict, Verdict::Legal);
    EXPECT_EQ(diag(kStart, "g1f3").verdict, Verdict::Legal);  // knight jumps
}

TEST(Diagnose, NoPieceAtSource) {
    Diagnosis d = diag(kStart, "e4e5");
    EXPECT_EQ(d.verdict, Verdict::NoPieceAtSource);
    ASSERT_FALSE(d.squares.empty());
    EXPECT_EQ(d.squares[0], sq("e4"));
}

TEST(Diagnose, WrongTurn) {
    EXPECT_EQ(diag(kStart, "e7e5").verdict, Verdict::WrongTurn);
}

TEST(Diagnose, SameSquareIsNotAMove) {
    EXPECT_EQ(diag(kStart, "e2e2").verdict, Verdict::IllegalPieceGeometry);
}

TEST(Diagnose, CapturesOwnPiece) {
    Diagnosis d = diag(kStart, "d1d2");
    EXPECT_EQ(d.verdict, Verdict::CapturesOwnPiece);
    ASSERT_FALSE(d.squares.empty());
    EXPECT_EQ(d.squares[0], sq("d2"));
    EXPECT_EQ(diag(kStart, "g1e2").verdict, Verdict::CapturesOwnPiece);
}

// ------------------------------------------------------------------ geometry

TEST(Diagnose, PieceGeometry) {
    EXPECT_EQ(diag("4k3/8/8/8/8/8/8/2B1K3 w - - 0 1", "c1c3").verdict, Verdict::IllegalPieceGeometry);  // bishop straight
    EXPECT_EQ(diag("4k3/8/8/8/8/8/8/R3K3 w - - 0 1", "a1b2").verdict, Verdict::IllegalPieceGeometry);   // rook diagonal
    EXPECT_EQ(diag(kStart, "g1g3").verdict, Verdict::IllegalPieceGeometry);                              // knight
    EXPECT_EQ(diag("4k3/8/8/8/8/8/8/4K3 w - - 0 1", "e1e3").verdict, Verdict::IllegalPieceGeometry);     // king two steps
    EXPECT_EQ(diag("4k3/8/8/8/8/8/8/Q3K3 w - - 0 1", "a1b3").verdict, Verdict::IllegalPieceGeometry);   // queen like a knight
}

TEST(Diagnose, SlidingPiecesBlocked) {
    Diagnosis d = diag(kStart, "a1a3");
    EXPECT_EQ(d.verdict, Verdict::Blocked);
    ASSERT_FALSE(d.squares.empty());
    EXPECT_EQ(d.squares[0], sq("a2"));

    d = diag(kStart, "f1c4");
    EXPECT_EQ(d.verdict, Verdict::Blocked);
    EXPECT_EQ(d.squares[0], sq("e2"));

    EXPECT_EQ(diag(kStart, "d1d3").verdict, Verdict::Blocked);
}

TEST(Diagnose, SlidingPieceMayCaptureEnemy) {
    EXPECT_EQ(diag("r3k3/8/8/8/8/8/8/R3K3 w - - 0 1", "a1a8").verdict, Verdict::Legal);  // open file
    EXPECT_EQ(diag("4k3/8/8/3p4/8/8/8/1B2K3 w - - 0 1", "b1d3").verdict, Verdict::Legal);
}

// --------------------------------------------------------------------- pawns

TEST(Diagnose, PawnPushes) {
    const std::string f = "4k3/8/8/8/4P3/8/8/4K3 w - - 0 1";
    EXPECT_EQ(diag(f, "e4e5").verdict, Verdict::Legal);
    EXPECT_EQ(diag(f, "e4e6").verdict, Verdict::IllegalPieceGeometry);  // double push off start rank
    EXPECT_EQ(diag(f, "e4e3").verdict, Verdict::IllegalPieceGeometry);  // backwards
    EXPECT_EQ(diag(f, "e4f5").verdict, Verdict::IllegalPieceGeometry);  // diagonal, nothing to take
}

TEST(Diagnose, PawnBlockedByAnyPiece) {
    // Own piece in front: Blocked, not CapturesOwnPiece
    Diagnosis d = diag("4k3/8/8/8/8/4N3/4P3/4K3 w - - 0 1", "e2e3");
    EXPECT_EQ(d.verdict, Verdict::Blocked);
    ASSERT_FALSE(d.squares.empty());
    EXPECT_EQ(d.squares[0], sq("e3"));

    // Enemy piece in front: pawns cannot capture forward
    EXPECT_EQ(diag("4k3/8/8/8/8/4n3/4P3/4K3 w - - 0 1", "e2e3").verdict, Verdict::Blocked);
    // Double push: blocked on the middle square, and on the destination square
    d = diag("4k3/8/8/8/8/4n3/4P3/4K3 w - - 0 1", "e2e4");
    EXPECT_EQ(d.verdict, Verdict::Blocked);
    EXPECT_EQ(d.squares[0], sq("e3"));
    d = diag("4k3/8/8/8/4n3/8/4P3/4K3 w - - 0 1", "e2e4");
    EXPECT_EQ(d.verdict, Verdict::Blocked);
    EXPECT_EQ(d.squares[0], sq("e4"));
}

TEST(Diagnose, PawnCaptures) {
    const std::string f = "4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1";
    EXPECT_EQ(diag(f, "e4d5").verdict, Verdict::Legal);
    EXPECT_EQ(diag("4k3/8/8/3P4/4P3/8/8/4K3 w - - 0 1", "e4d5").verdict, Verdict::CapturesOwnPiece);
}

TEST(Diagnose, BlackPawnsMoveDown) {
    const std::string f = "4k3/4p3/8/8/8/8/8/4K3 b - - 0 1";
    EXPECT_EQ(diag(f, "e7e5").verdict, Verdict::Legal);
    EXPECT_EQ(diag(f, "e7e8").verdict, Verdict::IllegalPieceGeometry);
}

TEST(Diagnose, EnPassant) {
    EXPECT_EQ(diag("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6").verdict, Verdict::Legal);
    EXPECT_EQ(diag("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1", "d4e3").verdict, Verdict::Legal);
    // Same position without the en passant target
    EXPECT_EQ(diag("4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1", "e5d6").verdict, Verdict::IllegalPieceGeometry);
}

TEST(Diagnose, EnPassantThatExposesKing) {
    // Removing both pawns from the 5th rank opens the rook's line to the king
    Diagnosis d = diag("8/8/8/KPp4r/8/8/8/4k3 w - c6 0 1", "b5c6");
    EXPECT_EQ(d.verdict, Verdict::LeavesKingInCheck);
    ASSERT_EQ(d.squares.size(), 2u);
    EXPECT_EQ(d.squares[0], sq("a5"));  // king
    EXPECT_EQ(d.squares[1], sq("h5"));  // attacker
}

// ----------------------------------------------------------------- promotion

TEST(Diagnose, Promotion) {
    const std::string w = "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1";
    Diagnosis d = diag(w, "b7b8");
    EXPECT_EQ(d.verdict, Verdict::AmbiguousPromotion);
    ASSERT_FALSE(d.squares.empty());
    EXPECT_EQ(d.squares[0], sq("b8"));
    for (const char* u : {"b7b8q", "b7b8r", "b7b8b", "b7b8n"}) EXPECT_EQ(diag(w, u).verdict, Verdict::Legal) << u;

    const std::string b = "4k3/8/8/8/8/8/1p6/4K3 b - - 0 1";
    EXPECT_EQ(diag(b, "b2b1").verdict, Verdict::AmbiguousPromotion);
    EXPECT_EQ(diag(b, "b2b1n").verdict, Verdict::Legal);
}

TEST(Diagnose, PromotionPieceOnNonPromotingMoveIsRejected) {
    EXPECT_EQ(diag(kStart, "e2e4q").verdict, Verdict::IllegalPieceGeometry);
}

TEST(Diagnose, PromotionByCapture) {
    EXPECT_EQ(diag("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7b8q").verdict, Verdict::Legal);
}

// ---------------------------------------------------------------------- check

TEST(Diagnose, PinnedPieceCannotMove) {
    Diagnosis d = diag("4k3/4r3/8/8/8/8/4N3/4K3 w - - 0 1", "e2c3");
    EXPECT_EQ(d.verdict, Verdict::LeavesKingInCheck);
    ASSERT_EQ(d.squares.size(), 2u);
    EXPECT_EQ(d.squares[0], sq("e1"));
    EXPECT_EQ(d.squares[1], sq("e7"));
}

TEST(Diagnose, PinnedPieceMayMoveAlongPin) {
    // Rook pinned on the e-file can still move along it, even capturing the pinner
    EXPECT_EQ(diag("4k3/4r3/8/8/8/8/4R3/4K3 w - - 0 1", "e2e7").verdict, Verdict::Legal);
    EXPECT_EQ(diag("4k3/4r3/8/8/8/8/4R3/4K3 w - - 0 1", "e2a2").verdict, Verdict::LeavesKingInCheck);
}

TEST(Diagnose, KingCannotWalkIntoCheck) {
    const std::string f = "4k3/8/8/8/8/8/3r4/4K3 w - - 0 1";
    EXPECT_EQ(diag(f, "e1e2").verdict, Verdict::LeavesKingInCheck);
    EXPECT_EQ(diag(f, "e1d2").verdict, Verdict::Legal);  // takes the undefended rook
}

TEST(Diagnose, MustAnswerCheck) {
    const std::string f = "4r1k1/8/8/8/8/8/P7/4K3 w - - 0 1";
    EXPECT_EQ(diag(f, "a2a3").verdict, Verdict::LeavesKingInCheck);
    EXPECT_EQ(diag(f, "e1d1").verdict, Verdict::Legal);
}

// ------------------------------------------------------------------ castling

TEST(Diagnose, CastlingLegalAllFourWays) {
    const std::string w = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
    const std::string b = "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1";
    EXPECT_EQ(diag(w, "e1g1").verdict, Verdict::Legal);
    EXPECT_EQ(diag(w, "e1c1").verdict, Verdict::Legal);
    EXPECT_EQ(diag(b, "e8g8").verdict, Verdict::Legal);
    EXPECT_EQ(diag(b, "e8c8").verdict, Verdict::Legal);
}

TEST(Diagnose, CastlingRightsLost) {
    EXPECT_EQ(diag("r3k2r/8/8/8/8/8/8/R3K2R w Qkq - 0 1", "e1g1").verdict, Verdict::CastlingNotAllowed);
    EXPECT_EQ(diag("r3k2r/8/8/8/8/8/8/R3K2R w Kkq - 0 1", "e1c1").verdict, Verdict::CastlingNotAllowed);
    EXPECT_EQ(diag("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1", "e1g1").verdict, Verdict::CastlingNotAllowed);
}

TEST(Diagnose, CastlingRookMissing) {
    EXPECT_EQ(diag("4k3/8/8/8/8/8/8/4K3 w K - 0 1", "e1g1").verdict, Verdict::CastlingNotAllowed);
}

TEST(Diagnose, CastlingBlocked) {
    Diagnosis d = diag("r3k2r/8/8/8/8/8/8/RN2K2R w KQkq - 0 1", "e1c1");  // b1 occupied
    EXPECT_EQ(d.verdict, Verdict::Blocked);
    ASSERT_FALSE(d.squares.empty());
    EXPECT_EQ(d.squares[0], sq("b1"));

    d = diag("r3k2r/8/8/8/8/8/8/R3KN1R w KQkq - 0 1", "e1g1");  // f1 occupied
    EXPECT_EQ(d.verdict, Verdict::Blocked);
    EXPECT_EQ(d.squares[0], sq("f1"));
}

TEST(Diagnose, CastlingThroughAttackedSquare) {
    // black rook on f8 attacks f1
    EXPECT_EQ(diag("r3kr2/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1g1").verdict, Verdict::CastlingNotAllowed);
    // queenside is still fine
    EXPECT_EQ(diag("r3kr2/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1c1").verdict, Verdict::Legal);
}

TEST(Diagnose, CastlingIntoAttackedSquare) {
    // black rook on g8 attacks g1
    EXPECT_EQ(diag("r3k1r1/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1g1").verdict, Verdict::CastlingNotAllowed);
}

TEST(Diagnose, CastlingOutOfCheck) {
    EXPECT_EQ(diag("4k3/8/8/8/8/8/4r3/R3K2R w KQ - 0 1", "e1g1").verdict, Verdict::CastlingNotAllowed);
}

TEST(Diagnose, QueensideCastleMayPassOverAttackedBFile) {
    // rook on b8 attacks b1, which only the rook (not the king) crosses
    EXPECT_EQ(diag("1r2k3/8/8/8/8/8/8/R3K3 w Q - 0 1", "e1c1").verdict, Verdict::Legal);
}

// ------------------------------------------------------------------ describe

TEST(Diagnose, DescribeGivesDistinctNonEmptyText) {
    const std::vector<Verdict> all = {
        Verdict::Legal,           Verdict::NoPieceAtSource,   Verdict::WrongTurn,
        Verdict::CapturesOwnPiece, Verdict::IllegalPieceGeometry, Verdict::Blocked,
        Verdict::LeavesKingInCheck, Verdict::CastlingNotAllowed, Verdict::AmbiguousPromotion};
    std::vector<std::string> seen;
    for (Verdict v : all) {
        const std::string s = Diagnose::describe(v);
        EXPECT_FALSE(s.empty());
        EXPECT_NE(s, "Unknown");
        for (const auto& t : seen) EXPECT_NE(s, t);
        seen.push_back(s);
    }
}

// ------------------------------------------------- perft(1) oracle (no engine)
// Counting the Legal verdicts over every from/to pair must match the known perft(1) numbers.

TEST(DiagnosePerft, StartPosition) { EXPECT_EQ(perft1(kStart), 20); }

TEST(DiagnosePerft, Kiwipete) {
    EXPECT_EQ(perft1("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"), 48);
}

TEST(DiagnosePerft, PinsAndEnPassantEndgame) {
    EXPECT_EQ(perft1("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"), 14);
}

TEST(DiagnosePerft, PromotionsAndChecks) {
    EXPECT_EQ(perft1("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"), 6);
    EXPECT_EQ(perft1("r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1"), 6);  // mirrored, black to move
    EXPECT_EQ(perft1("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"), 44);
}

TEST(DiagnosePerft, MiddlegameWithBothSidesDeveloped) {
    EXPECT_EQ(perft1("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"), 46);
}

// Every move called Legal must apply cleanly and produce a FEN that parses back unchanged.
TEST(DiagnosePerft, LegalMovesApplyAndRoundTrip) {
    const std::vector<std::string> fens = {
        kStart,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
    };
    for (const auto& fen : fens) {
        const BoardState b = FEN::fromFEN(fen);
        for (const auto& u : legalMoves(b)) {
            BoardState after = b;
            ASSERT_NO_THROW(after.applyMove(UCI::parseUCI(u))) << fen << " " << u;
            const std::string out = FEN::toFEN(after);
            EXPECT_EQ(FEN::toFEN(FEN::fromFEN(out)), out) << fen << " " << u;
        }
    }
}