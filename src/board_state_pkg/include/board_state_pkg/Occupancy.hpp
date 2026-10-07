#ifndef H_OCCUPANCY
#define H_OCCUPANCY


//a lossy projection of whats on each square
#include "BoardState.hpp"
#include <array>

enum class Cell : uint8_t {
    Empty, 
    White, 
    Black
};


class Occupancy {
public:
    // Default ctor
    Occupancy() = default;

    // Ctor given board if we need to restart in middle of position
    explicit Occupancy(const BoardState& boardStateIn) {
        for (int rank = 0; rank < 8; ++rank) {
            for (int file = 0; file < 8; ++file) {

                Piece currPiece = boardStateIn.at(Square{file, rank});

                if (currPiece == Piece::Empty) {
                    cellBoard_[rank][file] = Cell::Empty;
                }
                else if (BoardState::colorOf(currPiece) == Color::Black) {
                    cellBoard_[rank][file] = Cell::Black;
                }
                else {
                    cellBoard_[rank][file] = Cell::White;
                }
            }
        }
    }

    // Getting cell piece at given square
    Cell cellAt(Square square) const {
        return cellBoard_[square.rank][square.file];
    }

    void set(Square s, Cell c) {
        cellBoard_[s.rank][s.file] = c;
    }

    bool operator==(const Occupancy& o) const {
        return cellBoard_ == o.cellBoard_;
    }

private:
    std::array<std::array<Cell, 8>, 8> cellBoard_{};
};

#endif