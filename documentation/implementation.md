# Implementation

## Current Implementation Order

1. Create a chess board/state representation. **Complete**
2. Convert the board state into FEN. **Complete**
3. Implement UCI move parsing. **Complete**
4. Implement the `BoardStateNode` ROS 2 interface. **Complete**
5. Add automated tests for BoardState and BoardStateNode. **Complete**
6. Implement the UCI protocol library for Stockfish communication. **Complete**
7. Integrate Stockfish with ROS 2. **In progress**
8. Add user-configurable Stockfish skill/strength.
9. Complete the FEN → Stockfish → UCI move → BoardState loop.
10. Represent the resulting chess move for robot manipulation.
11. Given `BoardState` + `Move`, create a robot manipulation plan.
12. Simulate basic piece movement.
13. Add/verify physical handling of captures and special moves.
14. Add board/robot visualization.
15. Later add camera-based board perception.
16. Later add a more realistic robot simulation and motion planning.
17. Eventually connect the robot controller to physical hardware.

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

Stockfish communicates using the Universal Chess Interface (UCI) protocol.

The `UCI` namespace handles both construction of commands sent to Stockfish and parsing of messages received from Stockfish.

UCI command builders produce strings that can be sent to the Stockfish process.

Stockfish responses are parsed into typed message structures using:

```cpp
using Message = std::variant<
    UciOk,
    ReadyOk,
    IdLine,
    Option,
    Info,
    BestMove,
    Unknown
>;
```

The main parser is:

```cpp
Message UCI::parseLine(const std::string&);
```

For example:

```text
bestmove e2e4
```

is parsed into a `BestMove` message containing the move string.

The UCI layer uses move strings such as:

```text
e2e4
e7e8q
```

rather than the internal `Move` type. Conversion between UCI move strings and `Move` remains outside the UCI protocol layer.

## UCI Responsibilities

The UCI library handles:

* Building Stockfish commands
* Parsing `uciok`
* Parsing `readyok`
* Parsing `id`
* Parsing `option`
* Parsing*
