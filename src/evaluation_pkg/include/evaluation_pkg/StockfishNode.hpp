#ifndef H_STOCKFISHNODE
#define H_STOCKFISHNODE

#include "EngineProcess.hpp"
#include "UCI.hpp"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

// Connects ROS topic messaging with the Stockfish chess engine process.
class StockfishNode : public rclcpp::Node {
public:
    explicit StockfishNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
    ~StockfishNode() override;

    //PUBLIC FUNCTIONS

private:
    enum class State {
        WaitingUciOk,
        WaitingReadyOk,
        Idle,
        Searching
    };

    //PRIVATE FUNCTIONS
    void onFen(const std_msgs::msg::String &msg);
    void sendSearch(const std::string &fen);
    void publishMove(const std::string &move);

    void readerLoop();
    void handleParsedOutput(const uci::Message &msg);

    void handleUciOk();
    void handleReadyOk();
    void handleIdLine(const uci::IdLine &id);
    void handleOption(const uci::Option &opt);
    void handleInfo(const uci::Info &info);
    void handleBestMove(const uci::BestMove &bm);
    void handleUnknown(const uci::Unknown &u);

    //PRIVATE VARIABLES
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr fenSub_; // Subscriber for FEN positions
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr stockfishMovePub_; // Publisher for best moves
    std::unique_ptr<EngineProcess> engine_; // Subprocess wrapper managing Stockfish
    std::thread reader_; // Background reader thread
    std::mutex mutex_; // Mutex protecting internal engine state
    uci::Info latestInfo_{}; // Latest principal variation search information
    std::optional<std::string> pendingFen_; // Pending FEN queue
    std::atomic<bool> running_{true}; // Execution running state
    State state_ = State::WaitingUciOk; // State of the engine handshake/search
    int skillLevel_ = 20; // Engine skill level from 0 to 20
    int moveTimeMs_ = 1000; // Search time allotted per move in milliseconds
    int threads_ = 1; // Thread count for engine search
    int hashMb_ = 16; // Memory size for hash table in megabytes
    bool staleSearch_ = false; // Indicates active search is superseded
};

#endif