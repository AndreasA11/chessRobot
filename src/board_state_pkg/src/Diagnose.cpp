#include "Diagnose.hpp"

#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using Diagnose::Verdict;
using Diagnose::Diagnosis;

Diagnosis Ok() {
    return {std::vector<Square>{}, Verdict::Legal};
}

Diagnosis Fail(Verdict v, std::vector<Square> sq = {}) {
    return {std::move(sq), v};
}

int Sign(int x) {
    return (x > 0) - (x < 0);
}

Color Opposite(Color c) {
    return (c == Color::White ? Color::Black : Color::White);
}

bool EmptySquare(const BoardState &b, Square s) {
    return (b.at(s) == Piece::Empty);
}

//ATTACKS

std::optional<Square> AttackerOf(const BoardState &b, Square sq, Color by) {
    auto isPiece = [&](Square s, char type) {
        return (BoardState::inBounds(s.file, s.rank) && b.at(s) != Piece::Empty &&
                BoardState::colorOf(b.at(s)) == by && BoardState::typeOf(b.at(s)) == type);
    };

    // Pawns: a white pawn attacks upward, so it sits one rank below sq
    const int pawnRank = sq.rank + (by == Color::White ? -1 : 1);
    for(int df : {-1, 1}) {
        Square s{sq.file + df, pawnRank};
        if(isPiece(s, 'P')) {
            return s;
        }
    }

    static const int knight[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    for(int i = 0; i < 8; ++i) {
        Square s{sq.file + knight[i][0], sq.rank + knight[i][1]};
        if(isPiece(s, 'N')) {
            return s;
        }
    }

    for(int df = -1; df <= 1; ++df) {
        for(int dr = -1; dr <= 1; ++dr) {
            if(!df && !dr) {
                continue;
            }
            Square k{sq.file + df, sq.rank + dr};
            if(isPiece(k, 'K')) {
                return k;
            }

            // Sliding ray in this direction
            const bool diag = (df != 0 && dr != 0);
            Square s{sq.file + df, sq.rank + dr};
            while(BoardState::inBounds(s.file, s.rank)) {
                if(!EmptySquare(b, s)) {
                    if(isPiece(s, 'Q') || isPiece(s, diag ? 'B' : 'R')) {
                        return s;
                    }
                    break;
                }
                s = Square{s.file + df, s.rank + dr};
            }
        }
    }
    return std::nullopt;
}

std::optional<Square> FindKing(const BoardState &b, Color c) {
    for(int r = 0; r < 8; ++r) {
        for(int f = 0; f < 8; ++f) {
            Square s{f, r};
            Piece p = b.at(s);
            if(p != Piece::Empty && BoardState::colorOf(p) == c && BoardState::typeOf(p) == 'K') {
                return s;
            }
        }
    }
    return std::nullopt;
}

//PER-PIECE CHECKS

Diagnosis CheckSliding(const BoardState &b, const Move &m, bool straight, bool diagonal) {
    const int df = m.to.file - m.from.file;
    const int dr = m.to.rank - m.from.rank;
    const bool isStraight = (df == 0) != (dr == 0);
    const bool isDiag = (df != 0 && std::abs(df) == std::abs(dr));
    if(!((straight && isStraight) || (diagonal && isDiag))) {
        return Fail(Verdict::IllegalPieceGeometry);
    }

    const int sf = Sign(df);
    const int sr = Sign(dr);
    Square s{m.from.file + sf, m.from.rank + sr};
    while(!(s == m.to)) {
        if(!EmptySquare(b, s)) {
            return Fail(Verdict::Blocked, {s});
        }
        s = Square{s.file + sf, s.rank + sr};
    }
    return Ok();
}

Diagnosis CheckBishop(const BoardState &b, const Move &m) { return CheckSliding(b, m, false, true); }
Diagnosis CheckRook(const BoardState &b, const Move &m)   { return CheckSliding(b, m, true, false); }
Diagnosis CheckQueen(const BoardState &b, const Move &m)  { return CheckSliding(b, m, true, true); }

Diagnosis CheckKnight(const BoardState &, const Move &m) {
    const int df = std::abs(m.to.file - m.from.file);
    const int dr = std::abs(m.to.rank - m.from.rank);
    return (df * dr == 2) ? Ok() : Fail(Verdict::IllegalPieceGeometry);
}

Diagnosis CheckPawn(const BoardState &b, const Move &m) {
    const Color c = BoardState::colorOf(b.at(m.from));
    const int dir = (c == Color::White) ? 1 : -1;
    const int startRank = (c == Color::White) ? 1 : 6;
    const int df = m.to.file - m.from.file;
    const int dr = m.to.rank - m.from.rank;

    if(df == 0) {
        const bool single = (dr == dir);
        const bool dbl = (dr == 2 * dir && m.from.rank == startRank);
        if(!single && !dbl) {
            return Fail(Verdict::IllegalPieceGeometry);
        }
        Square mid{m.from.file, m.from.rank + dir};
        if(!EmptySquare(b, mid)) {
            return Fail(Verdict::Blocked, {mid});
        }
        if(!EmptySquare(b, m.to)) {
            return Fail(Verdict::Blocked, {m.to});
        }
        return Ok();
    }

    if(std::abs(df) == 1 && dr == dir) {
        if(!EmptySquare(b, m.to)) {
            return Ok();
        }
        std::optional<Square> ep = b.enPassantSquare();
        if(ep && *ep == m.to) {
            Square victim{m.to.file, m.from.rank};
            if(!EmptySquare(b, victim) && BoardState::colorOf(b.at(victim)) != c &&
               BoardState::typeOf(b.at(victim)) == 'P') {
                return Ok();
            }
        }
    }
    return Fail(Verdict::IllegalPieceGeometry);
}

Diagnosis CheckCastle(const BoardState &b, const Move &m) {
    const Color c = BoardState::colorOf(b.at(m.from));
    const int rank = (c == Color::White) ? 0 : 7;
    if(m.from.file != 4 || m.from.rank != rank || m.to.rank != rank) {
        return Fail(Verdict::IllegalPieceGeometry);
    }

    const bool ks = (m.to.file > m.from.file);
    if(!b.canCastle(c, ks ? CastleSide::KingSide : CastleSide::QueenSide)) {
        return Fail(Verdict::CastlingNotAllowed);
    }

    Square rook{ks ? 7 : 0, rank};
    if(b.at(rook) == Piece::Empty || BoardState::colorOf(b.at(rook)) != c ||
       BoardState::typeOf(b.at(rook)) != 'R') {
        return Fail(Verdict::CastlingNotAllowed, {rook});
    }

    for(int f = (ks ? 5 : 1); f <= (ks ? 6 : 3); ++f) {
        Square s{f, rank};
        if(!EmptySquare(b, s)) {
            return Fail(Verdict::Blocked, {s});
        }
    }

    // King may not start in, pass through, or land on an attacked square
    for(int f : {4, ks ? 5 : 3, ks ? 6 : 2}) {
        if(std::optional<Square> a = AttackerOf(b, Square{f, rank}, Opposite(c))) {
            return Fail(Verdict::CastlingNotAllowed, {Square{f, rank}, *a});
        }
    }
    return Ok();
}

Diagnosis CheckKing(const BoardState &b, const Move &m) {
    const int df = std::abs(m.to.file - m.from.file);
    const int dr = std::abs(m.to.rank - m.from.rank);
    if(df <= 1 && dr <= 1) {
        return Ok();
    }
    if(df == 2 && dr == 0) {
        return CheckCastle(b, m);
    }
    return Fail(Verdict::IllegalPieceGeometry);
}

} // namespace

//DIAGNOSE IMPLEMENTATION

namespace Diagnose {

Diagnosis diagnoseMove(const BoardState &b, const Move &m) {
    if(m.from == m.to) {
        return Fail(Verdict::IllegalPieceGeometry);
    }

    const Piece p = b.at(m.from);
    if(p == Piece::Empty) {
        return Fail(Verdict::NoPieceAtSource, {m.from});
    }
    const Color mover = BoardState::colorOf(p);
    if(mover != b.sideToMove()) {
        return Fail(Verdict::WrongTurn, {m.from});
    }

    const char type = BoardState::typeOf(p);
    const bool pawnPush = (type == 'P' && m.from.file == m.to.file);
    if(!pawnPush && !EmptySquare(b, m.to) && BoardState::colorOf(b.at(m.to)) == mover) {
        return Fail(Verdict::CapturesOwnPiece, {m.to});
    }

    Diagnosis d;
    switch(type) {
        case 'P': d = CheckPawn(b, m);   break;
        case 'N': d = CheckKnight(b, m); break;
        case 'B': d = CheckBishop(b, m); break;
        case 'R': d = CheckRook(b, m);   break;
        case 'Q': d = CheckQueen(b, m);  break;
        case 'K': d = CheckKing(b, m);   break;
        default:  return Fail(Verdict::IllegalPieceGeometry);
    }
    if(d.verdict != Verdict::Legal) {
        return d;
    }

    const int lastRank = (mover == Color::White) ? 7 : 0;
    const bool promotes = (type == 'P' && m.to.rank == lastRank);
    if(promotes && m.promotion == Piece::Empty) {
        return Fail(Verdict::AmbiguousPromotion, {m.to});
    }
    if(!promotes && m.promotion != Piece::Empty) {
        return Fail(Verdict::IllegalPieceGeometry);
    }

    BoardState after = b;
    after.applyMove(m);
    if(std::optional<Square> k = FindKing(after, mover)) {
        if(std::optional<Square> a = AttackerOf(after, *k, Opposite(mover))) {
            return Fail(Verdict::LeavesKingInCheck, {*k, *a});
        }
    }

    return Ok();
}

std::string describe(Verdict v) {
    switch(v) {
        case Verdict::Legal:                return "Legal move";
        case Verdict::NoPieceAtSource:      return "There is no piece on that square";
        case Verdict::WrongTurn:            return "That piece belongs to the other side";
        case Verdict::CapturesOwnPiece:     return "Cannot capture your own piece";
        case Verdict::IllegalPieceGeometry: return "That piece cannot move that way";
        case Verdict::Blocked:              return "Another piece is in the way";
        case Verdict::LeavesKingInCheck:    return "That would leave your king in check";
        case Verdict::CastlingNotAllowed:   return "Castling is not allowed here";
        case Verdict::AmbiguousPromotion:   return "Pawn promotion needs a piece choice";
    }
    return "Unknown";
}

} // namespace Diagnose
