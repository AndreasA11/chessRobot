FROM ros:jazzy-ros-base

# Set a working directory right away to keep things clean
WORKDIR /ros2_workspace/chessRobot

# Install system dependencies (Stockfish and any build essentials if needed)
RUN apt-get update && apt-get install -y \
    python3-pip \
    stockfish \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

# Fix Stockfish path environment variable
ENV PATH="/usr/games:${PATH}"

# Copy requirements and install python packages
COPY requirements.txt .
RUN pip3 install --break-system-packages --ignore-installed --no-cache-dir -r requirements.txt

# Copy the rest of your workspace
COPY . /chessRobot

# Add ROS setup to bashrc for interactive shells
RUN echo "source /opt/ros/jazzy/setup.bash" >> ~/.bashrc

# Build the ROS2 workspace using colcon
RUN /bin/bash -c "source /opt/ros/jazzy/setup.bash && \
    rm -rf build/ install/ log/ && \
    colcon build"
