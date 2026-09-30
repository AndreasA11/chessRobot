#ifndef H_boardState
#define H_boardState


#include "boardTypes.hpp"
#include <cstdint>
#include <array>
#include <string>
#include <optional>


class BoardState;

namespace FEN {
    BoardState fromFEN(const std::string& fen);
    std::string toFEN(const BoardState&);
}

namespace UCI {
    Move parseUCI(const std::string& fen);
}

class BoardState {
    public:
        BoardState(); //CTOR

        // Read-only access to a square
        Piece at(Square s) const { return board_[s.rank][s.file]; }

        // Get information about a move before applying it
        Color sideToMove() const { return sideToMove_; }

        // Get information about a move before applying it
        // call BEFORE applyMove
        MoveInfo describeMove(const Move& m) const; 

        // Apply a legal move to the board
        // assumes the move is legal
        void applyMove(const Move& m); 

    private:
        Piece board_[8][8]; //board[rank][file]
        Color sideToMove_ = Color::White;
        bool castleWK_ = true, castleWQ_ = true, castleBK_ = true, castleBQ_ = true;
        std::optional<Square> enPassant_;
        int halfmoveClock_ = 0;
        int fullmoveNumber_ = 1;
        static std::string squareToString(Square s);

        friend BoardState FEN::fromFEN(const std::string&);
        friend std::string FEN::toFEN(const BoardState&);
};




#endif




