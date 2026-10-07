# Implementation

## Current Implementation Order

1. Create a chess board/state representation. **Complete**
2. Convert the board state into FEN. **Complete**
3. Implement UCI move parsing. **Complete**
4. Implement the `BoardStateNode` ROS 2 interface. **Complete**
5. Add automated tests for BoardState and BoardStateNode. **Complete**
6. Implement the UCI protocol library for Stockfish communication. **Complete**
7. Integrate Stockfish with ROS 2. **Complete**
8. Add user-configurable Stockfish skill/strength. **In Progress**
9. Complete the FEN → Stockfish → UCI move → BoardState loop. **Complete**
10. Implement Occupancy as the board perception representation. **Complete**
11. Implement board-state change inference from observed occupancy. **Complete**
12. Implement move diagnosis and legality checking for inferred moves.
13. Represent the resulting chess move for robot manipulation.
14. Given `BoardState` + `Move`, create a robot manipulation plan.
15. Simulate basic piece movement.
16. Add/verify physical handling of captures and special moves.
17. Add board/robot visualization.
18. Later add camera-based board perception.
19. Later add a more realistic robot simulation and motion planning.
20. Eventually connect the robot controller to physical hardware.


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

# EngineProcess

`EngineProcess` manages the Stockfish child process and its communication pipes.

It is independent of UCI and ROS. Its only responsibility is moving data between the ROS node and the Stockfish process.

## Process Communication

Two pipes are used for normal communication:

```text
                 Stockfish
                ┌──────────┐
                │          │
       stdin ◀──┤          ├──▶ stdout
                └──────────┘
                    ▲  │
                    │  │
                    │  │
              toEngine_  fromEngine_
                    │  │
                    ▼  ▼
               StockfishNode
```

The parent writes UCI commands through `toEngine_` and reads Stockfish output through `fromEngine_`.

A third pipe is used during startup to report an `exec()` failure from the child process.

## `writeLine()`

```cpp
void writeLine(const std::string&);
```

Sends one complete command to Stockfish.

A newline is appended automatically.

The function may be called from multiple threads and uses a mutex to prevent concurrent writes from being mixed together.

The underlying `write()` call may write only part of the command, so `writeLine()` continues writing until the entire command has been sent.

## `readLine()`

```cpp
bool readLine(std::string&);
```

Reads one complete Stockfish output line at a time.

The underlying `read()` call does not guarantee one line per call. It may return:

* Part of a line
* One line
* Multiple lines

`buffer_` stores unread engine output between calls.

```text
Stockfish output
       │
       ▼
     read()
       │
       ▼
   buffer_
       │
       ▼
   readLine()
       │
       ▼
 complete line
```

`readLine()` is called repeatedly by the Stockfish reader thread:

```cpp
std::string line;

while (engine.readLine(line)) {
    // process line
}
```

The function returns `false` when the engine reaches EOF or the read fails.

## Shutdown

`shutdown()` closes Stockfish's stdin, allowing the process to exit normally.

It waits briefly for a clean exit. If Stockfish does not exit, the process is terminated and reaped.

The destructor calls `shutdown()` to clean up the Stockfish process and its remaining pipe.

# StockfishNode

`StockfishNode` provides the ROS 2 interface to the Stockfish engine.

It connects the `EngineProcess` and `UCI` layers while managing the engine's state and deciding how to respond to Stockfish messages.

Its responsibilities are:

* Start and configure Stockfish
* Perform the UCI handshake
* Receive FEN positions through ROS 2
* Start Stockfish searches
* Handle engine responses
* Publish the selected UCI move
* Manage new positions arriving while the engine is busy

## ROS Parameters

The node currently supports:

```text
engine_path
threads
hash_mb
skill_level
move_time_ms
fen_topic
move_topic
```

The default search time is 1000 ms.

## UCI Handshake

When the node starts, it sends:

```text
uci
```

After receiving `uciok`, it configures Stockfish and sends:

```text
setoption name Threads value ...
setoption name Hash value ...
setoption name Skill Level value ...
ucinewgame
isready
```

After receiving `readyok`, the engine is ready to search.

## Search Flow

A FEN received from the ROS 2 `fen_topic` is passed to Stockfish as:

```text
position fen <FEN>
go movetime <time>
```

Stockfish then produces `info` messages followed by a `bestmove`.

The `bestmove` is published through the `move_topic`.

```text
FEN
 │
 ▼
StockfishNode
 │
 ├── position fen ...
 ├── go movetime ...
 │
 ▼
Stockfish
 │
 ├── info ...
 ├── info ...
 └── bestmove e2e4
             │
             ▼
       StockfishNode
             │
             ▼
      ROS move topic
```

## Engine State

The node tracks the engine using several states:

```text
WaitingUciOk
      │
      ▼
WaitingReadyOk
      │
      ▼
    Idle
      │
      ▼
 Searching
      │
      ▼
    Idle
```

A FEN received while the engine is starting is stored as `pendingFen_`.

A FEN received while searching causes the current search to be stopped. Its `bestmove` is then discarded because it belongs to an outdated position.

Only the newest pending FEN is retained.

## Engine Output

`StockfishNode` passes each line from `EngineProcess` to:

```cpp
uci::parseLine(line)
```

The resulting `std::variant` is handled by `handleParsedOutput()`.

```text
Stockfish stdout
       │
       ▼
EngineProcess::readLine()
       │
       ▼
UCI::parseLine()
       │
       ▼
UCI::Message
       │
       ├── UciOk
       ├── ReadyOk
       ├── IdLine
       ├── Option
       ├── Info
       ├── BestMove
       └── Unknown
       │
       ▼
StockfishNode handler
```

`StockfishNode` decides what action to take for each message type, while the UCI library only interprets the protocol.

## Current Move Flow

The complete engine-side flow is now:

```text
BoardStateNode
      │
      │ FEN
      ▼
StockfishNode
      │
      ▼
UCI command builders
      │
      ▼
EngineProcess
      │
      ▼
Stockfish
      │
      │ bestmove
      ▼
EngineProcess
      │
      ▼
UCI parser
      │
      ▼
StockfishNode
      │
      │ UCI move
      ▼
BoardStateNode
```

The remaining work is connecting this completed Stockfish flow to the robot manipulation layer.

Occupancy

Occupancy is a lossy projection of BoardState used to represent what the physical-board perception system can observe.

Unlike BoardState, it does not attempt to represent a complete chess position.

Each square contains only:

enum class Cell : uint8_t {
    Empty,
    White,
    Black
};

The occupancy board therefore answers only:

Is there a piece on this square, and if so, what color is it?

BoardState vs Occupancy

	BoardState	Occupancy
Piece type	Yes	No
Piece color	Yes	Yes
Empty squares	Yes	Yes
Side to move	Yes	No
Castling rights	Yes	No
En passant	Yes	No
Move counters	Yes	No

For example, BoardState can distinguish:

White Pawn
White Knight
White Bishop
White Rook
White Queen
White King

while Occupancy represents all of them simply as:

White

The same applies to black pieces.

Why Occupancy Exists

A physical perception system may be able to reliably determine that a square is occupied without reliably identifying the exact chess piece.

For example:

Camera
   │
   ▼
Square e4
   │
   ▼
"White piece detected"

This can be represented as:

Cell::White

The camera cannot directly observe information such as:

Side to move
Castling rights
En passant target
Move counters

Therefore this information remains in BoardState.

Constructing Occupancy

Occupancy can be constructed from an existing BoardState:

Occupancy occupancy(boardState);

This performs a lossy conversion:

BoardState
    │
    │ discard piece type
    ▼
Occupancy

The reverse conversion is not generally possible because multiple BoardState positions can produce the same Occupancy.

For example:

White Pawn on e4 ──┐
White Bishop on e4 ├──▶ White on e4
White Queen on e4 ─┘

Therefore Occupancy should be treated as an observation rather than an authoritative chess position.

Board State Inference

The purpose of Occupancy is to allow the perception system to determine what changed on the physical board.

The system compares an observed Occupancy against the expected position from BoardState.

             BoardState
                  │
                  ▼
             Occupancy
             (expected)
                  │
                  │ compare
                  ▼
Camera ─────▶ Occupancy
(observed)         │
                   ▼
             Changed squares
                   │
                   ▼
            Candidate Move

The existing BoardState provides information that Occupancy does not contain.

For example, if:

e2 = White
e4 = Empty

in the expected occupancy and the camera observes:

e2 = Empty
e4 = White

the system can infer:

e2 → e4

The piece type can then be obtained from the existing BoardState:

BoardState:
e2 = White Pawn

Occupancy:
e2 → Empty
e4 → White

Inference:
White Pawn e2 → e4

The inference layer should identify what physical change most likely occurred. It should not replace BoardState or independently maintain a second chess position.