cmake_minimum_required(VERSION 3.8)
project(board_state_pkg)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

include_directories(include)

add_executable(board_state_node
  src/boardStateNode.cpp
  src/boardState.cpp
)

target_include_directories(board_state_node PRIVATE
  include/board_state_pkg
  src
)

install(TARGETS
  board_state_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
