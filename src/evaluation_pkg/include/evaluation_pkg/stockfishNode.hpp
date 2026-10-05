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

#include "engineProcess.hpp"
#include "UCI.hpp"


class StockfishNode : public rclcpp::Node {
    public:
        
    private:
};


#endif