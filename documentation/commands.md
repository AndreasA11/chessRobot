# Build everything
colcon build

# Build one package
colcon build --packages-select board_state_pkg

# Build with live/verbose output
colcon build --event-handlers console_direct+


# Run all package tests
colcon test

# Run tests with output shown directly
colcon test --event-handlers console_direct+

# Show test results
colcon test-result --verbose

# See what tests CMake registered
cd build/board_state_pkg
ctest -N

# Actually run those tests directly
ctest --output-on-failure



#chesscog commands:
    1. find corners
        poetry run python -m chesscog.corner_detection.detect_corners testLocals/images/test#.png

    2. get position
        poetry run python -m chesscog.recognition.recognition testLocals/images/test#.png --[white | black]


