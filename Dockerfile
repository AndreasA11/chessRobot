FROM ros:jazzy-ros-base

# Use the same workspace path as the bind mount
WORKDIR /chessRobot

# Install system dependencies
RUN apt-get update && apt-get install -y \
    python3-pip \
    stockfish \
    build-essential \
    ros-jazzy-ros-gz \
    mesa-utils \
    x11-apps \
    && rm -rf /var/lib/apt/lists/*

# Make Stockfish accessible
ENV PATH="/usr/games:${PATH}"

# Copy the workspace into the image
COPY . /chessRobot

# Source ROS automatically in interactive bash shells
RUN echo "source /opt/ros/jazzy/setup.bash" >> /root/.bashrc

# Build the ROS 2 workspace
RUN /bin/bash -c "source /opt/ros/jazzy/setup.bash && \
    rm -rf build/ install/ log/ && \
    colcon build"
