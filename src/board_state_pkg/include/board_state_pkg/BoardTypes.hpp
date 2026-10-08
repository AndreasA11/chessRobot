#ifndef H_BOARDTYPES
#define H_BOARDTYPES

enum class Color {
    White,
    Black
};

enum class CastleSide {
    KingSide,
    QueenSide
};

enum class Piece : char {
    Empty = ' ',
    WP = 'P', WN = 'N', WB = 'B', WR = 'R', WQ = 'Q', WK = 'K',
    BP = 'p', BN = 'n', BB = 'b', BR = 'r', BQ = 'q', BK = 'k'
};

struct Square {
    int file = 0;
    int rank = 0;

    bool operator==(const Square &other) const {
        return (rank == other.rank && file == other.file);
    }
};

struct Move {
    Square from{};
    Square to{};
    Piece promotion = Piece::Empty;
};

struct MoveInfo {
    Square captureSquare{};
    Square rookFrom{};
    Square rookTo{};
    Piece moved = Piece::Empty;
    Piece captured = Piece::Empty;
    Piece promotion = Piece::Empty;
    bool isCastle = false;
};

#endif