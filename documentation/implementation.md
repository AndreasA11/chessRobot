IMPLEMENTATION DOCUMENTATION

Date: 9/28/2026

Current Implementation Order

1. Create a chess board/state representation.
2. Convert the board state into a format Stockfish can use, likely FEN.
3. Integrate Stockfish with ROS 2.
4. Add user-configurable Stockfish skill/strength.
5. Represent Stockfish's output as a ChessMove.
6. Given BoardState + ChessMove, create a robot manipulation plan.
7. Simulate basic piece movement.
8. Add captures and special moves:
   * Castling
   * Promotion
   * En passant
9. Add board/robot visualization.
10. Later add camera-based board perception.
11. Later add a more realistic robot simulation and motion planning.
12. Eventually connect the robot controller to physical hardware.

## Architecture

          PERCEPTION              PLANNING              ACTION

       ┌─────────────┐        ┌─────────────┐        ┌─────────────┐
       │ Chess Board │        │  Stockfish  │        │    Robot    │
       │             │        │             │        │             │
       │   observe   │───────▶│ decide move │───────▶│ execute move│
       └─────────────┘        └─────────────┘        └─────────────┘
              │                       │                      │
              ▼                       ▼                      ▼
         BoardState                ChessMove             Physical
                                                        Board Change


1. Board Perception
    Determine the current chess board state.

2. Chess Planning
    Give the current board state to Stockfish and determine the move to play.

3. Robot Manipulation
    Given the board state and selected move, generate and execute the physical actions required to make that move.