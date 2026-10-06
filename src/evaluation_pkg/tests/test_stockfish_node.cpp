// Tests for StockfishNode. The engine is a scripted fake (python3) that logs every command
// it receives and answers deterministically, so behaviour can be checked exactly.
// One test uses the real Stockfish if installed.
//
// Works against real rclcpp (run under ament_add_gtest) or the in-process stub in stub/.
#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <sys/wait.h>

#include "StockfishNode.hpp"
#include "test_helpers.hpp"

using namespace std::chrono_literals;
using testutil::TempDir;

namespace {

// Fake engine behaviour (decided from the last `position` line it saw):
//   FEN contains "NONE"  -> bestmove (none)
//   FEN contains " b "   -> bestmove e7e5
//   otherwise            -> bestmove e2e4
// Every received command is appended to the log file, one per line.
std::string fakeEngineSource(const std::string& logPath, double startupDelaySec, double goDelaySec) {
    std::string src = R"PY(#!/usr/bin/env python3
import sys, time
LOG, STARTUP, GO = "%LOG%", %STARTUP%, %GO%
log = open(LOG, "a", buffering=1)
fen = ""
for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    log.write(line + "\n")
    cmd = line.split()[0]
    if cmd == "uci":
        time.sleep(STARTUP)
        print("id name FakeFish\nid author test\nuciok", flush=True)
    elif cmd == "isready":
        print("readyok", flush=True)
    elif cmd == "position":
        fen = line
    elif cmd == "go":
        time.sleep(GO)
        if "NONE" in fen:
            print("bestmove (none)", flush=True)
        elif " b " in fen:
            print("bestmove e7e5", flush=True)
        else:
            print("bestmove e2e4", flush=True)
    elif cmd == "quit":
        break
)PY";
    auto replace = [&](const std::string& key, const std::string& val) {
        src.replace(src.find(key), key.size(), val);
    };
    replace("%LOG%", logPath);
    replace("%STARTUP%", std::to_string(startupDelaySec));
    replace("%GO%", std::to_string(goDelaySec));
    return src;
}

struct Config {
    std::string enginePath;
    int threads = 1, hashMb = 16, skillLevel = 20, moveTimeMs = 100;
};

const char* kWhiteFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const char* kBlackFen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";

}  // namespace

class StockfishNodeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { if (!rclcpp::ok()) rclcpp::init(0, nullptr); }
    static void TearDownTestSuite() { rclcpp::shutdown(); }

    void TearDown() override { stopNode(); }

    // Builds a node whose engine is the fake script. Returns the log file path.
    std::string startFakeNode(double startupDelaySec = 0.0, double goDelaySec = 0.05, Config cfg = {}) {
        logPath_ = dir_.path() + "/engine.log";
        cfg.enginePath = dir_.writeScript("fake_engine.py", fakeEngineSource(logPath_, startupDelaySec, goDelaySec));
        startNode(cfg);
        return logPath_;
    }

    void startNode(const Config& cfg) {
        rclcpp::NodeOptions opts;
        opts.parameter_overrides({
            rclcpp::Parameter("engine_path", cfg.enginePath),
            rclcpp::Parameter("threads", cfg.threads),
            rclcpp::Parameter("hash_mb", cfg.hashMb),
            rclcpp::Parameter("skill_level", cfg.skillLevel),
            rclcpp::Parameter("move_time_ms", cfg.moveTimeMs),
        });
        node_ = std::make_shared<StockfishNode>(opts);

        helper_ = std::make_shared<rclcpp::Node>("stockfish_node_test_helper");
        fenPub_ = helper_->create_publisher<std_msgs::msg::String>("board_fen", 10);
        moveSub_ = helper_->create_subscription<std_msgs::msg::String>(
            "stockfish_move", 10, [this](const std_msgs::msg::String& m) { moves_.push_back(m.data); });

        exec_.add_node(node_);
        exec_.add_node(helper_);

        // Publishers and subscribers find each other asynchronously in real ROS; a message
        // published before they are matched is silently lost.
        ASSERT_TRUE(spinUntil([&] {
            return fenPub_->get_subscription_count() >= 1 && moveSub_->get_publisher_count() >= 1;
        }, 5000ms)) << "helper never connected to the node's topics";
    }

    void stopNode() {
        if (node_) exec_.remove_node(node_);
        node_.reset();
        moveSub_.reset();
        fenPub_.reset();
        helper_.reset();
    }

    void publishFen(const std::string& fen) {
        std_msgs::msg::String m;
        m.data = fen;
        fenPub_->publish(m);
    }

    // Runs the executor on this thread until `pred` is true or `timeout` passes.
    bool spinUntil(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            exec_.spin_some();
            if (pred()) return true;
            std::this_thread::sleep_for(5ms);
        }
        exec_.spin_some();
        return pred();
    }
    void spinFor(std::chrono::milliseconds d) { spinUntil([] { return false; }, d); }
    bool waitForMoves(size_t n, std::chrono::milliseconds timeout = 5000ms) {
        return spinUntil([&] { return moves_.size() >= n; }, timeout);
    }
    bool logContains(const std::string& line) const {
        const auto lines = testutil::readLines(logPath_);
        return std::find(lines.begin(), lines.end(), line) != lines.end();
    }
    bool waitForLogLine(const std::string& line, std::chrono::milliseconds timeout = 5000ms) {
        return spinUntil([&] { return logContains(line); }, timeout);
    }

    TempDir dir_;
    std::string logPath_;
    std::shared_ptr<StockfishNode> node_;
    std::shared_ptr<rclcpp::Node> helper_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr fenPub_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr moveSub_;
    rclcpp::executors::SingleThreadedExecutor exec_;
    std::vector<std::string> moves_;   // only touched from the thread that spins the executor
};

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------
TEST_F(StockfishNodeTest, ConstructorThrowsWhenEngineCannotBeLaunched) {
    rclcpp::NodeOptions opts;
    opts.parameter_overrides({rclcpp::Parameter("engine_path", "/nonexistent/engine")});
    EXPECT_THROW(StockfishNode node(opts), std::runtime_error);
}

TEST_F(StockfishNodeTest, HandshakeSendsUciThenOptionsThenIsReady) {
    Config cfg; cfg.threads = 4; cfg.hashMb = 32; cfg.skillLevel = 5;
    startFakeNode(0.0, 0.05, cfg);
    ASSERT_TRUE(waitForLogLine("isready"));

    const auto lines = testutil::readLines(logPath_);
    const std::vector<std::string> expected = {
        "uci",
        "setoption name Threads value 4",
        "setoption name Hash value 32",
        "setoption name Skill Level value 5",
        "ucinewgame",
        "isready",
    };
    ASSERT_GE(lines.size(), expected.size());
    EXPECT_EQ(std::vector<std::string>(lines.begin(), lines.begin() + expected.size()), expected);
}

TEST_F(StockfishNodeTest, FenReceivedBeforeHandshakeFinishesIsSearchedAfterwards) {
    startFakeNode(/*startupDelay=*/1.0);   // engine takes 1 s to answer `uci`
    publishFen(kWhiteFen);                 // arrives while the node is still waiting for uciok

    spinFor(200ms);
    EXPECT_FALSE(logContains(std::string("position fen ") + kWhiteFen)) << "search must wait for readyok";
    EXPECT_TRUE(moves_.empty());

    ASSERT_TRUE(waitForMoves(1, 5000ms));
    EXPECT_EQ(moves_[0], "e2e4");
}

// ---------------------------------------------------------------------------
// Searching
// ---------------------------------------------------------------------------
TEST_F(StockfishNodeTest, PublishesTheEnginesMoveForAFen) {
    startFakeNode();
    ASSERT_TRUE(waitForLogLine("isready"));
    publishFen(kWhiteFen);
    ASSERT_TRUE(waitForMoves(1));
    EXPECT_EQ(moves_[0], "e2e4");
}

TEST_F(StockfishNodeTest, SendsPositionThenGoWithConfiguredMoveTime) {
    Config cfg; cfg.moveTimeMs = 250;
    startFakeNode(0.0, 0.05, cfg);
    ASSERT_TRUE(waitForLogLine("isready"));
    publishFen(kBlackFen);
    ASSERT_TRUE(waitForMoves(1));
    EXPECT_EQ(moves_[0], "e7e5");   // side to move was black

    const auto lines = testutil::readLines(logPath_);
    const std::string pos = std::string("position fen ") + kBlackFen;
    const auto it = std::find(lines.begin(), lines.end(), pos);
    ASSERT_NE(it, lines.end());
    ASSERT_NE(it + 1, lines.end());
    EXPECT_EQ(*(it + 1), "go movetime 250");
}

TEST_F(StockfishNodeTest, AnswersConsecutiveFensInOrder) {
    startFakeNode();
    ASSERT_TRUE(waitForLogLine("isready"));
    publishFen(kWhiteFen);
    ASSERT_TRUE(waitForMoves(1));
    publishFen(kBlackFen);
    ASSERT_TRUE(waitForMoves(2));
    EXPECT_EQ(moves_, (std::vector<std::string>{"e2e4", "e7e5"}));
}

TEST_F(StockfishNodeTest, NewFenDuringSearchStopsItAndOnlyTheNewAnswerIsPublished) {
    startFakeNode(0.0, /*goDelay=*/0.4);
    ASSERT_TRUE(waitForLogLine("isready"));

    publishFen(kWhiteFen);                                   // would answer e2e4
    ASSERT_TRUE(waitForLogLine("go movetime 100"));          // search for it is running
    publishFen(kBlackFen);                                   // would answer e7e5

    ASSERT_TRUE(waitForMoves(1, 3000ms));
    EXPECT_EQ(moves_[0], "e7e5");
    spinFor(800ms);                                          // make sure the stale e2e4 never shows up
    EXPECT_EQ(moves_, (std::vector<std::string>{"e7e5"}));
    EXPECT_TRUE(logContains("stop"));
}

TEST_F(StockfishNodeTest, OnlyTheNewestOfSeveralQueuedFensIsSearched) {
    startFakeNode(0.0, 0.4);
    ASSERT_TRUE(waitForLogLine("isready"));

    publishFen(kWhiteFen);
    ASSERT_TRUE(waitForLogLine("go movetime 100"));
    publishFen("8/8/8/8/8/8/8/K6k w - - 0 1");               // superseded before it is ever searched
    publishFen(kBlackFen);

    ASSERT_TRUE(waitForMoves(1, 3000ms));
    spinFor(800ms);
    EXPECT_EQ(moves_, (std::vector<std::string>{"e7e5"}));
    EXPECT_FALSE(logContains("position fen 8/8/8/8/8/8/8/K6k w - - 0 1"));
}

TEST_F(StockfishNodeTest, NoLegalMoveIsNotPublishedAndNodeStaysUsable) {
    startFakeNode();
    ASSERT_TRUE(waitForLogLine("isready"));

    publishFen("NONE w - - 0 1");                            // fake engine answers "bestmove (none)"
    ASSERT_TRUE(waitForLogLine("go movetime 100"));
    spinFor(400ms);
    EXPECT_TRUE(moves_.empty());

    publishFen(kWhiteFen);                                   // the node must have gone back to idle
    ASSERT_TRUE(waitForMoves(1));
    EXPECT_EQ(moves_[0], "e2e4");
}

// ---------------------------------------------------------------------------
// Bad input
// ---------------------------------------------------------------------------
TEST_F(StockfishNodeTest, MalformedFensAreIgnoredAndCannotInjectCommands) {
    startFakeNode();
    ASSERT_TRUE(waitForLogLine("isready"));

    publishFen("");
    publishFen("8/8/8/8/8/8/8/K6k w - - 0 1\nquit");        // would shut the engine down if forwarded
    publishFen("8/8/8/8/8/8/8/K6k w - - 0 1\r\nstop");
    spinFor(400ms);

    EXPECT_TRUE(moves_.empty());
    EXPECT_FALSE(logContains("quit"));
    EXPECT_FALSE(logContains("stop"));
    for (const auto& l : testutil::readLines(logPath_))
        EXPECT_NE(l.rfind("position", 0), 0u) << "malformed FEN reached the engine: " << l;

    publishFen(kWhiteFen);                                   // still works afterwards
    ASSERT_TRUE(waitForMoves(1));
    EXPECT_EQ(moves_[0], "e2e4");
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------
TEST_F(StockfishNodeTest, DestructorQuitsTheEngineAndReapsIt) {
    startFakeNode();
    ASSERT_TRUE(waitForLogLine("isready"));

    const auto t0 = std::chrono::steady_clock::now();
    stopNode();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_LT(secs, 2.0);

    EXPECT_TRUE(logContains("quit"));
    errno = 0;
    EXPECT_EQ(waitpid(-1, nullptr, WNOHANG), -1) << "engine process left behind (zombie or still running)";
    EXPECT_EQ(errno, ECHILD);
}

TEST_F(StockfishNodeTest, DestructorWorksWhileASearchIsRunning) {
    startFakeNode(0.0, /*goDelay=*/1.0);
    ASSERT_TRUE(waitForLogLine("isready"));
    publishFen(kWhiteFen);
    ASSERT_TRUE(waitForLogLine("go movetime 100"));

    const auto t0 = std::chrono::steady_clock::now();
    stopNode();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_LT(secs, 3.0);
    errno = 0;
    EXPECT_EQ(waitpid(-1, nullptr, WNOHANG), -1);
    EXPECT_EQ(errno, ECHILD);
}

// ---------------------------------------------------------------------------
// Real Stockfish (skipped if not installed; set STOCKFISH_PATH to point at it)
// ---------------------------------------------------------------------------
TEST_F(StockfishNodeTest, RealStockfishFindsMateInOne) {
    const std::string sf = testutil::findStockfish();
    if (sf.empty()) GTEST_SKIP() << "Stockfish not found (set STOCKFISH_PATH)";

    Config cfg; cfg.enginePath = sf; cfg.moveTimeMs = 300;
    startNode(cfg);
    spinFor(500ms);                                          // let the handshake finish
    publishFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");      // back-rank mate: Ra8#
    ASSERT_TRUE(waitForMoves(1, 8000ms));
    EXPECT_EQ(moves_[0], "a1a8");
}
