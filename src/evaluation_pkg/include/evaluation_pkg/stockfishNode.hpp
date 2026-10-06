#ifndef H_STOCKFISH_NODE
#define H_STOCKFISH_NODE

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include "EngineProcess.hpp"
#include "UCI.hpp"

// Contract with boardState (Option B):
//   - boardState publishes a FEN (std_msgs/String) on `fen_topic` ONLY when it wants
//     the engine to move. The node searches the side to move in that FEN.
//   - The node publishes the chosen move in long algebraic form ("e2e4", "e7e8q")
//     on `move_topic`. boardState validates and applies it.
class StockfishNode : public rclcpp::Node {
public:
    // `options` lets tests (or a launch file) override parameters such as engine_path.
    explicit StockfishNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
    ~StockfishNode() override;   // quit engine, join reader thread

private:
    // Startup is a small state machine instead of blocking on futures, so no ROS
    // callback ever waits on the engine.
    enum class State { WaitingUciOk, WaitingReadyOk, Idle, Searching };

    // ---- ROS side (executor thread) ---------------------------------------
    void onFen(const std_msgs::msg::String& msg);
    void sendSearch(const std::string& fen);         // position fen ... + go movetime ...
    void publishMove(const std::string& move);

    // ---- engine side (reader thread) --------------------------------------
    void readerLoop();                                   // readLine -> parseLine -> handleParsedOutput
    void handleParsedOutput(const uci::Message& msg);    // std::visit, routing only

    void handleUciOk();
    void handleReadyOk();
    void handleIdLine(const uci::IdLine& id);
    void handleOption(const uci::Option& opt);
    void handleInfo(const uci::Info& info);
    void handleBestMove(const uci::BestMove& bm);
    void handleUnknown(const uci::Unknown& u);

    // ---- parameters -------------------------------------------------------
    int threads_     = 1;
    int hashMb_      = 16;
    
    /*
    • Skill Level 0: ~1100 Elo
    • Skill Level 5: ~1600–1700 Elo
    • Skill Level 10: ~2100–2200 Elo
    • Skill Level 15: ~2600–2700 Elo
    • Skill Level 20: ~3300–3500+ Elo (Full strength)
    */
    int skillLevel_  = 20;
    int moveTimeMs_  = 1000;

    // ---- ROS --------------------------------------------------------------
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr fenSub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr    stockfishMovePub_;

    // ---- engine -----------------------------------------------------------
    std::unique_ptr<EngineProcess> engine_;
    std::thread                    reader_;
    std::atomic<bool>              running_{true};

    // Shared between executor thread and reader thread -> guard with mutex_.
    std::mutex                 mutex_;
    State                      state_ = State::WaitingUciOk;
    std::optional<std::string> pendingFen_;      // newest FEN received while the engine was busy
    bool                       staleSearch_ = false;   // running search is for an outdated FEN
    uci::Info                  latestInfo_;      // last multipv-1 info with a score
};


#endif