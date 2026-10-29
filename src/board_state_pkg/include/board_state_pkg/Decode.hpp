#pragma once

#include <array>
#include <vector>

#include "BoardState.hpp"
#include "Occupancy.hpp"
#include "SoftObs.hpp"

struct DecoderConfig {
    // Probabilities are clamped to at least this before taking logs, so one
    // confidently-wrong square can cost at most ~ln(1/floor) nats (~6.9 at 1e-3)
    // instead of dominating the whole score.
    double probFloor = 1e-3;

    // Evidence threshold (nats). A move must beat the "no move" hypothesis by this
    // much to be proposed, and unexplained change must reach it to be reported.
    // 3.0 is roughly a 20:1 likelihood ratio.
    double minScore = 3.0;

    // Best move must beat the runner-up by this many nats to be Confident.
    double margin = 3.0;
};

struct Decoding {
    enum class Kind {
        Confident,           // one move clearly explains the evidence
        Ambiguous,           // top two moves too close; ask for help / re-observe
        NoChange,            // board looks like the committed position
        NoLegalExplanation,  // the board looks different, but no legal move explains it
                             // (piece lifted or misplaced, illegal move, or a misread)
        Unusable             // frame flagged unusable (hand, motion, arm); not scored
    };

    Kind kind = Kind::NoChange;
    Move best{};
    Move runnerUp{};
    double bestScore = 0.0;
    double runnerUpScore = 0.0;
    double margin = 0.0;               // bestScore - runnerUpScore
    double unexplainedChange = 0.0;    // total evidence (nats) that squares differ from the committed position
    bool promotionUnspecified = false; // best is a promotion; piece must be chosen
    int framesUsed = 0;
};

// Decides which legal move (if any) a sequence of soft observations shows.
// Does not commit anything: BoardState stays authoritative.
class MoveDecoder {
public:
    explicit MoveDecoder(DecoderConfig cfg = {}) : cfg_(cfg) {}

    // Call whenever the committed position changes. Rebuilds the legal-move
    // cache and clears accumulated evidence.
    void reset(const BoardState& b);

    // Add one frame of evidence and return the current verdict.
    // Frames with usable == false are ignored (nothing accumulated).
    Decoding observe(const SoftObs& obs, bool usable = true);

    // Clear accumulated evidence but keep the legal-move cache
    // (e.g. after a human-help request that restarts observation).
    void clearEvidence();

    int legalMoveCount() const { return static_cast<int>(cands_.size()); }

private:
    struct Delta {
        Square sq;
        Cell was;    // value in the committed position
        Cell after;  // value if this move were played
    };

    struct Candidate {
        Move move;
        std::vector<Delta> footprint;  // only squares that change
        bool promotionUnspecified = false;
        double score = 0.0;            // accumulated log-likelihood ratio vs. "no move"
    };

    Decoding evaluate() const;

    DecoderConfig cfg_;
    std::vector<Candidate> cands_;
    int frames_ = 0;

    // Committed-position cell per square (index = rank * 8 + file), and for each
    // square the accumulated log-ratio of "this class" vs. "unchanged", per class.
    // Used to tell "nothing changed" apart from "something changed that no legal
    // move explains".
    std::array<Cell, 64> was_{};
    std::array<std::array<double, 3>, 64> changeLR_{};
};