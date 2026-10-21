#pragma once

#include <string>

#include "BoardState.hpp"
#include "Occupancy.hpp"
#include "SoftObs.hpp"

namespace testutil {

inline const std::string kStartFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// "e4" -> Square{4, 3}
inline Square sq(const std::string& name) {
    return Square{name[0] - 'a', name[1] - '1'};
}

inline Move mv(const std::string& from, const std::string& to) {
    Move m;
    m.from = sq(from);
    m.to = sq(to);
    m.promotion = Piece::Empty;
    return m;
}

inline bool sameMove(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to;
}

// ADAPT THIS ONE FUNCTION to however BoardState is built from a FEN string.
inline BoardState boardFromFen(const std::string& fen) {
    return BoardState::fromFen(fen);
}

// Hard observation of a position, wrapped as soft evidence.
inline SoftObs obsFromFen(const std::string& fen, float confidence = 0.99f) {
    const BoardState b = boardFromFen(fen);
    return SoftObs::fromHard(Occupancy(b), confidence);
}

}  // namespace testutil