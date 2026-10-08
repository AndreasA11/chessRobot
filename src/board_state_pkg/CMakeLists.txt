cmake_minimum_required(VERSION 3.8)

project(board_state_pkg)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

# BoardState library
add_library(board_state_lib
  src/BoardState.cpp
  src/Diagnose.cpp
  src/Infer.cpp
)

target_include_directories(board_state_lib PUBLIC
  include/board_state_pkg
)

# BoardStateNode library
add_library(board_state_node_lib
  src/BoardStateNode.cpp
)

# ament_target_dependencies(
#   board_state_lib
#   rclcpp
#   std_msgs
# )

target_include_directories(board_state_node_lib PUBLIC
  include/board_state_pkg
)

ament_target_dependencies(
  board_state_node_lib
  rclcpp
  std_msgs
)

target_link_libraries(
  board_state_node_lib
  board_state_lib
)

# Normal ROS node executable
add_executable(board_state_node
  src/BoardStateNodeMain.cpp
)

target_link_libraries(
  board_state_node
  board_state_node_lib
)

ament_target_dependencies(
  board_state_node
  rclcpp
  std_msgs
)

# Tests
if(BUILD_TESTING)

  find_package(ament_cmake_gtest REQUIRED)

  # BoardState unit tests
  ament_add_gtest(
    test_board_state
    tests/test_board_state.cpp
  )

  target_link_libraries(
    test_board_state
    board_state_lib
  )

  # BoardStateNode tests
  # This test provides its own main() because it calls rclcpp::init().
  ament_add_gtest(
    test_board_state_node
    tests/test_board_state_node.cpp
    SKIP_LINKING_MAIN_LIBRARIES
  )

  target_link_libraries(
    test_board_state_node
    board_state_node_lib
  )

  #occupancy tests
  ament_add_gtest(
    test_occupancy
    tests/test_occupancy.cpp
  )

  target_link_libraries(
    test_occupancy
    board_state_lib
  )
  

endif()

# Install normal executable
install(
  TARGETS
    board_state_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()