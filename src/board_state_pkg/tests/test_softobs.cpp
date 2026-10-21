#include <gtest/gtest.h>

#include "SoftObs.hpp"
#include "test_helpers.hpp"

using namespace testutil;

TEST(SoftObs, DefaultConstructedIsAllZero) {
    SoftObs obs;
    for(float v : obs.p) {
        EXPECT_FLOAT_EQ(v, 0.0f);
    }
}

TEST(SoftObs, ClassIndexMapping) {
    EXPECT_EQ(SoftObs::classIndex(Cell::Empty), 0);
    EXPECT_EQ(SoftObs::classIndex(Cell::White), 1);
    EXPECT_EQ(SoftObs::classIndex(Cell::Black), 2);
}

TEST(SoftObs, SetAndProbRoundTrip) {
    SoftObs obs;
    for(const char* name : {"a1", "h8", "e4", "a8", "h1"}) {
        obs.set(sq(name), 0.2f, 0.5f, 0.3f);
        EXPECT_FLOAT_EQ(obs.prob(sq(name), Cell::Empty), 0.2f) << name;
        EXPECT_FLOAT_EQ(obs.prob(sq(name), Cell::White), 0.5f) << name;
        EXPECT_FLOAT_EQ(obs.prob(sq(name), Cell::Black), 0.3f) << name;
    }
}

TEST(SoftObs, LayoutIsRankMajorA1ToH8) {
    // Must match BoardObservation.msg: index = (rank * 8 + file) * 3 + class
    SoftObs obs;
    obs.set(sq("a1"), 0.1f, 0.2f, 0.3f);
    EXPECT_FLOAT_EQ(obs.p[0], 0.1f);
    EXPECT_FLOAT_EQ(obs.p[2], 0.3f);

    obs.set(sq("b1"), 0.4f, 0.5f, 0.6f);  // file 1, rank 0
    EXPECT_FLOAT_EQ(obs.p[3], 0.4f);

    obs.set(sq("a2"), 0.7f, 0.8f, 0.9f);  // file 0, rank 1
    EXPECT_FLOAT_EQ(obs.p[24], 0.7f);

    obs.set(sq("h8"), 0.15f, 0.25f, 0.35f);
    EXPECT_FLOAT_EQ(obs.p[189], 0.15f);
    EXPECT_FLOAT_EQ(obs.p[191], 0.35f);
}

TEST(SoftObs, SettingOneSquareDoesNotTouchNeighbors) {
    SoftObs obs;
    obs.set(sq("e4"), 0.1f, 0.2f, 0.7f);
    EXPECT_FLOAT_EQ(obs.prob(sq("d4"), Cell::Black), 0.0f);
    EXPECT_FLOAT_EQ(obs.prob(sq("f4"), Cell::Black), 0.0f);
    EXPECT_FLOAT_EQ(obs.prob(sq("e3"), Cell::Black), 0.0f);
    EXPECT_FLOAT_EQ(obs.prob(sq("e5"), Cell::Black), 0.0f);
}

TEST(SoftObs, FromHardPutsConfidenceOnTrueClass) {
    const SoftObs obs = obsFromFen(kStartFen, 0.99f);

    EXPECT_NEAR(obs.prob(sq("e2"), Cell::White), 0.99f, 1e-6);
    EXPECT_NEAR(obs.prob(sq("e7"), Cell::Black), 0.99f, 1e-6);
    EXPECT_NEAR(obs.prob(sq("e4"), Cell::Empty), 0.99f, 1e-6);
    EXPECT_NEAR(obs.prob(sq("a1"), Cell::White), 0.99f, 1e-6);
    EXPECT_NEAR(obs.prob(sq("h8"), Cell::Black), 0.99f, 1e-6);

    // The wrong classes get the leftover mass
    EXPECT_NEAR(obs.prob(sq("e2"), Cell::Empty), 0.005f, 1e-6);
    EXPECT_NEAR(obs.prob(sq("e2"), Cell::Black), 0.005f, 1e-6);
}

TEST(SoftObs, FromHardCustomConfidence) {
    const SoftObs obs = obsFromFen(kStartFen, 0.8f);
    EXPECT_NEAR(obs.prob(sq("e2"), Cell::White), 0.8f, 1e-6);
    EXPECT_NEAR(obs.prob(sq("e2"), Cell::Empty), 0.1f, 1e-6);
    EXPECT_NEAR(obs.prob(sq("e2"), Cell::Black), 0.1f, 1e-6);
}

TEST(SoftObs, FromHardRowsSumToOne) {
    const SoftObs obs = obsFromFen(kStartFen, 0.97f);
    for(int r = 0; r < 8; ++r) {
        for(int f = 0; f < 8; ++f) {
            const Square s{f, r};
            const float sum = obs.prob(s, Cell::Empty) + obs.prob(s, Cell::White) +
                              obs.prob(s, Cell::Black);
            EXPECT_NEAR(sum, 1.0f, 1e-6) << "file " << f << " rank " << r;
        }
    }
}

TEST(SoftObs, FromHardMatchesOccupancyOnEverySquare) {
    const BoardState b = boardFromFen(kStartFen);
    const Occupancy occ(b);
    const SoftObs obs = SoftObs::fromHard(occ);
    for(int r = 0; r < 8; ++r) {
        for(int f = 0; f < 8; ++f) {
            const Square s{f, r};
            EXPECT_GT(obs.prob(s, occ.cellAt(s)), 0.9f) << "file " << f << " rank " << r;
        }
    }
}