#include "Infer.hpp"

#include <cstdlib>

namespace Infer {

Inference inferMove(const BoardState& b, const Occupancy& o) {
    const Occupancy before(b);
    const Cell mine = (b.sideToMove() == Color::White) ? Cell::White : Cell::Black;

    std::vector<Square> vacMine, vacTheirs, filled, flipped, changed;
    bool wrongColor = false;

    for(int rank = 0; rank < 8; ++rank) {
        for(int file = 0; file < 8; ++file) {
            const Square s{file, rank};
            const Cell was = before.cellAt(s); 
            const Cell now = o.cellAt(s);
            if(was == now) { continue; }
            changed.push_back(s);

            if (was == Cell::Empty) {                 // something arrived
                filled.push_back(s);
                if (now != mine) { wrongColor = true; }   // opponent piece appeared
            } else if (now == Cell::Empty) {          // something left
                (was == mine ? vacMine : vacTheirs).push_back(s);
            } else {                                  // color flipped
                flipped.push_back(s);
                if (now != mine) { wrongColor = true; }
            }
        }
    }
        

    auto anomaly = [&] { return Inference{Inference::Kind::Anomaly, {}, changed}; };
    auto candidate = [&](Square from, Square to) {
        Inference r{Inference::Kind::Candidate, {}, changed};
        r.move.from = from;
        r.move.to = to;
        return r;
    };

    if(changed.empty()) { return {}; }
    if(wrongColor) { return anomaly(); }

    // Normal move, capture, or lifted piece
    if (vacTheirs.empty() && vacMine.size() == 1 && flipped.empty()) {
        if (filled.size() == 1) { return candidate(vacMine[0], filled[0]); } 
        if (filled.empty()) { return {Inference::Kind::PieceLifted, {}, changed}; }
    }
    if (vacTheirs.empty() && vacMine.size() == 1 && filled.empty() && flipped.size() == 1) {
        return candidate(vacMine[0], flipped[0]);
    }
        

    // Castling: two of my pieces vacated, two squares filled
    if (vacTheirs.empty() && vacMine.size() == 2 && filled.size() == 2 && flipped.empty()) {
        for (Square k : vacMine) {
            if (BoardState::typeOf(b.at(k)) != 'K') { continue; } 
            for (Square dest : filled) {
                if (dest.rank == k.rank && std::abs(dest.file - k.file) == 2) {
                    return candidate(k, dest);
                }
            }
                
        }
        return anomaly();
    }

    // En passant: my pawn left, their pawn vanished beside it, one empty square filled
    if (vacMine.size() == 1 && vacTheirs.size() == 1 && filled.size() == 1 && flipped.empty()) {
        const Square from = vacMine[0], to = filled[0];
        if (vacTheirs[0] == Square{to.file, from.rank}) { return candidate(from, to); }
    } 

    return anomaly();   // knocked piece, two moves at once, etc.
}

}

