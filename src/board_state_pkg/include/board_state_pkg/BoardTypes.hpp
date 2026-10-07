#ifndef H_boardTypes
#define H_boardTypes

enum class Color {
        White, 
        Black
};

enum class Piece : char {
    Empty = ' ',
    WP = 'P', WN = 'N', WB = 'B', WR = 'R', WQ = 'Q', WK = 'K', 
    BP = 'p', BN = 'n', BB = 'b', BR = 'r', BQ = 'q', BK = 'k'
};

struct Square {
    int file, rank;

    bool operator==(const Square& other) const {
        return rank == other.rank && file == other.file;
    }
};

struct Move {
    Square from, to;
    Piece promotion = Piece::Empty;
};

struct MoveInfo {
    Piece moved;
    Piece captured = Piece::Empty;
    Square captureSquare;
    bool isCastle = false;
    Square rookFrom, rookTo;         // only valid if isCastle
    Piece promotion = Piece::Empty;

    
};


#endif