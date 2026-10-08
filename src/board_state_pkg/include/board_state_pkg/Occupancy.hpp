#ifndef H_OCCUPANCY
#define H_OCCUPANCY

#include "BoardState.hpp"

#include <array>
#include <cstdint>

enum class Cell : uint8_t {
    Empty,
    White,
    Black
};

// Represents a lossy 8x8 cell occupancy grid representing piece colors.
class Occupancy {
public:
    Occupancy() = default;

    // Constructs occupancy from an existing board state
    explicit Occupancy(const BoardState &boardStateIn) {
        for(int rank = 0; rank < 8; ++rank) {
            for(int file = 0; file < 8; ++file) {
                Piece currPiece = boardStateIn.at(Square{file, rank});
                if(currPiece == Piece::Empty) {
                    cellBoard_[rank][file] = Cell::Empty;
                } else if(BoardState::colorOf(currPiece) == Color::Black) {
                    cellBoard_[rank][file] = Cell::Black;
                } else {
                    cellBoard_[rank][file] = Cell::White;
                }
            }
        }
    }

    //PUBLIC FUNCTIONS
    // Returns the cell type at the given square
    Cell cellAt(Square square) const {
        return cellBoard_[square.rank][square.file];
    }

    // Updates the cell type at the given square
    void set(Square s, Cell c) {
        cellBoard_[s.rank][s.file] = c;
    }

    // Checks equality of two occupancy boards
    bool operator==(const Occupancy &o) const {
        return (cellBoard_ == o.cellBoard_);
    }

private:
    //PRIVATE VARIABLES
    std::array<std::array<Cell, 8>, 8> cellBoard_{}; // 8x8 board of cell states
};

#endif