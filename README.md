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

## Current Setup

Development uses:

* ROS 2 Jazzy
* Docker
* `ros:jazzy-ros-base`
* Stockfish
* Ubuntu environment compatible with the target Raspberry Pi 5

### Docker Development Workflow

#### 1. Build the Docker Image
Build the image containing your base dependencies (ROS2 Jazzy & Stockfish) from the repository root:
```bash
docker build -t chessrobot .
```

#### 2. Start the Development Container (With Volume Mount)
Run this command to start the container. It maps your current local directory into the container so code updates sync instantly:
```bash
docker run -it --rm --name chessrobot-dev -v "$(pwd)":/chessRobot chessrobot
```
*(Note: `--rm` ensures the container automatically cleans up when you exit, avoiding "name already in use" errors).*

#### 3. Open an Additional Terminal Inside the Container
If you already have the container running and need a second terminal session:
```bash
docker exec -it chessrobot-dev bash
```

#### 4. Building inside the Container
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


