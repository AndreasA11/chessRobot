#include "BoardState.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

//BOARDSTATE IMPLEMENTATION

BoardState::BoardState() {
    for(int rank = 0; rank < 8; ++rank) {
        for(int file = 0; file < 8; ++file) {
            board_[rank][file] = Piece::Empty;
        }
    }
}

std::string BoardState::squareToString(Square s) {
    std::string out;
    out += static_cast<char>('a' + s.file);
    out += static_cast<char>('1' + s.rank);
    return out;
}

MoveInfo BoardState::describeMove(const Move &m) const {
    MoveInfo info{};
    info.moved = at(m.from);
    if(isEmpty(info.moved)) {
        throw std::invalid_argument("describeMove: no piece on " + squareToString(m.from));
    }

    const Color mover = colorOf(info.moved);
    const char type = typeOf(info.moved);

    info.captured = at(m.to);
    info.captureSquare = m.to;
    info.promotion = colorize(m.promotion, mover);

    // En passant: pawn moves diagonally onto the empty en passant square
    if((type == 'P' && m.from.file != m.to.file) &&
       (isEmpty(info.captured) && enPassant_.has_value()) &&
       (*enPassant_ == m.to)) {
        info.captureSquare = {m.to.file, m.from.rank};
        info.captured = at(info.captureSquare);
    }

    // Castling: king moves two files
    if(type == 'K' && std::abs(m.to.file - m.from.file) == 2) {
        info.isCastle = true;
        const bool kingside = (m.to.file > m.from.file);
        info.rookFrom = {kingside ? 7 : 0, m.from.rank};
        info.rookTo = {kingside ? 5 : 3, m.from.rank};
    }

    return info;
}

void BoardState::applyMove(const Move &m) {
    const MoveInfo info = describeMove(m);
    const Color mover = colorOf(info.moved);
    const char type = typeOf(info.moved);

    // 1. Remove captured piece (may not be on `to` for en passant)
    if(!isEmpty(info.captured)) {
        board_[info.captureSquare.rank][info.captureSquare.file] = Piece::Empty;
    }

    // 2. Move the piece (with promotion if any)
    board_[m.from.rank][m.from.file] = Piece::Empty;
    board_[m.to.rank][m.to.file] = isEmpty(info.promotion) ? info.moved : info.promotion;

    // 3. Move the rook for castling
    if(info.isCastle) {
        board_[info.rookTo.rank][info.rookTo.file] = board_[info.rookFrom.rank][info.rookFrom.file];
        board_[info.rookFrom.rank][info.rookFrom.file] = Piece::Empty;
    }

    // 4. Castling rights
    if(type == 'K') {
        if(mover == Color::White) {
            castleWK_ = false;
            castleWQ_ = false;
        } else {
            castleBK_ = false;
            castleBQ_ = false;
        }
    }

    // Any move from or to a rook's corner (rook moved or rook captured)
    for(const Square &s : {m.from, m.to}) {
        if(s.rank == 0 && s.file == 0) { castleWQ_ = false; }
        if(s.rank == 0 && s.file == 7) { castleWK_ = false; }
        if(s.rank == 7 && s.file == 0) { castleBQ_ = false; }
        if(s.rank == 7 && s.file == 7) { castleBK_ = false; }
    }

    // 5. En passant target (set after any double pawn push, as FEN convention)
    enPassant_.reset();
    if(type == 'P' && std::abs(m.to.rank - m.from.rank) == 2) {
        enPassant_ = Square{m.from.file, (m.from.rank + m.to.rank) / 2};
    }

    // 6. Clocks and side
    if(type == 'P' || !isEmpty(info.captured)) {
        halfmoveClock_ = 0;
    } else {
        ++halfmoveClock_;
    }

    if(mover == Color::Black) {
        ++fullmoveNumber_;
    }

    sideToMove_ = (mover == Color::White) ? Color::Black : Color::White;
}

//FEN IMPLEMENTATION

namespace FEN {

BoardState fromFEN(const std::string &fen) {
    std::istringstream iss(fen);
    std::string placement;
    std::string side;
    std::string castling;
    std::string ep;
    int half = 0;
    int full = 1;

    if(!(iss >> placement >> side >> castling >> ep >> half >> full)) {
        throw std::invalid_argument("fromFEN: expected at least 4 fields");
    }

    if(half < 0) {
        throw std::invalid_argument("fromFEN: invalid halfmove clock");
    }

    if(full < 1) {
        throw std::invalid_argument("fromFEN: invalid fullmove counter");
    }

    // Clocks are optional in some FENs; keep defaults if missing
    if(iss >> half) {
        iss >> full;
    }

    BoardState st{};

    // Piece placement: first rank in the string is rank 8 (index 7)
    int rank = 7;
    int file = 0;
    for(char c : placement) {
        if(c == '/') {
            if(file != 8) {
                throw std::invalid_argument("fromFEN: bad rank length");
            }
            --rank;
            file = 0;
            if(rank < 0) {
                throw std::invalid_argument("fromFEN: too many ranks");
            }
        } else if(c >= '1' && c <= '8') {
            file += c - '0';
            if(file > 8) {
                throw std::invalid_argument("fromFEN: rank overflow");
            }
        } else if(BoardState::isValidPieceChar(c)) {
            if(file >= 8) {
                throw std::invalid_argument("fromFEN: rank overflow");
            }
            st.board_[rank][file++] = static_cast<Piece>(c);
        } else {
            throw std::invalid_argument(std::string("fromFEN: bad character '") + c + "'");
        }
    }

    if(rank != 0 || file != 8) {
        throw std::invalid_argument("fromFEN: incomplete placement");
    }

    // Side to move
    if(side == "w") {
        st.sideToMove_ = Color::White;
    } else if(side == "b") {
        st.sideToMove_ = Color::Black;
    } else {
        throw std::invalid_argument("fromFEN: bad side to move");
    }

    // Castling rights
    st.castleWK_ = false;
    st.castleWQ_ = false;
    st.castleBK_ = false;
    st.castleBQ_ = false;

    if(castling != "-") {
        for(char c : castling) {
            switch(c) {
                case 'K': st.castleWK_ = true; break;
                case 'Q': st.castleWQ_ = true; break;
                case 'k': st.castleBK_ = true; break;
                case 'q': st.castleBQ_ = true; break;
                default: throw std::invalid_argument("fromFEN: bad castling field");
            }
        }
    }

    // En passant
    if(ep != "-") {
        if(ep.size() != 2 || ep[0] < 'a' ||
           ep[0] > 'h' || (ep[1] != '3' && ep[1] != '6')) {
            throw std::invalid_argument("fromFEN: bad en passant square");
        }
        st.enPassant_ = Square{ep[0] - 'a', ep[1] - '1'};
    }

    st.halfmoveClock_ = half;
    st.fullmoveNumber_ = full;
    return st;
}

std::string toFEN(const BoardState &st) {
    std::string out;

    for(int rank = 7; rank >= 0; --rank) {
        int empties = 0;
        for(int file = 0; file < 8; ++file) {
            Piece p = st.board_[rank][file];
            if(BoardState::isEmpty(p)) {
                ++empties;
            } else {
                if(empties) {
                    out += static_cast<char>('0' + empties);
                    empties = 0;
                }
                out += BoardState::ch(p);
            }
        }
        if(empties) {
            out += static_cast<char>('0' + empties);
        }
        if(rank > 0) {
            out += '/';
        }
    }

    out += (st.sideToMove_ == Color::White) ? " w " : " b ";

    std::string castling;
    if(st.castleWK_) { castling += 'K'; }
    if(st.castleWQ_) { castling += 'Q'; }
    if(st.castleBK_) { castling += 'k'; }
    if(st.castleBQ_) { castling += 'q'; }
    out += castling.empty() ? "-" : castling;

    out += ' ';
    out += st.enPassant_ ? BoardState::squareToString(*st.enPassant_) : "-";

    out += ' ' + std::to_string(st.halfmoveClock_) + ' ' + std::to_string(st.fullmoveNumber_);
    return out;
}

} // namespace FEN

//UCI IMPLEMENTATION

namespace UCI {

Move parseUCI(const std::string &s) {
    if(s.size() != 4 && s.size() != 5) {
        throw std::invalid_argument("parseUCI: expected 4 or 5 characters, got '" + s + "'");
    }

    Move m{};
    m.from = {s[0] - 'a', s[1] - '1'};
    m.to = {s[2] - 'a', s[3] - '1'};
    if(!BoardState::inBounds(m.from.file, m.from.rank) || !BoardState::inBounds(m.to.file, m.to.rank)) {
        throw std::invalid_argument("parseUCI: square out of range in '" + s + "'");
    }

    if(s.size() == 5) {
        char p = static_cast<char>(std::tolower(static_cast<unsigned char>(s[4])));
        if(p != 'q' && p != 'r' && p != 'b' && p != 'n') {
            throw std::invalid_argument("parseUCI: bad promotion piece in '" + s + "'");
        }
        m.promotion = static_cast<Piece>(p);
    }
    return m;
}

} // namespace UCI