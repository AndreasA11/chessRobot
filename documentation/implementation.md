# Implementation

## Current Implementation Order

1. Create a chess board/state representation.
2. Convert the board state into a format Stockfish can use, likely FEN.
3. Integrate Stockfish with ROS 2.
4. Add user-configurable Stockfish skill/strength.
5. Represent Stockfish's output as a `Move`.
6. Given `BoardState` + `Move`, create a robot manipulation plan.
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

```text
       PERCEPTION              PLANNING              ACTION

    ┌─────────────┐        ┌─────────────┐        ┌─────────────┐
    │ Chess Board │        │  Stockfish  │        │    Robot    │
    │             │        │             │        │             │
    │   observe   │───────▶│ decide move │───────▶│ execute move│
    └─────────────┘        └─────────────┘        └─────────────┘
           │                       │                      │
           ▼                       ▼                      ▼
      BoardState                 Move                Physical
                                                    Board Change
```

### 1. Board Perception

Determine the current chess board state.

### 2. Chess Planning

Give the current board state to Stockfish and determine the move to play.

### 3. Robot Manipulation

Given the board state and selected move, generate and execute the physical actions required to make that move.

---

# Board State

`BoardState` is the central representation of the chess position.

The package uses an `8x8` array of `Piece` values:

```cpp
Piece board_[8][8];
```

The indexing convention is:

```text
board_[rank][file]

file: 0 = a, ..., 7 = h
rank: 0 = 1, ..., 7 = 8
```

For example:

```text
board_[0][0] → a1
board_[1][4] → e2
board_[3][4] → e4
board_[7][7] → h8
```

The board representation is intentionally a simple square-based representation rather than an engine-oriented bitboard implementation. Stockfish is responsible for chess-specific calculations such as move generation, attack detection, and evaluation.

## Board State Information

`BoardState` currently stores:

* Piece on each square
* Side to move
* Castling rights
* En passant target square
* Halfmove clock
* Fullmove number

This information is sufficient to represent the chess position and convert it to/from FEN.

## BoardState Interface

The main operations are:

```cpp
Piece at(Square) const;
Color sideToMove() const;

MoveInfo describeMove(const Move&) const;
void applyMove(const Move&);
```

### `at()`

Returns the piece currently occupying a square.

### `sideToMove()`

Returns the color whose turn it is.

### `describeMove()`

Determines the information needed to apply a move, including:

* Piece being moved
* Captured piece
* Capture square
* Castling information
* Promotion information

This is called **before** `applyMove()`.

### `applyMove()`

Updates the board state after a move.

`applyMove()` assumes the supplied move is legal. Move legality and chess-engine decisions are not the responsibility of `BoardState`.

---

# FEN

FEN is the planned interface between `BoardState` and Stockfish.

```text
BoardState
    │
    ▼
 FEN string
    │
    ▼
Stockfish
    │
    ▼
 UCI move
    │
    ▼
 Move
    │
    ▼
BoardState
```

The `FEN` namespace provides:

```cpp
BoardState FEN::fromFEN(const std::string&);
std::string FEN::toFEN(const BoardState&);
```

`toFEN()` converts the current board state into a FEN position for Stockfish.

`fromFEN()` allows a board state to be initialized from an existing FEN position, which will also be useful for testing and simulation.

---

# UCI

Stockfish communicates moves using UCI notation.

The `UCI` namespace will convert Stockfish's move representation into the project's `Move` representation.

```cpp
Move UCI::parseUCI(const std::string&);
```

For example:

```text
e2e4
```

becomes a move from:

```text
e2 → e4
```

This keeps Stockfish's communication format separate from the internal board representation.

---

# Design Boundary

`BoardState` is responsible for **representing and updating the position**, not for being a chess engine.

It does not handle:

* Move generation
* Move evaluation
* Check/checkmate detection
* Engine search
* Piece attack tables
* Magic bitboards
* Stockfish logic

Those responsibilities belong to Stockfish or higher-level packages.

The intended flow is:

```text
BoardState
    │
    │ FEN
    ▼
Stockfish
    │
    │ UCI move
    ▼
Move
    │
    ▼
BoardState
    │
    ▼
Robot manipulation
```
