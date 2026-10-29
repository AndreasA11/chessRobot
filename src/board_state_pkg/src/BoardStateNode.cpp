#include "BoardStateNode.hpp"
#include "BoardState.hpp"
#include "Diagnose.hpp"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

Color OtherColor(Color c) {
    return (c == Color::White ? Color::Black : Color::White);
}

Color ColorOfPiece(Piece p) {
    return std::isupper(static_cast<unsigned char>(static_cast<char>(p))) ? Color::White
                                                                          : Color::Black;
}

const char *ColorName(Color c) {
    return (c == Color::White ? "white" : "black");
}

const char *KindName(Action::Kind k) {
    switch(k) {
        case Action::Kind::Wait:             return "wait";
        case Action::Kind::Commit:           return "commit";
        case Action::Kind::AskPromotion:     return "ask_promotion";
        case Action::Kind::AskWhichMove:     return "ask_which_move";
        case Action::Kind::ReportIllegal:    return "report_illegal";
        case Action::Kind::AskCheckBoard:    return "ask_check_board";
        case Action::Kind::RequestClearView: return "request_clear_view";
    }
    return "unknown";
}

// "e2e4", or "e7e8q" when a promotion piece is set
std::string MoveToUci(const Move &m) {
    std::string s = BoardState::squareToString(m.from) + BoardState::squareToString(m.to);
    if(!BoardState::isEmpty(m.promotion)) {
        s += static_cast<char>(std::tolower(static_cast<unsigned char>(BoardState::typeOf(m.promotion))));
    }
    return s;
}

// Lowercase with whitespace removed, so " E2E4\n" matches "e2e4"
std::string Normalized(const std::string &s) {
    std::string out;
    for(char c : s) {
        if(!std::isspace(static_cast<unsigned char>(c))) {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

} // namespace

BoardStateNode::BoardStateNode(const rclcpp::NodeOptions &options)
    : rclcpp::Node("board_state_node", options) {
    const std::string startFen = declare_parameter<std::string>(
        "start_fen", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const std::string engineColorStr = declare_parameter<std::string>("engine_color", "black");

    if(engineColorStr == "white") {
        engineColor_ = Color::White;
    } else if(engineColorStr == "black") {
        engineColor_ = Color::Black;
    } else {
        throw std::invalid_argument("engine_color must be 'white' or 'black'");
    }

    // Perception policy knobs (defaults come from the config structs)
    DecoderConfig decoderCfg;
    decoderCfg.minScore = declare_parameter<double>("decoder_min_score", decoderCfg.minScore);
    decoderCfg.margin = declare_parameter<double>("decoder_margin", decoderCfg.margin);

    ArbiterConfig arbiterCfg;
    arbiterCfg.confirmFrames =
        declare_parameter<int>("arbiter_confirm_frames", arbiterCfg.confirmFrames);
    arbiterCfg.ambiguousFrameLimit =
        declare_parameter<int>("arbiter_ambiguous_frame_limit", arbiterCfg.ambiguousFrameLimit);
    arbiterCfg.noLegalFrameLimit =
        declare_parameter<int>("arbiter_no_legal_frame_limit", arbiterCfg.noLegalFrameLimit);
    arbiterCfg.unusableFrameLimit =
        declare_parameter<int>("arbiter_unusable_frame_limit", arbiterCfg.unusableFrameLimit);

    decoder_ = MoveDecoder(decoderCfg);
    arbiter_ = Arbiter(arbiterCfg);

    board_ = FEN::fromFEN(startFen);

    boardFenPub_ = create_publisher<std_msgs::msg::String>(
        "board_fen", rclcpp::QoS(1).reliable().transient_local());
    eventPub_ = create_publisher<std_msgs::msg::String>("board_event", 10);

    engineMoveSub_ = create_subscription<std_msgs::msg::String>(
        "engine_move", 10,
        std::bind(&BoardStateNode::onEngineMove, this, std::placeholders::_1));
    humanMoveSub_ = create_subscription<std_msgs::msg::String>(
        "human_move", 10,
        std::bind(&BoardStateNode::onHumanMove, this, std::placeholders::_1));
    observationSub_ = create_subscription<board_observation_interface::msg::BoardObservation>(
        "board_observation", 10,
        std::bind(&BoardStateNode::onObservation, this, std::placeholders::_1));
    answerSub_ = create_subscription<std_msgs::msg::String>(
        "human_answer", 10,
        std::bind(&BoardStateNode::onHumanAnswer, this, std::placeholders::_1));

    RCLCPP_INFO(get_logger(), "Engine plays %s. Start position: %s",
                ColorName(engineColor_), FEN::toFEN(board_).c_str());

    afterBoardChanged();
}

void BoardStateNode::onEngineMove(const std_msgs::msg::String::SharedPtr msg) {
    if(msg->data == "(none)") {
        RCLCPP_INFO(get_logger(), "Engine has no legal moves: game over.");
        return;
    }

    const std::optional<MoveInfo> info = tryApplyMove(msg->data, engineColor_, "engine");
    if(!info) {
        return;
    }

    afterBoardChanged();
}

void BoardStateNode::onHumanMove(const std_msgs::msg::String::SharedPtr msg) {
    const std::optional<MoveInfo> info = tryApplyMove(msg->data, OtherColor(engineColor_), "human");
    if(!info) {
        return;
    }

    afterBoardChanged();
}

void BoardStateNode::onObservation(const board_observation_interface::msg::BoardObservation::SharedPtr msg) {
    // Only watch for human moves: not while the robot is moving, not while a question
    // is waiting for an answer, and not once the game is over.
    if(board_.sideToMove() == engineColor_ || pending_ || decoder_.legalMoveCount() == 0) {
        return;
    }

    SoftObs obs;
    std::copy(msg->probs.begin(), msg->probs.end(), obs.p.begin());

    const Decoding d = decoder_.observe(obs, msg->usable);
    handleAction(arbiter_.step(board_, d, obs.hardLabels()));
}

void BoardStateNode::handleAction(const Action &a) {
    using K = Action::Kind;

    switch(a.kind) {
        case K::Wait:
            return;

        case K::Commit:
            if(tryApplyMove(MoveToUci(a.move), OtherColor(engineColor_), "camera")) {
                publishEvent(KindName(a.kind), a.message);
                afterBoardChanged();
            } else {
                // Should not happen (the Arbiter validates first); start observing afresh
                decoder_.clearEvidence();
                arbiter_.reset();
            }
            return;

        case K::RequestClearView:
            publishEvent(KindName(a.kind), a.message);
            return;

        case K::AskPromotion:
        case K::AskWhichMove:
        case K::ReportIllegal:
        case K::AskCheckBoard:
            pending_ = a;
            publishEvent(KindName(a.kind), a.message);
            return;
    }
}

void BoardStateNode::onHumanAnswer(const std_msgs::msg::String::SharedPtr msg) {
    if(!pending_) {
        RCLCPP_WARN(get_logger(), "Ignoring answer '%s': no question is pending.",
                    msg->data.c_str());
        return;
    }

    const std::string answer = Normalized(msg->data);
    const Action question = *pending_;

    switch(question.kind) {
        case Action::Kind::AskPromotion: {
            if(answer.size() != 1 || std::string("qrbn").find(answer[0]) == std::string::npos) {
                RCLCPP_WARN(get_logger(), "Promotion answer must be q, r, b or n (got '%s').",
                            answer.c_str());
                return;
            }
            if(tryApplyMove(MoveToUci(question.move) + answer, OtherColor(engineColor_),
                            "human answer")) {
                afterBoardChanged();
            }
            return;
        }

        case Action::Kind::AskWhichMove: {
            Move chosen;
            if(answer == MoveToUci(question.move)) {
                chosen = question.move;
            } else if(answer == MoveToUci(question.alt)) {
                chosen = question.alt;
            } else {
                RCLCPP_WARN(get_logger(), "Answer must be '%s' or '%s' (got '%s').",
                            MoveToUci(question.move).c_str(), MoveToUci(question.alt).c_str(),
                            answer.c_str());
                return;
            }

            // The chosen move may itself be a promotion that still needs a piece
            if(Diagnose::diagnoseMove(board_, chosen).verdict ==
               Diagnose::Verdict::AmbiguousPromotion) {
                Action next;
                next.kind = Action::Kind::AskPromotion;
                next.move = chosen;
                next.message = "Which piece should the pawn on " +
                               BoardState::squareToString(chosen.to) +
                               " promote to? (q, r, b or n)";
                pending_ = next;
                publishEvent(KindName(next.kind), next.message);
                return;
            }

            if(tryApplyMove(MoveToUci(chosen), OtherColor(engineColor_), "human answer")) {
                afterBoardChanged();
            }
            return;
        }

        case Action::Kind::ReportIllegal:
        case Action::Kind::AskCheckBoard:
            if(answer != "ok") {
                RCLCPP_WARN(get_logger(), "Answer 'ok' once the board is fixed (got '%s').",
                            answer.c_str());
                return;
            }
            // Board fixed: drop the stale evidence from the bad frames and resume
            pending_.reset();
            decoder_.clearEvidence();
            arbiter_.reset();
            return;

        case Action::Kind::Wait:
        case Action::Kind::Commit:
        case Action::Kind::RequestClearView:
            return;  // never pending
    }
}

std::optional<MoveInfo> BoardStateNode::tryApplyMove(const std::string &moveStr,
                                                     Color expectedSide,
                                                     const char *source) {
    if(board_.sideToMove() != expectedSide) {
        RCLCPP_WARN(get_logger(), "Ignoring %s move '%s': it is %s's turn, not %s's.", source,
                    moveStr.c_str(), ColorName(board_.sideToMove()), ColorName(expectedSide));
        return std::nullopt;
    }

    try {
        const Move move = UCI::parseUCI(moveStr);
        const MoveInfo info = board_.describeMove(move);

        if(ColorOfPiece(info.moved) != expectedSide) {
            RCLCPP_WARN(get_logger(), "Ignoring %s move '%s': piece on %s is not %s's.", source,
                        moveStr.c_str(), BoardState::squareToString(move.from).c_str(),
                        ColorName(expectedSide));
            return std::nullopt;
        }

        board_.applyMove(move);
        RCLCPP_INFO(get_logger(), "Applied %s move %s -> %s", source, moveStr.c_str(),
                    FEN::toFEN(board_).c_str());
        return info;
    } catch(const std::exception &e) {
        RCLCPP_WARN(get_logger(), "Rejected %s move '%s': %s", source, moveStr.c_str(), e.what());
        return std::nullopt;
    }
}

void BoardStateNode::afterBoardChanged() {
    pending_.reset();
    decoder_.reset(board_);
    arbiter_.reset();
    publishFEN();

    if(decoder_.legalMoveCount() == 0) {
        RCLCPP_INFO(get_logger(), "No legal moves for %s: game over.",
                    ColorName(board_.sideToMove()));
    }
}

void BoardStateNode::publishEvent(const std::string &kind, const std::string &message) {
    std_msgs::msg::String msg{};
    msg.data = kind + ": " + message;
    eventPub_->publish(msg);
    RCLCPP_INFO(get_logger(), "%s", msg.data.c_str());
}

void BoardStateNode::publishFEN() {
    std_msgs::msg::String msg{};
    msg.data = FEN::toFEN(board_);
    boardFenPub_->publish(msg);
}