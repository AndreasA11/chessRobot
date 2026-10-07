
#ifndef H_boardStateNode
#define H_boardStateNode


#include "BoardState.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include <string>


// Owns the game state. Source of truth for "what is the position right now".
//
//   engine_move  (String, "e2e4")  <- stockfishNode
//   human_move   (String, "e7e5")  <- perception / sim
//   board_fen    (String, FEN)     -> stockfishNode   (transient_local)
//
// Parameters:
//   start_fen     (string) starting position
//   engine_color  (string) "white" | "black"
class BoardStateNode :  public rclcpp::Node {
    public: 
        explicit BoardStateNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
        
    private:
        void onEngineMove(const std_msgs::msg::String::SharedPtr msg);
        void onHumanMove(const std_msgs::msg::String::SharedPtr msg);

        // Validates turn + piece ownership, then applies the move to board_.
        // Returns the MoveInfo (computed BEFORE applying) on success, nullopt if rejected.
        std::optional<MoveInfo> tryApplyMove(const std::string& moveStr,
                                            Color expectedSide,
                                            const char* source);
        void readStockfishOutput(const std_msgs::msg::String::SharedPtr msg);

        void publishFEN();
        
        BoardState board_;
        Color engineColor_ = Color::Black;

        //publishers and subscribers
        rclcpp::Subscription<std_msgs::msg::String>::SharedPtr engineMoveSub_;
        rclcpp::Subscription<std_msgs::msg::String>::SharedPtr humanMoveSub_;
        rclcpp::Publisher<std_msgs::msg::String>::SharedPtr boardFenPub_;

};

#endif