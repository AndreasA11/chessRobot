
#include "boardStateNode.hpp"
#include "boardState.hpp"
#include <iostream>
#include <cctype>
#include <functional>
#include <stdexcept>

namespace {

Color otherColor(Color c) { return c == Color::White ? Color::Black : Color::White; }

Color colorOfPiece(Piece p) {
    return std::isupper(static_cast<unsigned char>(static_cast<char>(p))) ? Color::White
                                                                          : Color::Black;
}

const char* colorName(Color c) { return c == Color::White ? "white" : "black"; }

}  // namespace

BoardStateNode::BoardStateNode() : rclcpp::Node("board_state_node") {
    // ---- parameters
    const std::string startFen = declare_parameter<std::string>(
        "start_fen", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const std::string engineColorStr = declare_parameter<std::string>("engine_color", "black");

    if (engineColorStr == "white")      engineColor_ = Color::White;
    else if (engineColorStr == "black") engineColor_ = Color::Black;
    else throw std::invalid_argument("engine_color must be 'white' or 'black'");

    // Throws on a malformed FEN; main() catches it and exits.
    board_ = FEN::fromFEN(startFen);

    // ---- publisher: latched so late-starting nodes (stockfishNode) still get the position
    boardFenPub_ = create_publisher<std_msgs::msg::String>(
        "board_fen", rclcpp::QoS(1).reliable().transient_local());

    // ---- subscribers
    engineMoveSub_ = create_subscription<std_msgs::msg::String>(
        "engine_move", 10,
        std::bind(&BoardStateNode::onEngineMove, this, std::placeholders::_1));
    humanMoveSub_ = create_subscription<std_msgs::msg::String>(
        "human_move", 10,
        std::bind(&BoardStateNode::onHumanMove, this, std::placeholders::_1));

    RCLCPP_INFO(get_logger(), "Engine plays %s. Start position: %s",
                colorName(engineColor_), FEN::toFEN(board_).c_str());

    publishFEN();
}

void BoardStateNode::onEngineMove(const std_msgs::msg::String::SharedPtr msg) {
    // stockfishNode reports "(none)" when there is no legal move (checkmate/stalemate)
    if (msg->data == "(none)") {
        RCLCPP_INFO(get_logger(), "Engine has no legal moves: game over.");
        return;
    }

    const auto info = tryApplyMove(msg->data, engineColor_, "engine");
    if (!info) return;

    // TODO(hardware): build and publish a ChessMove message from `info` here
    // (moved, captured, captureSquare, isCastle, rookFrom/rookTo, promotion)
    // so the hardwareMovementNode knows how to physically execute the move.
    // This is done only for engine moves: human moves already happened on the board.

    publishFEN();
}

void BoardStateNode::onHumanMove(const std_msgs::msg::String::SharedPtr msg) {
    const auto info = tryApplyMove(msg->data, otherColor(engineColor_), "human");
    if (!info) return;

    publishFEN();  // this is what triggers the engine's reply
}

std::optional<MoveInfo> BoardStateNode::tryApplyMove(const std::string& moveStr,
                                                     Color expectedSide,
                                                     const char* source) {
    if (board_.sideToMove() != expectedSide) {
        RCLCPP_WARN(get_logger(), "Ignoring %s move '%s': it is %s's turn, not %s's.", source,
                    moveStr.c_str(), colorName(board_.sideToMove()), colorName(expectedSide));
        return std::nullopt;
    }

    try {
        const Move move = UCI::parseUCI(moveStr);
        const MoveInfo info = board_.describeMove(move);  // before applying!

        if (colorOfPiece(info.moved) != expectedSide) {
            RCLCPP_WARN(get_logger(), "Ignoring %s move '%s': piece on %s is not %s's.", source,
                        moveStr.c_str(), BoardState::squareToString(move.from).c_str(),
                        colorName(expectedSide));
            return std::nullopt;
        }

        board_.applyMove(move);
        RCLCPP_INFO(get_logger(), "Applied %s move %s -> %s", source, moveStr.c_str(),
                    FEN::toFEN(board_).c_str());
        return info;
    } catch (const std::exception& e) {
        // Never let an exception escape a callback: it would kill spin().
        RCLCPP_WARN(get_logger(), "Rejected %s move '%s': %s", source, moveStr.c_str(), e.what());
        return std::nullopt;
    }
}

void BoardStateNode::publishFEN() {
    std_msgs::msg::String msg;
    msg.data = FEN::toFEN(board_);
    boardFenPub_->publish(msg);
}

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    try {
        rclcpp::spin(std::make_shared<BoardStateNode>());
    } catch (const std::exception& e) {
        RCLCPP_FATAL(rclcpp::get_logger("main"), "Fatal: %s", e.what());
        rclcpp::shutdown();
        return 1;
    }

    rclcpp::shutdown();
    return 0;
}