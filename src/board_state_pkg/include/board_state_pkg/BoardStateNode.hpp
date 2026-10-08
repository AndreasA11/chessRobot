#ifndef H_BOARDSTATENODE
#define H_BOARDSTATENODE

#include "BoardState.hpp"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <optional>
#include <string>

// Owns the game state and acts as the single source of truth for the board position.
class BoardStateNode : public rclcpp::Node {
public:
    explicit BoardStateNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

    //PUBLIC FUNCTIONS

private:
    //PRIVATE FUNCTIONS
    void onEngineMove(const std_msgs::msg::String::SharedPtr msg);
    void onHumanMove(const std_msgs::msg::String::SharedPtr msg);

    // Validates turn + piece ownership, then applies the move to board_
    std::optional<MoveInfo> tryApplyMove(const std::string &moveStr,
                                         Color expectedSide,
                                         const char *source);
    void readStockfishOutput(const std_msgs::msg::String::SharedPtr msg);
    void publishFEN();

    //PRIVATE VARIABLES
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr engineMoveSub_; // Engine move subscriber
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr humanMoveSub_; // Human move subscriber
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr boardFenPub_; // Board FEN publisher
    BoardState board_; // Current board state representation
    Color engineColor_ = Color::Black; // Color assigned to engine
};

#endif