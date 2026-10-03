CURRENT STATE DOCUMENTATION 


10/3/2026

UCI protocol library has been implemented for Stockfish communication.

UCI provides command builders for sending commands to Stockfish and a parser for converting Stockfish output into typed messages using std::variant.

UCI parsing handles uciok, readyok, id, option, info, and bestmove messages, with unknown or malformed messages handled safely.

UCI is kept independent of BoardState. Move strings such as e2e4 are used at the UCI layer, while conversion to the internal Move type will be handled by StockfishNode.

UCI unit tests have been created for command builders and Stockfish response parsing.

Next step is implementing StockfishNode to spawn and communicate with the Stockfish process, read its output, and handle uciok, readyok, info, and bestmove responses.



Date: 9/30/2026

BoardState implementation is complete, including board representation, FEN conversion, UCI move parsing, move description, and move application.

BoardStateNode has been implemented as the ROS 2 interface for maintaining and updating the current BoardState.

BoardStateNode receives human/engine moves as ROS string messages, translates them internally, validates basic move information, updates the BoardState, and publishes the resulting FEN.

The ROS interface uses strings while UCI translation is handled internally by the respective nodes. This keeps the ROS communication separate from the Stockfish UCI protocol.

Initial unit tests have been created for BoardState and BoardStateNode. BoardState tests currently pass; ROS node tests will be run in the ROS 2 environment.

Next step is implementing StockfishNode to handle the Stockfish process, UCI communication, FEN input, and engine move output.

Date: 9/29/2026


Initial BoardState representation implemented.
BoardState stores the internal chess board and game state, with translation between the internal state and FEN notation.
UCI move parsing and additional FEN helper functions will be added as Stockfish communication is implemented.

The BoardState ROS 2 node/driver still needs to be implemented. Its required interface will be finalized after Stockfish communication is added and the needed board state functions are identified.

UCI protocal will follow this documentation: https://gist.github.com/DOBRO/2592c6dad754ba67e6dcaec8c90165bf

FEN protocal will follow this documentation: https://chessprogramming.org/Forsyth-Edwards_Notation 


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

