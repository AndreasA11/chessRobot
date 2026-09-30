CURRENT STATE DOCUMENTATION 


Date: 9/29/2026


Initial BoardState representation implemented.
BoardState stores the internal chess board and game state, with translation between the internal state and FEN notation.
UCI move parsing and additional FEN helper functions will be added as Stockfish communication is implemented.

The BoardState ROS 2 node/driver still needs to be implemented. Its required interface will be finalized after Stockfish communication is added and the needed board state functions are identified.





Date: 9/28/2026

Project just started; overall implementation plan created.
Using Docker ros:jazzy-ros-base to match the Ubuntu/ROS 2 Jazzy environment intended for the Raspberry Pi 5.
ROS 2 will be used as the middleware.
Stockfish will be used as the chess engine.
Development will initially be software/simulation only; physical robot hardware will come later.

## General Plan

1. Board Perception
    Interpret a chess board and determine the current BoardState.

2. Chess Planning**
    Feed BoardState to Stockfish.
    Get the move Stockfish wants to play.
    Allow the user to configure Stockfish skill/strength.

3. Robot Manipulation
    Given BoardState + the selected chess move, determine how to physically make the move.
    Eventually simulate the robot moving pieces before connecting to real hardware.

## Architecture

PERCEPTION        PLANNING          ACTION

Chess Board  →    Stockfish    →    Robot
    ↓                  ↓               ↓
BoardState         ChessMove       Move Piece


The system should keep chess logic separate from robot movement so the same chess/planning software can eventually be used with a real robot.

