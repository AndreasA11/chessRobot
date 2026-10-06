cmake_minimum_required(VERSION 3.8)
project(evaluation_pkg)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(Threads REQUIRED)


add_library(uci_engine STATIC
  src/UCI.cpp
  src/EngineProcess.cpp
)
target_include_directories(uci_engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/evaluation_pkg>
)
target_link_libraries(uci_engine PUBLIC Threads::Threads)
set_target_properties(uci_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)


add_library(stockfish_node_lib STATIC
  src/StockfishNode.cpp
)
target_link_libraries(stockfish_node_lib PUBLIC
  uci_engine
  rclcpp::rclcpp
  ${std_msgs_TARGETS}
)


add_executable(stockfish_node 
  src/StockfishNodeMain.cpp)
target_link_libraries(stockfish_node PRIVATE 
  stockfish_node_lib)

install(TARGETS stockfish_node
  DESTINATION lib/${PROJECT_NAME}
)


if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_uci tests/UCItest.cpp)
  target_link_libraries(test_uci uci_engine)

  ament_add_gtest(test_engine_process tests/test_engine_process.cpp TIMEOUT 120)
  target_link_libraries(test_engine_process uci_engine)

  # Needs python3 on PATH (the fake engine is a python script).
  # Set STOCKFISH_PATH to run the tests that use a real Stockfish; they skip otherwise.
  ament_add_gtest(test_stockfish_node tests/test_stockfish_node.cpp TIMEOUT 180)
  target_link_libraries(test_stockfish_node stockfish_node_lib)
endif()

ament_package()