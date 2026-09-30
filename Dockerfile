FROM ros:jazzy-ros-base

RUN apt-get update && \
    apt-get install -y \
        stockfish \
        && rm -rf /var/lib/apt/lists/

ENV PATH="/usr/games:${PATH}"

WORKDIR /chessRobot

COPY . /chessRobot


RUN echo "source /opt/ros/jazzy/setup.bash" >> ~/.bashrc

RUN /bin/bash -c "source /opt/ros/jazzy/setup.bash && \
    rm -rf build/ install/ log/ && \
    colcon build"