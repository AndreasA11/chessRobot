# ROS 2 Robot Chess

A ROS 2-based chess-playing robot project using **Stockfish** as the chess engine.

The project is initially being developed entirely in simulation/software, with the goal of eventually being transferable to a physical chess-playing robot.

## Project Overview

The system is split into three main stages:

```text
PERCEPTION        PLANNING          ACTION

Chess Board  →    Stockfish    →    Robot
    ↓                  ↓               ↓
BoardState         ChessMove       Move Piece
```

1. **Board Perception**

   * Determine the current chess board state.

2. **Chess Planning**

   * Give the board state to Stockfish.
   * Get the move Stockfish wants to play.
   * Allow the user to configure Stockfish's skill/strength.

3. **Robot Manipulation**

   * Given the board state and selected move, determine how to physically move the piece.
   * Initially use a simulated robot.
   * Eventually support physical robot hardware.


## Documentation

commands.md: contains often run commands that don't have to be retyped/found over and over again.

currentOverview.md is a brief overview of the project and its current state.

currentState.md is a log of the currentState of the project each big update.

implementation.md is a log of implementation summaries of each new update added.




## Current Setup

Development uses:

* ROS 2 Jazzy
* Docker
* `ros:jazzy-ros-base`
* Stockfish
* Ubuntu environment compatible with the target Raspberry Pi 5

### Docker Development Workflow

## Docker Development Environment

The project uses Docker containers to provide a consistent ROS 2 Jazzy environment. There are two containers:

- **`chessrobot-dev`** — Main development environment for chess logic, ROS 2 packages, Stockfish integration, and testing.
- **`chessrobot-sim`** — Simulation environment with Gazebo Harmonic and ROS-Gazebo integration.

Both containers mount the same repository directory, so source-code changes are shared between them.

### 1. Build the Docker Images

Build the development image from the repository root:

```bash
docker build -t chessrobot:latest .
```

Build the simulation image:

```bash
docker build -t chessrobot:sim .
```

The simulation image includes the additional Gazebo and GUI dependencies.

### 2. Create the Development Container (First Time Only)

Run from the repository root:

```bash
docker run -it \
    --name chessrobot-dev \
    -v "$(pwd)":/chessRobot \
    chessrobot:latest
```

### 3a WINDOWS. Create the Simulation Container :

Run from the repository root in Ubuntu WSL:

```bash
docker run -it \
    --name chessrobot-sim \
    --network host \
    -v "$(pwd)":/chessRobot \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v /mnt/wslg:/mnt/wslg \
    -e DISPLAY=$DISPLAY \
    -e WAYLAND_DISPLAY=$WAYLAND_DISPLAY \
    -e XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR \
    chessrobot:sim \
    bash
```

The additional volume mounts and environment variables allow Gazebo's GUI to display through WSLg on the Windows desktop.

### 3b MAC. Create the Simulation Container :

docker run -it \
  --name chessrobot-sim \
  -v "$(pwd)":/chessRobot \
  -p 6080:6080 \
  --shm-size=1g \
  chessrobot:sim bash

To run gazebo :
    In the container
    1.  Xvfb :0 -screen 0 1920x1080x24 +extension GLX +render -noreset &
    2.  export DISPLAY=:0
        export LIBGL_ALWAYS_SOFTWARE=1
        export GALLIUM_DRIVER=llvmpipe
        export QT_X11_NO_MITSHM=1
    3.  x11vnc -display :0 -forever -shared -nopw -rfbport 5900 &
    4.  websockify --web=/usr/share/novnc/ 6080 localhost:5900 &

### 4. Stop the Containers

To stop a running container:

```bash
docker stop chessrobot-dev
```

Or stop the simulation container:

```bash
docker stop chessrobot-sim
```

Alternatively, if you are inside a container, you can exit its shell:

```bash
exit
```

**Note:** Exiting the shell of a container started with `docker run -it` stops that container when its main process exits.

### 5. Start the Containers (Every Day After)

Start the development container:

```bash
docker start -ai chessrobot-dev
```

Start the simulation container:

```bash
docker start -ai chessrobot-sim
```

You can also start both containers in the background:

```bash
docker start chessrobot-dev
docker start chessrobot-sim
```

### 6. Open Additional Terminals Inside a Running Container

If a container is already running, open another terminal session without stopping it.

Development:

```bash
docker exec -it chessrobot-dev bash
```

Simulation:

```bash
docker exec -it chessrobot-sim bash
```

### 7. Run Gazebo

Inside the simulation container:

```bash
source /opt/ros/jazzy/setup.bash
gz sim
```

### Important Notes

- Both containers share the same source directory through the bind mount at `/chessRobot`.
- The containers have separate installed dependencies and environments.
- **Do not run `colcon build` in both containers simultaneously.** They share the `build/`, `install/`, and `log/` directories.
- Use `chessrobot-dev` for normal development and testing. Use `chessrobot-sim` when working with Gazebo.
- When modifying the Dockerfile, rebuild the corresponding image. Existing containers do not automatically update when an image is rebuilt. If dependencies change, recreate the affected container to use the updated image.

#### 6. Building inside the Container
Because ROS2 Jazzy is automatically sourced in your `.bashrc`, you only need to run:
```bash
colcon build
```

Verify Stockfish:

```bash
stockfish
```

Stockfish should start and provide its UCI prompt.

### Building

From the ROS 2 workspace inside the container:

```bash
colcon build
```

Then source the workspace:

```bash
source install/setup
```


## Usage Examples

### Run the chess system

Once the ROS 2 nodes are implemented:

```bash
ros2 launch robot_chess chess.launch.py
```

### Run Stockfish

The Stockfish node will eventually receive a board state and return a chess move.

Conceptually:

```text
BoardState
    ↓
Stockfish
    ↓
ChessMove
```

Example:

```text
BoardState: starting position

Stockfish:
    e2e4
```

### Execute a Move

A chess move such as:

```text
e2 → e4
```

should eventually become a robot manipulation sequence:

```text
MoveTo(e2)
Pick()
MoveTo(e4)
Place()
```

The same interface should eventually work with either a simulated or physical robot.


