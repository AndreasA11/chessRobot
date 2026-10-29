#include "Decode.hpp"

#include "Diagnose.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>



void MoveDecoder::reset(const BoardState& b) {
    cands_.clear();
    frames_ = 0;

    const Cell mine = (b.sideToMove() == Color::White) ? Cell::White : Cell::Black;
    const Occupancy before(b);

    for(int i = 0; i < 64; ++i) {
        was_[i] = before.cellAt(Square{i % 8, i / 8});
        changeLR_[i] = {0.0, 0.0, 0.0};
    }

    auto addDelta = [&](Candidate& c, Square s, Cell after) {
        const Cell was = before.cellAt(s);
        if(was != after) {
            c.footprint.push_back({s, was, after});
        }
    };

    for(const Diagnose::LegalMove& lm : Diagnose::legalMoves(b)) {
        const Move& m = lm.move;
        const char type = BoardState::typeOf(b.at(m.from));

        Candidate c;
        c.move = m;
        c.promotionUnspecified = lm.promotionUnspecified;

        // Footprint: the squares whose occupancy/color changes.
        addDelta(c, m.from, Cell::Empty);
        addDelta(c, m.to, mine);  // covers plain moves and captures (color flip)

        const int fileDiff = m.to.file - m.from.file;
        if(type == 'K' && std::abs(fileDiff) == 2) {
            const bool kingSide = fileDiff > 0;
            addDelta(c, Square{kingSide ? 7 : 0, m.from.rank}, Cell::Empty);  // rook leaves
            addDelta(c, Square{kingSide ? 5 : 3, m.from.rank}, mine);         // rook arrives
        }
        if(type == 'P' && fileDiff != 0 && BoardState::isEmpty(b.at(m.to))) {
            // En passant: the captured pawn sits beside the origin.
            addDelta(c, Square{m.to.file, m.from.rank}, Cell::Empty);
        }

        cands_.push_back(std::move(c));
    }
}

void MoveDecoder::clearEvidence() {
    for(Candidate& c : cands_) {
        c.score = 0.0;
    }
    for(auto& row : changeLR_) {
        row = {0.0, 0.0, 0.0};
    }
    frames_ = 0;
}

Decoding MoveDecoder::observe(const SoftObs& obs, bool usable) {
    if(!usable) {
        Decoding d;
        d.kind = Decoding::Kind::Unusable;
        d.framesUsed = frames_;
        return d;
    }

    auto logp = [&](Square s, Cell c) {
        return std::log(std::max<double>(obs.prob(s, c), cfg_.probFloor));
    };

    // Each move is scored as a log-likelihood ratio against "no move":
    // for every square it changes, how much more does the frame support the
    // new value than the old one? Squares no candidate touches cancel out.
    for(Candidate& c : cands_) {
        for(const Delta& d : c.footprint) {
            c.score += logp(d.sq, d.after) - logp(d.sq, d.was);
        }
    }

    // Independently of any move: how strongly does each square look different
    // from the committed position?
    const Cell kCells[3] = {Cell::Empty, Cell::White, Cell::Black};
    for(int i = 0; i < 64; ++i) {
        const Square s{i % 8, i / 8};
        const double lwas = logp(s, was_[i]);
        for(Cell c : kCells) {
            if(c != was_[i]) {
                changeLR_[i][SoftObs::classIndex(c)] += logp(s, c) - lwas;
            }
        }
    }
    ++frames_;

    return evaluate();
}

Decoding MoveDecoder::evaluate() const {
    Decoding out;
    out.framesUsed = frames_;

    if(cands_.empty()) {  // no legal moves (checkmate/stalemate) or reset() not called
        out.kind = Decoding::Kind::NoLegalExplanation;
        return out;
    }

    // Total evidence that squares differ from the committed position.
    double unexplained = 0.0;
    for(int i = 0; i < 64; ++i) {
        const int wasIdx = SoftObs::classIndex(was_[i]);
        double m = -std::numeric_limits<double>::infinity();
        for(int k = 0; k < 3; ++k) {
            if(k != wasIdx) {
                m = std::max(m, changeLR_[i][k]);
            }
        }
        if(m > 0.0) {
            unexplained += m;
        }
    }
    out.unexplainedChange = unexplained;

    const double negInf = -std::numeric_limits<double>::infinity();
    const Candidate* best = nullptr;
    const Candidate* second = nullptr;
    for(const Candidate& c : cands_) {
        if(!best || c.score > best->score) {
            second = best;
            best = &c;
        } else if(!second || c.score > second->score) {
            second = &c;
        }
    }

    out.best = best->move;
    out.bestScore = best->score;
    out.promotionUnspecified = best->promotionUnspecified;
    out.runnerUpScore = second ? second->score : negInf;
    if(second) {
        out.runnerUp = second->move;
    }
    out.margin = second ? (best->score - second->score) : std::numeric_limits<double>::infinity();

    if(best->score >= cfg_.minScore) {
        // Some legal move explains a real change; is it clearly the best one?
        out.kind = (out.margin >= cfg_.margin) ? Decoding::Kind::Confident
                                               : Decoding::Kind::Ambiguous;
    } else if(unexplained >= cfg_.minScore) {
        // The board looks different from the committed position, but no legal
        // move accounts for it.
        out.kind = Decoding::Kind::NoLegalExplanation;
    } else {
        out.kind = Decoding::Kind::NoChange;
    }
    return out;
}