#ifndef H_INFER
#define H_INFER

#include "BoardState.hpp"
#include "Occupancy.hpp"

#include <vector>




namespace Infer {

//What we think happened on the board, does not have to be a legal move, just what perception saw

struct Inference {
    enum class Kind { NoChange, PieceLifted, Candidate, Anomaly } kind = Kind::NoChange;
    Move move{};                  // valid when kind == Candidate (promotion left Empty)
    std::vector<Square> changed;  // every differing square, for highlighting
};

//set the move we think happened
Inference inferMove(const BoardState& b, const Occupancy& o);

}


#endif