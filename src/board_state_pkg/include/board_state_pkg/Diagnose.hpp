#ifndef H_DIAGNOSE
#define H_DIAGNOSE

#include "BoardState.hpp"

#include <string>
#include <vector>

namespace Diagnose {

enum class Verdict {
    Legal,
    NoPieceAtSource,
    WrongTurn,
    CapturesOwnPiece,
    IllegalPieceGeometry,
    Blocked,
    LeavesKingInCheck,
    CastlingNotAllowed,
    AmbiguousPromotion
};

struct LegalMove {
    Move move;
    bool promotionUnspecified = false;  // pawn reaches the last rank; piece not chosen yet
};

// Every legal move for the side to move, built on diagnoseMove so there is a
// single definition of legality. A promotion appears once (not four times),
// with move.promotion left Empty and promotionUnspecified set.
// An empty result means checkmate or stalemate.
std::vector<LegalMove> legalMoves(const BoardState& b);



struct Diagnosis {
    std::vector<Square> squares{}; // Blocker, checking piece, etc.
    Verdict verdict = Verdict::Legal;
};

// Validates a move against board rules and returns detailed diagnosis
Diagnosis diagnoseMove(const BoardState &b, const Move &m);

// Returns true if a move is legal according to diagnosis
inline bool moveIsLegal(const BoardState &b, const Move &m) {
    return (diagnoseMove(b, m).verdict == Verdict::Legal);
}

// Converts a verdict enum to human-readable string description
std::string describe(Verdict v);

} // namespace Diagnose

#endif