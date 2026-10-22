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

    // "No move" is the baseline hypothesis with score 0.
    if(best->score <= 0.0) {
        out.kind = Decoding::Kind::NoChange;
    } else if(best->score < cfg_.minScore) {
        out.kind = Decoding::Kind::NoLegalExplanation;
    } else if(out.margin < cfg_.margin) {
        out.kind = Decoding::Kind::Ambiguous;
    } else {
        out.kind = Decoding::Kind::Confident;
    }
    return out;
}