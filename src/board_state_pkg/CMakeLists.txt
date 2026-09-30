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

ament_target_dependencies(
  board_state_node
  rclcpp
  std_msgs
)

target_include_directories(board_state_node PRIVATE
  include/board_state_pkg
  src
)

add_library(board_state_lib src/boardState.cpp)

target_include_directories(board_state_lib PUBLIC include)

add_library(board_state_node_lib src/boardStateNode.cpp)

ament_target_dependencies(board_state_node_lib rclcpp std_msgs)

target_link_libraries(board_state_node_lib board_state_lib)

add_executable(board_state_node src/board_state_node_main.cpp)

target_link_libraries(board_state_node board_state_node_lib)

install(TARGETS board_state_node DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_board_state test/test_board_state.cpp)
  target_link_libraries(test_board_state board_state_lib)

  # own main() (calls rclcpp::init), so skip gtest_main
  ament_add_gtest(test_board_state_node test/test_board_state_node.cpp SKIP_LINKING_MAIN_LIBRARIES)
  target_link_libraries(test_board_state_node board_state_node_lib GTest::gtest)
endif()

install(TARGETS
  board_state_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
