// ROS-level tests of the perception path through BoardStateNode:
//   BoardObservation frames -> decoder -> arbiter -> board_fen / board_event
// Provides its own main() because it calls rclcpp::init().

#include "BoardStateNode.hpp"
#include "test_helpers.hpp"

#include <board_observation_interface/msg/board_observation.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>




using board_observation_interface::msg::BoardObservation;
using std_msgs::msg::String;
using namespace testutil;
using namespace std::chrono_literals;

namespace {

const std::string kAfterE4 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";

// FEN the node should publish after `uci` is played from `startFen`
std::string fenAfter(const std::string &startFen, const std::string &uci) {
    BoardState b = FEN::fromFEN(startFen);
    b.applyMove(UCI::parseUCI(uci));
    return FEN::toFEN(b);
}

class Harness {
public:
    explicit Harness(const std::string &engineColor = "black") {
        rclcpp::NodeOptions opts;
        opts.parameter_overrides({rclcpp::Parameter("engine_color", engineColor)});
        node_ = std::make_shared<BoardStateNode>(opts);
        helper_ = std::make_shared<rclcpp::Node>("board_state_test_helper");

        obsPub_ = helper_->create_publisher<BoardObservation>("board_observation", 10);
        answerPub_ = helper_->create_publisher<String>("human_answer", 10);
        fenSub_ = helper_->create_subscription<String>(
            "board_fen", rclcpp::QoS(1).reliable().transient_local(),
            [this](String::SharedPtr m) { fen_ = m->data; });
        eventSub_ = helper_->create_subscription<String>(
            "board_event", 10, [this](String::SharedPtr m) { events_.push_back(m->data); });

        exec_.add_node(node_);
        exec_.add_node(helper_);

        // Wait for discovery and for the node's initial (latched) FEN
        spinUntil([&] {
            return obsPub_->get_subscription_count() > 0 &&
                   answerPub_->get_subscription_count() > 0 && !fen_.empty();
        }, 5000ms);
        spinUntil([] { return false; }, 200ms);  // let the event topic connect too
        startFen_ = fen_;
    }

    template <typename Pred>
    bool spinUntil(Pred pred, std::chrono::milliseconds timeout = 3000ms) {
        const auto end = std::chrono::steady_clock::now() + timeout;
        while(std::chrono::steady_clock::now() < end) {
            exec_.spin_some(10ms);
            if(pred()) {
                return true;
            }
            std::this_thread::sleep_for(5ms);
        }
        return pred();
    }

    void publish(const SoftObs &obs, bool usable = true) {
        BoardObservation msg;
        msg.header.stamp = helper_->now();
        std::copy(obs.p.begin(), obs.p.end(), msg.probs.begin());
        msg.usable = usable;
        msg.settle_score = 0.0f;
        obsPub_->publish(msg);
    }

    // Keep publishing `obs` (like a camera would) until pred() holds or frames run out
    template <typename Pred>
    bool streamUntil(const SoftObs &obs, Pred pred, int maxFrames = 60, bool usable = true) {
        for(int i = 0; i < maxFrames; ++i) {
            publish(obs, usable);
            if(spinUntil(pred, 50ms)) {
                return true;
            }
        }
        return false;
    }

    void answer(const std::string &text) {
        String msg;
        msg.data = text;
        answerPub_->publish(msg);
    }

    bool hasEvent(const std::string &needle) const {
        return std::any_of(events_.begin(), events_.end(), [&](const std::string &e) {
            return e.find(needle) != std::string::npos;
        });
    }

    const std::string &fen() const { return fen_; }
    const std::string &startFen() const { return startFen_; }
    const std::vector<std::string> &events() const { return events_; }

private:
    std::shared_ptr<BoardStateNode> node_;
    rclcpp::Node::SharedPtr helper_;
    rclcpp::Publisher<BoardObservation>::SharedPtr obsPub_;
    rclcpp::Publisher<String>::SharedPtr answerPub_;
    rclcpp::Subscription<String>::SharedPtr fenSub_;
    rclcpp::Subscription<String>::SharedPtr eventSub_;
    rclcpp::executors::SingleThreadedExecutor exec_;
    std::string fen_;
    std::string startFen_;
    std::vector<std::string> events_;
};

} // namespace

TEST(NodePerception, CameraMoveIsCommittedAndPublished) {
    Harness h;
    const std::string expected = fenAfter(h.startFen(), "e2e4");

    EXPECT_TRUE(h.streamUntil(obsFromFen(kAfterE4), [&] { return h.fen() == expected; }));
    EXPECT_TRUE(h.hasEvent("commit"));
}

TEST(NodePerception, ObservationsOnEnginesTurnAreIgnored) {
    Harness h("white");  // engine moves first, so it is not the human's turn

    EXPECT_FALSE(h.streamUntil(obsFromFen(kAfterE4), [&] { return h.fen() != h.startFen(); }, 15));
    EXPECT_EQ(h.fen(), h.startFen());
    EXPECT_TRUE(h.events().empty());
}

TEST(NodePerception, UnusableFramesChangeNothing) {
    Harness h;

    EXPECT_FALSE(h.streamUntil(obsFromFen(kAfterE4), [&] { return h.fen() != h.startFen(); },
                               15, /*usable=*/false));
    EXPECT_EQ(h.fen(), h.startFen());
}

TEST(NodePerception, AmbiguityPromptsThenHumanAnswerCommits) {
    Harness h;
    const std::string expected = fenAfter(h.startFen(), "e2e4");

    // The camera cannot tell e3 from e4
    SoftObs split = obsFromFen(kStartFen);
    split.set(sq("e2"), 0.98f, 0.01f, 0.01f);
    split.set(sq("e3"), 0.5f, 0.5f, 0.0f);
    split.set(sq("e4"), 0.5f, 0.5f, 0.0f);

    ASSERT_TRUE(h.streamUntil(split, [&] { return h.hasEvent("ask_which_move"); }));
    EXPECT_EQ(h.fen(), h.startFen()) << "must ask, not guess";

    h.answer("e2e4");
    EXPECT_TRUE(h.spinUntil([&] { return h.fen() == expected; }));
}

TEST(NodePerception, IllegalMoveIsReportedThenRecoversAfterOk) {
    Harness h;
    const std::string expected = fenAfter(h.startFen(), "e2e4");

    // Knight shows up on g3, which is not a legal knight move
    const SoftObs bad = obsFromFen("rnbqkbnr/pppppppp/8/8/8/6N1/PPPPPPPP/RNBQKB1R b KQkq - 1 1");
    ASSERT_TRUE(h.streamUntil(bad, [&] { return h.hasEvent("report_illegal"); }));
    EXPECT_EQ(h.fen(), h.startFen()) << "an illegal move must never be committed";

    // Human puts the knight back, says so, then plays a legal move
    h.answer("ok");
    h.spinUntil([] { return false; }, 100ms);
    EXPECT_TRUE(h.streamUntil(obsFromFen(kAfterE4), [&] { return h.fen() == expected; }));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    rclcpp::init(argc, argv);
    const int result = RUN_ALL_TESTS();
    rclcpp::shutdown();
    return result;
}