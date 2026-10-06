#include "StockfishNode.hpp"

#include <utility>

StockfishNode::StockfishNode(const rclcpp::NodeOptions& options)
    : rclcpp::Node("stockfish_node", options) {
    const auto enginePath = declare_parameter<std::string>("engine_path", "stockfish");
    threads_    = static_cast<int>(declare_parameter<int64_t>("threads", 1));
    hashMb_     = static_cast<int>(declare_parameter<int64_t>("hash_mb", 16));
    skillLevel_ = static_cast<int>(declare_parameter<int64_t>("skill_level", 20));
    moveTimeMs_ = static_cast<int>(declare_parameter<int64_t>("move_time_ms", 1000));
    const auto fenTopic  = declare_parameter<std::string>("fen_topic", "board_fen");
    const auto moveTopic = declare_parameter<std::string>("move_topic", "stockfish_move");

    stockfishMovePub_ = create_publisher<std_msgs::msg::String>(moveTopic, 10);
    fenSub_ = create_subscription<std_msgs::msg::String>(
        fenTopic, 10, [this](const std_msgs::msg::String& msg) { onFen(msg); });

    // Throws std::runtime_error if the engine can't be launched.
    engine_ = std::make_unique<EngineProcess>(enginePath);
    reader_ = std::thread(&StockfishNode::readerLoop, this);

    try {
        engine_->writeLine(uci::cmdUci());   // kicks off the handshake; reply arrives as UciOk
    } catch (...) {
        // The destructor won't run if the constructor throws, so clean up the thread here.
        running_ = false;
        engine_->shutdown();
        reader_.join();
        throw;
    }
    RCLCPP_INFO(get_logger(), "Started engine '%s', waiting for uciok", enginePath.c_str());
}

StockfishNode::~StockfishNode() {
    running_ = false;
    if (engine_) {
        try { engine_->writeLine(uci::cmdQuit()); } catch (...) {}
        engine_->shutdown();   // reader thread sees EOF and returns
    }
    if (reader_.joinable()) reader_.join();
}

// ===========================================================================
// ROS side
// ===========================================================================
void StockfishNode::onFen(const std_msgs::msg::String& msg) {
    const std::string& fen = msg.data;

    // A newline would let a bad message inject extra UCI commands.
    if (fen.empty() || fen.find_first_of("\r\n") != std::string::npos) {
        RCLCPP_WARN(get_logger(), "Ignoring empty or malformed FEN");
        return;
    }

    bool sendNow = false;
    bool sendStop = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == State::Idle) {
            state_ = State::Searching;
            sendNow = true;
        } else {
            // Engine is starting up or busy: remember only the newest position.
            pendingFen_ = fen;
            if (state_ == State::Searching && !staleSearch_) {
                staleSearch_ = true;   // its bestmove will be discarded
                sendStop = true;
            }
        }
    }

    if (sendNow)  sendSearch(fen);
    if (sendStop) {
        RCLCPP_WARN(get_logger(), "New FEN while searching: stopping current search");
        try { engine_->writeLine(uci::cmdStop()); }
        catch (const std::exception& e) { RCLCPP_ERROR(get_logger(), "%s", e.what()); }
    }
}

void StockfishNode::sendSearch(const std::string& fen) {
    try {
        engine_->writeLine(uci::cmdPosition(fen));
        uci::GoParams go;
        go.moveTime = moveTimeMs_;
        engine_->writeLine(uci::cmdGo(go));
    } catch (const std::exception& e) {
        RCLCPP_ERROR(get_logger(), "Failed to start search: %s", e.what());
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State::Idle;
    }
}

void StockfishNode::publishMove(const std::string& move) {
    std_msgs::msg::String msg;
    msg.data = move;
    stockfishMovePub_->publish(msg);
    RCLCPP_INFO(get_logger(), "Published move %s", move.c_str());
}

// ===========================================================================
// Engine side (reader thread)
// ===========================================================================
void StockfishNode::readerLoop() {
    std::string line;
    while (engine_->readLine(line)) {
        try {
            handleParsedOutput(uci::parseLine(line));
        } catch (const std::exception& e) {
            RCLCPP_ERROR(get_logger(), "Error handling engine output '%s': %s", line.c_str(), e.what());
        }
    }
    if (running_) RCLCPP_ERROR(get_logger(), "Engine process ended unexpectedly");
}

void StockfishNode::handleParsedOutput(const uci::Message& msg) {
    std::visit(uci::overloaded{
        [this](const uci::UciOk&)           { handleUciOk(); },
        [this](const uci::ReadyOk&)         { handleReadyOk(); },
        [this](const uci::IdLine& m)        { handleIdLine(m); },
        [this](const uci::Option& m)        { handleOption(m); },
        [this](const uci::Info& m)          { handleInfo(m); },
        [this](const uci::BestMove& m)      { handleBestMove(m); },
        [this](const uci::Unknown& m)       { handleUnknown(m); },
    }, msg);
}

void StockfishNode::handleUciOk() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::WaitingUciOk) return;
        state_ = State::WaitingReadyOk;
    }
    engine_->writeLine(uci::cmdSetOption("Threads", std::to_string(threads_)));
    engine_->writeLine(uci::cmdSetOption("Hash", std::to_string(hashMb_)));
    engine_->writeLine(uci::cmdSetOption("Skill Level", std::to_string(skillLevel_)));
    engine_->writeLine(uci::cmdUciNewGame());
    engine_->writeLine(uci::cmdIsReady());   // reply arrives as ReadyOk
}

void StockfishNode::handleReadyOk() {
    std::optional<std::string> fen;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::WaitingReadyOk) return;   // stray readyok
        if (pendingFen_) {
            fen = std::move(pendingFen_);
            pendingFen_.reset();
            state_ = State::Searching;
        } else {
            state_ = State::Idle;
        }
    }
    RCLCPP_INFO(get_logger(), "Engine ready");
    if (fen) sendSearch(*fen);   // a FEN arrived during startup
}

void StockfishNode::handleIdLine(const uci::IdLine& id) {
    RCLCPP_INFO(get_logger(), "Engine %s: %s",
                id.field == uci::IdLine::Field::Name ? "name" : "author", id.value.c_str());
}

void StockfishNode::handleOption(const uci::Option& opt) {
    RCLCPP_DEBUG(get_logger(), "Engine option: %s", opt.name.c_str());
}

void StockfishNode::handleInfo(const uci::Info& info) {
    if (info.text) {
        RCLCPP_DEBUG(get_logger(), "Engine: %s", info.text->c_str());
        return;
    }
    // Keep only the principal variation's score (multipv absent or 1).
    if (info.score && (!info.multipv || *info.multipv == 1)) {
        std::lock_guard<std::mutex> lock(mutex_);
        latestInfo_ = info;
    }
}

void StockfishNode::handleBestMove(const uci::BestMove& bm) {
    std::optional<std::string> toPublish;
    std::optional<std::string> nextFen;
    bool stray = false;
    bool discarded = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::Searching) {
            stray = true;
        } else {
            if (staleSearch_) {
                staleSearch_ = false;
                discarded = true;
            } else if (bm.move != "(none)") {
                toPublish = bm.move;
            }
            if (pendingFen_) {
                nextFen = std::move(pendingFen_);
                pendingFen_.reset();   // stays in Searching
            } else {
                state_ = State::Idle;
            }
        }
    }

    if (stray) {
        RCLCPP_WARN(get_logger(), "Ignoring unexpected bestmove %s", bm.move.c_str());
        return;
    }
    if (discarded) {
        RCLCPP_INFO(get_logger(), "Discarded bestmove %s for outdated position", bm.move.c_str());
    } else if (toPublish) {
        publishMove(*toPublish);
    } else {
        RCLCPP_WARN(get_logger(), "Engine has no legal move (bestmove %s)", bm.move.c_str());
    }
    if (nextFen) sendSearch(*nextFen);
}

void StockfishNode::handleUnknown(const uci::Unknown& u) {
    RCLCPP_DEBUG(get_logger(), "Unrecognised engine output: %s", u.raw.c_str());
}