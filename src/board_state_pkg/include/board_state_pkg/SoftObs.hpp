#ifndef H_SOFTOBS
#define H_SOFTOBS

#include "BoardState.hpp"
#include "Occupancy.hpp"

#include <array>

// Per-square probabilities over {Empty, White, Black} for one camera frame.
// Layout matches BoardObservation.msg: row-major a1..h8, 3 values per square,
//   index = (rank * 8 + file) * 3 + classIndex
struct SoftObs {
    std::array<float, 192> p{};

    static int classIndex(Cell c) {
        switch(c) {
            case Cell::Empty: return 0;
            case Cell::White: return 1;
            case Cell::Black: return 2;
        }
        return 0;
    }

    float prob(Square s, Cell c) const {
        return p[(s.rank * 8 + s.file) * 3 + classIndex(c)];
    }

    void set(Square s, float pEmpty, float pWhite, float pBlack) {
        const int i = (s.rank * 8 + s.file) * 3;
        p[i] = pEmpty;
        p[i + 1] = pWhite;
        p[i + 2] = pBlack;
    }

    // Wrap hard labels as soft evidence. Useful for tests and for the noise
    // simulator (start from a true board, then corrupt individual squares).
    static SoftObs fromHard(const Occupancy& o, float confidence = 0.99f) {
        SoftObs obs;
        const float rest = (1.0f - confidence) / 2.0f;
        for(int r = 0; r < 8; ++r) {
            for(int f = 0; f < 8; ++f) {
                const Square s{f, r};
                const Cell c = o.cellAt(s);
                obs.set(s,
                        c == Cell::Empty ? confidence : rest,
                        c == Cell::White ? confidence : rest,
                        c == Cell::Black ? confidence : rest);
            }
        }
        return obs;
    }
};


#endif