#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include "BoardStateNode.hpp"

using std_msgs::msg::String;
using namespace std::chrono_literals;

namespace {

const std::string kStartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Runs a BoardStateNode plus a "tester" node that plays the roles of perception (human_move),
// stockfishNode (engine_move), and records everything published on board_fen.
class BoardStateNodeTest : public ::testing::Test {
protected:
    void TearDown() override {
        exec_.reset();
        tester_.reset();
        node_.reset();
    }

    void startNode(const std::string& engineColor = "black", const std::string& startFen = "") {
        std::vector<rclcpp::Parameter> params{{"engine_color", engineColor}};
        if (!startFen.empty()) params.emplace_back("start_fen", startFen);
        rclcpp::NodeOptions options;
        options.parameter_overrides(params);

        node_ = std::make_shared<BoardStateNode>(options);
        tester_ = std::make_shared<rclcpp::Node>("board_state_tester");

        humanPub_ = tester_->create_publisher<String>("human_move", 10);
        enginePub_ = tester_->create_publisher<String>("engine_move", 10);
        fenSub_ = tester_->create_subscription<String>(
            "board_fen", rclcpp::QoS(1).reliable().transient_local(),
            [this](String::ConstSharedPtr msg) { fens_.push_back(msg->data); });

        exec_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        exec_->add_node(node_);
        exec_->add_node(tester_);

        // Wait for discovery so we don't publish into the void, and for the latched initial FEN.
        ASSERT_TRUE(spinUntil([this] {
            return humanPub_->get_subscription_count() > 0 &&
                   enginePub_->get_subscription_count() > 0 && !fens_.empty();
        })) << "node never came up / never published its initial FEN";
    }

    bool spinUntil(const std::function<bool()>& pred,
                   std::chrono::milliseconds timeout = 3000ms) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            exec_->spin_some();
            if (pred()) return true;
            std::this_thread::sleep_for(5ms);
        }
        return pred();
    }

    void spinFor(std::chrono::milliseconds d) {
        spinUntil([] { return false; }, d);
    }

    static void send(const rclcpp::Publisher<String>::SharedPtr& pub, const std::string& data) {
        String m;
        m.data = data;
        pub->publish(m);
    }

    // Publish a move and wait for the resulting FEN. Returns it, or "" on timeout.
    std::string sendAndWaitForFen(const rclcpp::Publisher<String>::SharedPtr& pub,
                                  const std::string& move) {
        const size_t before = fens_.size();
        send(pub, move);
        return spinUntil([&] { return fens_.size() > before; }) ? fens_.back() : "";
    }

    // Publish a move that should be rejected; assert no new FEN appears.
    void sendAndExpectNoFen(const rclcpp::Publisher<String>::SharedPtr& pub,
                            const std::string& move) {
        const size_t before = fens_.size();
        send(pub, move);
        spinFor(300ms);
        EXPECT_EQ(fens_.size(), before) << "move '" << move << "' should have been ignored";
    }

    std::shared_ptr<BoardStateNode> node_;
    std::shared_ptr<rclcpp::Node> tester_;
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> exec_;
    rclcpp::Publisher<String>::SharedPtr humanPub_, enginePub_;
    rclcpp::Subscription<String>::SharedPtr fenSub_;
    std::vector<std::string> fens_;
};

}  // namespace

// ---------------------------------------------------------------- startup

TEST_F(BoardStateNodeTest, PublishesStartPositionOnStartup) {
    startNode("black");
    ASSERT_FALSE(fens_.empty());
    EXPECT_EQ(fens_.front(), kStartFen);
}

TEST_F(BoardStateNodeTest, HonorsCustomStartFen) {
    const std::string fen = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
    startNode("black", fen);
    EXPECT_EQ(fens_.front(), fen);
}

TEST(BoardStateNodeConstruction, InvalidParametersThrow) {
    {
        rclcpp::NodeOptions o;
        o.parameter_overrides({{"engine_color", "purple"}});
        EXPECT_THROW(BoardStateNode{o}, std::invalid_argument);
    }
    {
        rclcpp::NodeOptions o;
        o.parameter_overrides({{"start_fen", "garbage"}});
        EXPECT_THROW(BoardStateNode{o}, std::invalid_argument);
    }
}

// ------------------------------------------------------------ happy paths

TEST_F(BoardStateNodeTest, HumanMoveUpdatesAndPublishesFen) {
    startNode("black");
    EXPECT_EQ(sendAndWaitForFen(humanPub_, "e2e4"),
              "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
}

TEST_F(BoardStateNodeTest, FullHumanEngineExchange) {
    startNode("black");
    EXPECT_EQ(sendAndWaitForFen(humanPub_, "e2e4"),
              "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    EXPECT_EQ(sendAndWaitForFen(enginePub_, "e7e5"),
              "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2");
    EXPECT_EQ(sendAndWaitForFen(humanPub_, "g1f3"),
              "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2");
}

TEST_F(BoardStateNodeTest, EngineCanPlayWhite) {
    startNode("white");
    EXPECT_EQ(sendAndWaitForFen(enginePub_, "d2d4"),
              "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 1");
    EXPECT_EQ(sendAndWaitForFen(humanPub_, "d7d5"),
              "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq d6 0 2");
}

TEST_F(BoardStateNodeTest, HumanCastlingFromCustomFen) {
    startNode("black", "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    EXPECT_EQ(sendAndWaitForFen(humanPub_, "e1g1"), "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1");
    EXPECT_EQ(sendAndWaitForFen(enginePub_, "e8c8"), "2kr3r/8/8/8/8/8/8/R4RK1 w - - 2 2");
}

// -------------------------------------------------------------- rejections

TEST_F(BoardStateNodeTest, EngineMoveOnHumanTurnIsIgnored) {
    startNode("black");  // white (human) to move
    sendAndExpectNoFen(enginePub_, "e7e5");
    // State must be unchanged: the human can still play the normal first move
    EXPECT_EQ(sendAndWaitForFen(humanPub_, "e2e4"),
              "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
}

TEST_F(BoardStateNodeTest, HumanMoveOnEngineTurnIsIgnored) {
    startNode("white");  // white (engine) to move
    sendAndExpectNoFen(humanPub_, "e7e5");
}

TEST_F(BoardStateNodeTest, MovingOpponentsPieceIsIgnored) {
    startNode("black");
    sendAndExpectNoFen(humanPub_, "e7e5");  // white's turn, black pawn
}

TEST_F(BoardStateNodeTest, MoveFromEmptySquareIsIgnored) {
    startNode("black");
    sendAndExpectNoFen(humanPub_, "e4e5");
}

TEST_F(BoardStateNodeTest, MalformedMovesDoNotCrashNode) {
    startNode("black");
    for (const char* bad : {"garbage", "", "e2", "z9z9", "e7e8x", "0000"})
        sendAndExpectNoFen(humanPub_, bad);
    // Node is still alive and responsive
    EXPECT_EQ(sendAndWaitForFen(humanPub_, "e2e4"),
              "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
}

TEST_F(BoardStateNodeTest, EngineNoneTokenMeansGameOverAndChangesNothing) {
    startNode("black");
    ASSERT_FALSE(sendAndWaitForFen(humanPub_, "e2e4").empty());
    sendAndExpectNoFen(enginePub_, "(none)");
    // Still black's (engine's) turn: a real engine move is still accepted afterwards
    EXPECT_EQ(sendAndWaitForFen(enginePub_, "e7e5"),
              "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    rclcpp::init(argc, argv);
    const int result = RUN_ALL_TESTS();
    rclcpp::shutdown();
    return result;
}
