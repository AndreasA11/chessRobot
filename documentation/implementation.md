# Implementation

## Current Implementation Order

1. Create a chess board/state representation. **Complete**
2. Convert the board state into FEN. **Complete**
3. Implement UCI move parsing. **Complete**
4. Implement the `BoardStateNode` ROS 2 interface. **Complete**
5. Add automated tests for BoardState and BoardStateNode. **Complete**
6. Integrate Stockfish with ROS 2. **In progress**
7. Add user-configurable Stockfish skill/strength.
8. Complete the FEN → Stockfish → UCI move → BoardState loop.
9. Represent the resulting chess move for robot manipulation.
10. Given `BoardState` + `Move`, create a robot manipulation plan.
11. Simulate basic piece movement.
12. Add/verify physical handling of captures and special moves.
13. Add board/robot visualization.
14. Later add camera-based board perception.
15. Later add a more realistic robot simulation and motion planning.
16. Eventually connect the robot controller to physical hardware.

---

# Architecture

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

## ROS 2 Planning Flow

```text
                  FEN
BoardStateNode ─────────────▶ StockfishNode
      ▲                            │
      │                            │ UCI move
      └────────────────────────────┘
```

ROS messages are currently represented as strings at the node boundaries.

The UCI protocol is kept internal to the Stockfish interface rather than being exposed directly as the ROS message format.

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

`applyMove()` assumes the supplied move is legal. Full move legality remains outside the responsibility of `BoardState`.

---

# FEN

FEN is the interface between `BoardState` and `Stockfish`.

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

`fromFEN()` allows a board state to be initialized from an existing FEN position, which is useful for testing and simulation.

---

# UCI

Stockfish communicates moves using UCI notation.

The `UCI` namespace converts Stockfish's move representation into the project's internal `Move` representation.

```cpp
Move UCI::parseUCI(const std::string&);
```

For example:

```text
e2e4
```

becomes:

```text
e2 → e4
```

This keeps Stockfish's communication format separate from the internal board representation.

---

# BoardStateNode

`BoardStateNode` provides the ROS 2 interface to `BoardState`.

Its responsibilities are:

* Maintain the current `BoardState`
* Receive moves through ROS messages
* Convert incoming move strings into `Move`
* Perform basic move validation
* Apply accepted moves
* Publish the updated board as FEN

The node supports:

* Configurable starting FEN
* Configurable engine color
* Transient-local FEN publication

The node does not perform Stockfish search.

## Current Move Flow

```text
Incoming ROS Move
       │
       ▼
 Parse UCI
       │
       ▼
 Basic Validation
       │
       ▼
 BoardState::applyMove()
       │
       ▼
 FEN::toFEN()
       │
       ▼
 Publish Board FEN
```

Full chess move legality is still delegated to Stockfish / future chess logic. The current node checks basic conditions such as turn and piece ownership before applying a move.

---

# Design Boundary

`BoardState` is responsible for **representing and updating the position**, not for being a chess engine.

It does not handle:

* Move generation
* Move evaluation
* Engine search
* Piece attack tables
* Magic bitboards
* Stockfish logic

`BoardStateNode` is responsible for connecting the board representation to the ROS 2 system.

`StockfishNode` will be responsible for communicating with and controlling the Stockfish process.

The intended flow is:

```text
                BoardStateNode
                 │          ▲
                 │ FEN      │ UCI move
                 ▼          │
              StockfishNode
                 │
                 ▼
              Stockfish
```

The resulting move will eventually be passed to the robot manipulation layer.
