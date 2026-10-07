#ifndef H_boardState
#define H_boardState

#include "BoardTypes.hpp"
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

        //turns a square into a string format
        static std::string squareToString(Square s);

        static Color colorOf(Piece p) { return isWhite(p) ? Color::White : Color::Black; }

        static char ch(Piece p) { return static_cast<char>(p); }

        static bool isEmpty(Piece p) { return p == Piece::Empty; }

        static bool isWhite(Piece p) { return std::isupper(static_cast<unsigned char>(ch(p))) != 0; }

        static char typeOf(Piece p) { return static_cast<char>(std::toupper(static_cast<unsigned char>(ch(p)))); }

        static Piece colorize(Piece tag, Color c) {
            if (isEmpty(tag)) return Piece::Empty;
            char t = typeOf(tag);
            return static_cast<Piece>(c == Color::White ? t : std::tolower(static_cast<unsigned char>(t)));
        }

        static bool isValidPieceChar(char c) {
            switch (c) {
                case 'P': case 'N': case 'B': case 'R': case 'Q': case 'K':
                case 'p': case 'n': case 'b': case 'r': case 'q': case 'k':
                    return true;
                default:
                    return false;
            }
        }

        static bool inBounds(int file, int rank) { return file >= 0 && file < 8 && rank >= 0 && rank < 8; }
        
        void set(Square s, Piece piece) {board_[s.rank][s.file] = piece;}
    private:
        Piece board_[8][8]; //board[rank][file]
        Color sideToMove_ = Color::White;
        bool castleWK_ = true, castleWQ_ = true, castleBK_ = true, castleBQ_ = true;
        std::optional<Square> enPassant_;
        int halfmoveClock_ = 0;
        int fullmoveNumber_ = 1;
        

        friend BoardState FEN::fromFEN(const std::string&);
        friend std::string FEN::toFEN(const BoardState&);

        
};




#endif




