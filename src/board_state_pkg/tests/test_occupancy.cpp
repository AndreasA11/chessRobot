#include "Occupancy.hpp"

#include <gtest/gtest.h>

TEST(Occupancy, DefaultIsEmpty) {
    Occupancy o;

    for (int rank = 0; rank < 8; ++rank)
        for (int file = 0; file < 8; ++file)
            EXPECT_EQ(o.cellAt(Square{file, rank}), Cell::Empty);
}

TEST(Occupancy, StartPosition) {
    BoardState board;

    // White back rank
    board.set(Square{0, 0}, Piece::WR);
    board.set(Square{4, 0}, Piece::WK);
    board.set(Square{7, 0}, Piece::WR);

    // White pawns
    for (int file = 0; file < 8; ++file) {
        board.set(Square{file, 1}, Piece::WP);
    }

    // Black back rank
    board.set(Square{0, 7}, Piece::BR);
    board.set(Square{4, 7}, Piece::BK);
    board.set(Square{7, 7}, Piece::BR);

    // Black pawns
    for (int file = 0; file < 8; ++file) {
        board.set(Square{file, 6}, Piece::BP);
    }

    Occupancy occupancy{board};

    // Entire white pawn rank
    for (int file = 0; file < 8; ++file) {
        EXPECT_EQ(occupancy.cellAt(Square{file, 1}), Cell::White);
    }

    // Entire black pawn rank
    for (int file = 0; file < 8; ++file) {
        EXPECT_EQ(occupancy.cellAt(Square{file, 6}), Cell::Black);
    }

    // Middle of board should be empty
    for (int rank = 2; rank < 6; ++rank) {
        for (int file = 0; file < 8; ++file) {
            EXPECT_EQ(
                occupancy.cellAt(Square{file, rank}),
                Cell::Empty
            );
        }
    }
}

TEST(Occupancy, EqualityDetectsDifference) {
    Occupancy a, b;

    EXPECT_TRUE(a == b);

    b.set(Square{4, 3}, Cell::White);  // e4

    EXPECT_FALSE(a == b);
}