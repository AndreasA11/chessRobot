#ifndef H_ARBITER
#define H_ARBITER


#include "BoardState.hpp"
#include "Diagnose.hpp"
#include "Decode.hpp"
#include "Occupancy.hpp"

#include <string>
#include <vector>

struct ArbiterConfig {
    // Consecutive Confident verdicts for the same move before committing.
    int confirmFrames = 2;

    // Consecutive Ambiguous frames before asking the human "A or B?".
    int ambiguousFrameLimit = 5;

    // Consecutive NoLegalExplanation frames before concluding something is
    // really wrong. Until then the Arbiter assumes a perception error and keeps
    // observing; this is how patient the system is before blaming the human.
    int noLegalFrameLimit = 6;

    // Consecutive Unusable frames (hand/arm stuck in view) before asking for a clear view.
    int unusableFrameLimit = 30;
};

struct Action {
    enum class Kind {
        Wait,             // nothing to do yet; keep observing
        Commit,           // apply `move` to BoardState, then reset decoder and arbiter
        AskPromotion,     // `move` is known except the promoted piece; ask, set promotion, commit
        AskWhichMove,     // human picks between `move` and `alt`
        ReportIllegal,    // human most likely played `move`, which is illegal for `verdict`
        AskCheckBoard,    // can't explain what the camera sees; ask the human to verify/fix the board
        RequestClearView  // view has been blocked too long
    };

    Kind kind = Kind::Wait;
    Move move{};
    Move alt{};
    Diagnose::Verdict verdict = Diagnose::Verdict::Legal;  // ReportIllegal only
    std::vector<Square> squares;                            // squares that differ, for highlighting
    std::string message;                                    // text for the player
};

// Policy layer: turns the decoder's verdict into an action.
// Pure logic, no ROS and no images. Separates perception trouble (re-observe, ask)
// from human error (report a Verdict).
//
// Contract with the caller:
//  - Call step() once per frame with the decoder's latest Decoding.
//  - After acting on Commit / AskPromotion / AskWhichMove / ReportIllegal /
//    AskCheckBoard (or whenever the committed position changes), call reset().
//  - Each non-Wait action is issued once; further frames return Wait until the
//    decoder's verdict changes kind or reset() is called.
//  - Check for checkmate/stalemate before observing; the decoder cannot tell
//    "no legal moves" apart from "no legal explanation".
class Arbiter {
public:
    explicit Arbiter(ArbiterConfig cfg = {}) : cfg_(cfg) {}

    // `latestHard` is the hard-label view of the same frame (SoftObs::hardLabels()).
    Action step(const BoardState& b, const Decoding& d, const Occupancy& latestHard);

    void reset();

private:
    Action onConfident(const BoardState& b, const Decoding& d);
    Action onAmbiguous(const Decoding& d);
    Action onNoLegalExplanation(const BoardState& b, const Occupancy& hard);

    ArbiterConfig cfg_;

    bool haveLast_ = false;
    Decoding::Kind lastKind_ = Decoding::Kind::NoChange;
    bool issued_ = false;

    int streak_ = 0;      // consecutive Confident frames for streakMove_
    Move streakMove_{};
    int ambiguous_ = 0;
    int noLegal_ = 0;
    int unusable_ = 0;
    bool clearAsked_ = false;
};



#endif