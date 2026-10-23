#include "Arbiter.hpp"

#include "Infer.hpp"

namespace {

bool sameMove(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to;
}

std::string moveText(const Move& m) {
    return BoardState::squareToString(m.from) + BoardState::squareToString(m.to);
}

}  // namespace

void Arbiter::reset() {
    haveLast_ = false;
    lastKind_ = Decoding::Kind::NoChange;
    issued_ = false;
    streak_ = 0;
    streakMove_ = Move{};
    ambiguous_ = 0;
    noLegal_ = 0;
    unusable_ = 0;
    clearAsked_ = false;
}

Action Arbiter::step(const BoardState& b, const Decoding& d, const Occupancy& latestHard) {
    using K = Decoding::Kind;

    // Unusable frames carry no evidence: they never touch the streaks or
    // counters below, they only count toward "view blocked too long".
    if(d.kind == K::Unusable) {
        ++unusable_;
        if(unusable_ >= cfg_.unusableFrameLimit && !clearAsked_) {
            clearAsked_ = true;
            Action a;
            a.kind = Action::Kind::RequestClearView;
            a.message = "The board has been blocked for a while. Please clear the camera's view.";
            return a;
        }
        return Action{};
    }
    unusable_ = 0;
    clearAsked_ = false;

    // A change in verdict kind starts a fresh situation.
    if(!haveLast_ || d.kind != lastKind_) {
        streak_ = 0;
        ambiguous_ = 0;
        noLegal_ = 0;
        issued_ = false;
    }
    haveLast_ = true;
    lastKind_ = d.kind;

    Action a;
    switch(d.kind) {
        case K::Confident:
            a = onConfident(b, d);
            break;
        case K::Ambiguous:
            a = onAmbiguous(d);
            break;
        case K::NoLegalExplanation:
            a = onNoLegalExplanation(b, latestHard);
            break;
        case K::NoChange:
        case K::Unusable:
            break;
    }

    // Each action is issued once per situation.
    if(a.kind != Action::Kind::Wait) {
        if(issued_) {
            return Action{};
        }
        issued_ = true;
    }
    return a;
}

Action Arbiter::onConfident(const BoardState& b, const Decoding& d) {
    if(streak_ > 0 && sameMove(d.best, streakMove_)) {
        ++streak_;
    } else {
        streakMove_ = d.best;
        streak_ = 1;
    }
    if(streak_ < cfg_.confirmFrames) {
        return Action{};
    }

    // Final safety net: the decoder only proposes legal moves, but BoardState is
    // authoritative, so validate before anything is committed.
    const Diagnose::Diagnosis v = Diagnose::diagnoseMove(b, d.best);

    Action a;
    a.move = d.best;
    if(d.promotionUnspecified && v.verdict == Diagnose::Verdict::AmbiguousPromotion) {
        a.kind = Action::Kind::AskPromotion;
        a.message = "Which piece should the pawn on " + BoardState::squareToString(d.best.to) +
                    " promote to?";
    } else if(!d.promotionUnspecified && v.verdict == Diagnose::Verdict::Legal) {
        a.kind = Action::Kind::Commit;
        a.message = "Move recognized: " + moveText(d.best);
    } else {
        // Decoder and validator disagree. That is a bug or a stale cache, not the
        // player's fault, so do not blame them.
        a.kind = Action::Kind::AskCheckBoard;
        a.verdict = v.verdict;
        a.message = "I could not confirm the move " + moveText(d.best) +
                    ". Please check the board.";
    }
    return a;
}

Action Arbiter::onAmbiguous(const Decoding& d) {
    ++ambiguous_;
    if(ambiguous_ < cfg_.ambiguousFrameLimit) {
        return Action{};  // keep gathering frames; more evidence may break the tie
    }

    Action a;
    a.kind = Action::Kind::AskWhichMove;
    a.move = d.best;
    a.alt = d.runnerUp;
    a.message = "Did you play " + moveText(d.best) + " or " + moveText(d.runnerUp) + "?";
    return a;
}

Action Arbiter::onNoLegalExplanation(const BoardState& b, const Occupancy& hard) {
    ++noLegal_;
    if(noLegal_ < cfg_.noLegalFrameLimit) {
        return Action{};  // assume a perception problem first and look again
    }

    // Persistent. Ask the exact-diff decoder what the hard labels look like and
    // run that through the validator: if it is a recognizable but illegal move,
    // the human is the likely cause.
    const Infer::Inference inf = Infer::inferMove(b, hard);

    Action a;
    a.squares = inf.changed;

    if(inf.kind == Infer::Inference::Kind::Candidate) {
        const Diagnose::Diagnosis v = Diagnose::diagnoseMove(b, inf.move);
        if(v.verdict != Diagnose::Verdict::Legal &&
           v.verdict != Diagnose::Verdict::AmbiguousPromotion) {
            a.kind = Action::Kind::ReportIllegal;
            a.move = inf.move;
            a.verdict = v.verdict;
            a.message = "I saw " + moveText(inf.move) + ", but that is not a legal move: " +
                        Diagnose::describe(v.verdict) +
                        ". If I misread the board, please move the piece again.";
            return a;
        }
        // The hard view looks legal but the soft evidence disagreed: unclear.
    }

    a.kind = Action::Kind::AskCheckBoard;
    a.message = "I cannot match what I see to a legal move. Please check that the pieces "
                "are placed correctly.";
    return a;
}