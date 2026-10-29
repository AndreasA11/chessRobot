#ifndef H_BOARDSTATENODE
#define H_BOARDSTATENODE

#include "Arbiter.hpp"
#include "BoardState.hpp"
#include "Decode.hpp"
#include "SoftObs.hpp"

#include <board_observation_interface/msg/board_observation.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <optional>
#include <string>

// Owns the game state and acts as the single source of truth for the board position.
//
// Topics
//   sub  engine_move        (String, UCI)   move played by the engine
//   sub  human_move         (String, UCI)   typed human move (manual / testing path)
//   sub  board_observation  (BoardObservation) per-square evidence from perception
//   sub  human_answer       (String)        answer to a pending question (see below)
//   pub  board_fen          (String)        authoritative position, transient_local
//   pub  board_event        (String)        "<kind>: <message>" for UI / terminal
//
// Event kinds: commit, ask_promotion, ask_which_move, report_illegal,
//              ask_check_board, request_clear_view
//
// Answers to pending questions
//   ask_promotion    -> "q" | "r" | "b" | "n"
//   ask_which_move   -> one of the two offered UCI moves, e.g. "e2e4"
//   report_illegal / ask_check_board -> "ok" (board fixed, resume observing)
class BoardStateNode : public rclcpp::Node {
public:
    explicit BoardStateNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

    //PUBLIC FUNCTIONS

private:
    //PRIVATE FUNCTIONS
    void onEngineMove(const std_msgs::msg::String::SharedPtr msg);
    void onHumanMove(const std_msgs::msg::String::SharedPtr msg);
    void onObservation(const board_observation_interface::msg::BoardObservation::SharedPtr msg);
    void onHumanAnswer(const std_msgs::msg::String::SharedPtr msg);

    // Validates turn + piece ownership, then applies the move to board_
    std::optional<MoveInfo> tryApplyMove(const std::string &moveStr,
                                         Color expectedSide,
                                         const char *source);
    void readStockfishOutput(const std_msgs::msg::String::SharedPtr msg);
    void publishFEN();

    // Acts on the Arbiter's decision for the latest frame
    void handleAction(const Action &a);
    void publishEvent(const std::string &kind, const std::string &message);

    // Called after every change to board_: resets perception state and publishes the FEN
    void afterBoardChanged();

    //PRIVATE VARIABLES
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr engineMoveSub_; // Engine move subscriber
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr humanMoveSub_; // Human move subscriber
    rclcpp::Subscription<board_observation_interface::msg::BoardObservation>::SharedPtr observationSub_; // Perception frames
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr answerSub_; // Human answers
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr boardFenPub_; // Board FEN publisher
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr eventPub_; // Prompts and events
    BoardState board_; // Current board state representation
    Color engineColor_ = Color::Black; // Color assigned to engine

    MoveDecoder decoder_;            // Which legal move do the frames show?
    Arbiter arbiter_;                // Commit, wait, or ask?
    std::optional<Action> pending_;  // Question awaiting a human answer (frames are ignored meanwhile)
};

#endif