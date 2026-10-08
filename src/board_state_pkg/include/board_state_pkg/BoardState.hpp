#ifndef H_BOARDSTATE
#define H_BOARDSTATE

#include "BoardTypes.hpp"

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>

class BoardState;

namespace FEN {
    BoardState fromFEN(const std::string &fen);
    std::string toFEN(const BoardState &st);
}

namespace UCI {
    Move parseUCI(const std::string &fen);
}

// Manages the internal chessboard state and move validation/execution.
class BoardState {
public:
    BoardState(); // CTOR

    //PUBLIC FUNCTIONS
    // Read-only access to a square
    Piece at(Square s) const { return board_[s.rank][s.file]; }

    // Get current side to move
    Color sideToMove() const { return sideToMove_; }

    // Get information about a move before applying it
    MoveInfo describeMove(const Move &m) const;

    // Apply a legal move to the board
    void applyMove(const Move &m);

    // Converts a square into algebraic notation string
    static std::string squareToString(Square s);

    // Returns the color of a piece
    static Color colorOf(Piece p) { return isWhite(p) ? Color::White : Color::Black; }

    // Converts a piece enum to char
    static char ch(Piece p) { return static_cast<char>(p); }

    // Checks if a piece is empty
    static bool isEmpty(Piece p) { return (p == Piece::Empty); }

    // Checks if a piece belongs to white
    static bool isWhite(Piece p) { return (std::isupper(static_cast<unsigned char>(ch(p))) != 0); }

    // Returns the uppercase piece type character
    static char typeOf(Piece p) { return static_cast<char>(std::toupper(static_cast<unsigned char>(ch(p)))); }

    // Sets piece color tag
    static Piece colorize(Piece tag, Color c) {
        if(isEmpty(tag)) { return Piece::Empty; }
        char t = typeOf(tag);
        return static_cast<Piece>(c == Color::White ? t : std::tolower(static_cast<unsigned char>(t)));
    }

    // Validates piece character
    static bool isValidPieceChar(char c) {
        switch(c) {
            case 'P': case 'N': case 'B': case 'R': case 'Q': case 'K':
            case 'p': case 'n': case 'b': case 'r': case 'q': case 'k':
                return true;
            default:
                return false;
        }
    }

    // Validates whether square file and rank are within board bounds
    static bool inBounds(int file, int rank) {
        return (file >= 0 && file < 8 && rank >= 0 && rank < 8);
    }

    // Sets a piece at a square
    void set(Square s, Piece piece) { board_[s.rank][s.file] = piece; }

    // Checks castling availability for given color and side
    bool canCastle(Color c, CastleSide s) const {
        const bool king = (s == CastleSide::KingSide);
        if(c == Color::White) { return king ? castleWK_ : castleWQ_; }
        return king ? castleBK_ : castleBQ_;
    }

    // Returns the optional en passant square
    std::optional<Square> enPassantSquare() const { return enPassant_; }

private:
    //PRIVATE VARIABLES
    Piece board_[8][8]; // 8x8 chessboard representation
    std::optional<Square> enPassant_; // Target en passant square
    Color sideToMove_ = Color::White; // Current side to move before applyMove happens
    int halfmoveClock_ = 0; // Halfmove clock for fifty-move rule
    int fullmoveNumber_ = 1; // Fullmove number counter
    bool castleWK_ = true; // White kingside castle availability
    bool castleWQ_ = true; // White queenside castle availability
    bool castleBK_ = true; // Black kingside castle availability
    bool castleBQ_ = true; // Black queenside castle availability

    //FRIENDING
    friend BoardState FEN::fromFEN(const std::string &fen);
    friend std::string FEN::toFEN(const BoardState &st);
};

#endif
