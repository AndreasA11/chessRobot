
FROM ros:jazzy-ros-base

# Set the workspace directory
WORKDIR /ros2_workspace/chessRobot

# Install system dependencies
RUN apt-get update && apt-get install -y \
    python3-pip \
    stockfish \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

# Make Stockfish accessible
ENV PATH="/usr/games:${PATH}"

# Copy the local workspace into the container
COPY . /ros2_workspace/chessRobot

# Source ROS automatically in interactive bash shells
RUN echo "source /opt/ros/jazzy/setup.bash" >> /root/.bashrc

# Build the ROS 2 workspace
RUN /bin/bash -c "source /opt/ros/jazzy/setup.bash && \
    rm -rf build/ install/ log/ && \
    colcon build"
