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
12. Implement move diagnosis and legality checking for inferred moves. **Complete**
13. Implement soft observation and decoder to distinguish if a move happened from a camera frame. **Complete**
14. Represent the resulting chess move for robot manipulation.
15. Given `BoardState` + `Move`, create a robot manipulation plan.
16. Simulate basic piece movement.
17. Add/verify physical handling of captures and special moves.
18. Add board/robot visualization.
19. Later add camera-based board perception.
20. Later add a more realistic robot simulation and motion planning.
21. Eventually connect the robot controller to physical hardware.


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

Infer determines what physical board change most likely occurred by comparing the authoritative BoardState with an observed Occupancy.

Its interface is:

namespace Infer {

struct Inference {
    enum class Kind {
        NoChange,
        PieceLifted,
        Candidate,
        Anomaly
    } kind = Kind::NoChange;

    Move move{};
    std::vector<Square> changed;
};

Inference inferMove(const BoardState& b, const Occupancy& o);

}

Inference Results

Kind

Meaning

NoChange

The observed occupancy matches the expected position.

PieceLifted

A piece appears to have been removed from its square but has not yet been placed.

Candidate

The occupancy changes are consistent with a candidate move.

Anomaly

The observed changes cannot be explained by a supported single-move pattern.

move is meaningful when kind == Candidate. The changed vector records the squares that differ from the expected occupancy and can be used for highlighting or debugging.

Inference Flow

BoardState
    │
    ▼
Expected Occupancy
    │
    │ compare
    ▼
Observed Occupancy
    │
    ▼
Changed Squares
    │
    ▼
Infer::inferMove()
    │
    ▼
Inference

Infer identifies a likely physical action; it does not establish that the move is legal under chess rules.

This separation allows the system to recognize an attempted move even when the player moves a piece illegally.

Diagnose

Diagnose checks a candidate move against chess rules and returns a verdict, together with relevant squares that help explain the result.

Its interface is:

namespace Diagnose {

enum class Verdict {
    Legal,
    NoPieceAtSource,
    WrongTurn,
    CapturesOwnPiece,
    IllegalPieceGeometry,
    Blocked,
    LeavesKingInCheck,
    CastlingNotAllowed,
    AmbiguousPromotion
};

struct Diagnosis {
    std::vector<Square> squares;
    Verdict verdict = Verdict::Legal;
};

Diagnosis diagnoseMove(const BoardState& b, const Move& m);

bool moveIsLegal(const BoardState& b, const Move& m);

std::string describe(Verdict v);

}

Diagnosis Results

Verdict

Meaning

Legal

The move passes the implemented legality checks.

NoPieceAtSource

No piece occupies the source square.

WrongTurn

The piece does not belong to the side to move.

CapturesOwnPiece

The destination contains a piece of the moving side.

IllegalPieceGeometry

The move does not match the piece's movement rules.

Blocked

Another piece obstructs the move.

LeavesKingInCheck

The move leaves the moving side's king in check.

CastlingNotAllowed

The attempted castle violates castling requirements.

AmbiguousPromotion

A pawn reaches its promotion rank without a specified promotion piece.

squares identifies relevant squares for the diagnosis, such as a blocking piece or a piece giving check.

moveIsLegal() provides a boolean interface that returns whether diagnoseMove() produces Verdict::Legal.

describe() converts a verdict into a human-readable description.


Diagnose

Diagnose evaluates whether a candidate move satisfies the implemented chess rules.

Its main interface is:

Diagnosis Diagnose::diagnoseMove(
    const BoardState& b,
    const Move& m
);

bool Diagnose::moveIsLegal(
    const BoardState& b,
    const Move& m
);

The Verdict enum includes:

Legal

NoPieceAtSource

WrongTurn

CapturesOwnPiece

IllegalPieceGeometry

Blocked

LeavesKingInCheck

CastlingNotAllowed

AmbiguousPromotion

Diagnosis also records relevant squares, and Diagnose::describe() converts a verdict into a human-readable description.

Diagnosis is separate from perception. A physically recognizable attempted move may still be illegal


SoftObserve Decoding (Part A of implementation path from yesterday) use cases:
      Example 1: One stray square kills a good frame

      The human plays Nf3. After the hand leaves, the model outputs:

      Square	P(empty)	P(white)	P(black)
      g1	0.97	0.02	0.01
      f3	0.02	0.96	0.02
      a6	0.46	0.02	0.52

      a6 was empty all along. The model has a slight glare problem there and leans black, 52% to 46%.

      Hard-label path: argmax says a6 is black. changed is now {g1, f3, a6}, and a6 is a black piece that appeared. That sets wrongColor, so inferMove returns Anomaly. Without SoftObs, you either report an error to the player (“that’s not a legal move”), which blames them for something they didn’t do, or you throw the frame away.

      Soft path: the scorer runs over legal moves. Nf3 expects g1 empty and f3 white. Its delta score on those two squares is strongly positive. The a6 squares don’t enter the comparison at all, because no legal white move changes a6, so every candidate expects a6 unchanged and scores the same there. Nf3 wins by a wide margin and you commit.

      The 52% black on a6 was real noise, but it was irrelevant to the decision, and SoftObs is what lets the decoder ignore it. This is the 0.98^64 problem in concrete form: the hard path needs all 64 squares right, while the soft path needs only the squares that distinguish the candidates.

      Example 2: A capture where the fast path commits the wrong move

      White has a knight on e5. Black has pawns on d7 and f7. Black’s d7 pawn captures nothing here, but suppose the black bishop on c8 and pawn on d7 both have a legal capture-like footprint on e6... let me use a cleaner setup.

      Black to move. A black pawn on d5 can capture a white knight on e4 (dxe4), and a black bishop on g6 can also capture it (Bxe4). Both end with a black piece on e4 and e4’s previous white piece gone, so the destination footprint is identical. They differ only in which origin square emptied: d5 or g6.

      After the hand leaves:

      Square	P(empty)	P(black)
      e4	0.01	0.97 (flipped from white)
      d5	0.55	0.45
      g6	0.40	0.60

      The model is unsure which origin square emptied. Maybe the piece’s shadow lingers on both.

      Hard-label path: argmax says d5 is empty and g6 is black. The diff shows a black piece left d5 and e4 flipped to black, which is a clean capture pattern. inferMove returns Candidate dxe4 with no hint of doubt, you commit, and if the bishop actually moved, BoardState now has the wrong piece on e4 and a ghost bishop still on g6. Every later move is evaluated against a corrupted position, and nothing downstream flags it.

      Soft path: both captures score about equally on d5 and g6, because the evidence is 55/45 and 40/60. The margin test fails and the result is Ambiguous between dxe4 and Bxe4. The system asks, “Did the pawn or the bishop capture?” The answer commits the right move and gets logged as a training label for exactly this kind of shadow-on-adjacent-squares failure.

LegalMoves

LegalMoves provides a shared function for enumerating the legal moves available from a position.

Diagnose::legalMoves(board);

Each result includes a move and a promotionUnspecified flag. A promotion is represented once rather than as four separate moves for queen, rook, bishop, and knight.

An empty result indicates that no legal moves are available, which may mean checkmate or stalemate.

LegalMoves is intended to provide a common source of legal candidates for both MoveDecoder and Arbiter, avoiding duplicated move enumeration.

SoftObs

SoftObs represents a probabilistic observation of the board.

Instead of assigning exactly one label to each square, it stores probabilities for the square being empty, occupied by white, or occupied by black.

For example:

Square

P(empty)

P(white)

P(black)

g1

0.97

0.02

0.01

f3

0.02

0.96

0.02

a6

0.46

0.02

0.52

This representation preserves uncertainty from the perception model.

A hard-label observation would classify a6 as black, even though the probabilities indicate that the prediction is uncertain. A decoder that uses only hard labels may reject the entire observation because of this unrelated error.

With SoftObs, candidate moves can instead be compared using the probability evidence for the squares each move should change.

The implementation also provides hard labels through hardLabels(). These are used by the Arbiter as a fallback when the probabilistic decoder cannot explain the observation.

MoveDecoder

MoveDecoder compares the observed probabilities against candidate legal moves.

Its purpose is to determine which legal move best explains the observed change from the expected board state.

The decoder uses LegalMoves to enumerate candidates and scores their expected occupancy changes against SoftObs.

Rather than requiring every square to be classified correctly, the decoder can favor a move whose source and destination changes are strongly supported by the observations.

Decoding Results

The decoder returns a Decoding result representing one of the following outcomes:

Result

Meaning

Unusable

The observation cannot currently be used.

NoChange

The observation is consistent with the expected board.

Confident

One candidate move is sufficiently better supported than the alternatives.

Ambiguous

Multiple candidate moves remain plausible.

NoLegalExplanation

No legal move adequately explains the observation.

The soft-scoring path is intended to reduce two failure modes:

Rejecting a good frame because of an unrelated, misclassified square.

Silently committing the wrong move when multiple legal moves have similar observation scores.

When two candidate moves score too closely, the decoder reports ambiguity instead of choosing arbitrarily.

Arbiter

Arbiter consumes a Decoding result and decides what the system should do next.

It is a pure-logic component: it receives the board, the decoding result, and the latest hard-label observation, then returns an action.

Decoding d = decoder.observe(soft, usable);

Action a = arbiter.step(
    board,
    d,
    soft.hardLabels()
);

Decision Behavior

Decoder result

Arbiter behavior

Unusable

Wait for a usable observation; request a clearer view after repeated failures.

NoChange

Wait for the board to change.

Confident

Require repeated confirmation, then revalidate before committing.

Ambiguous

Continue observing, then ask the human to choose between candidates if ambiguity persists.

NoLegalExplanation

Continue observing, then use hard-label inference and diagnosis to distinguish an illegal move from a perception problem.

The Arbiter can return actions such as:

Commit

Wait

AskPromotion

ReportIllegal

AskCheckBoard

Ambiguous results can also prompt the user to identify which candidate move occurred.

The current default thresholds include two consecutive frames for confirming a confident move, five frames before asking about ambiguity, six frames before investigating a persistent lack of legal explanation, and thirty consecutive unusable frames before requesting a clearer view.

These are initial tuning values, not yet validated performance settings.

Avoiding Incorrect Blame

Only ReportIllegal explicitly tells the player that an attempted move is illegal.

If the perception evidence and hard-label fallback disagree, or the diagnosis does not reliably establish an illegal move, the Arbiter should request a board check instead.

Each action is intended to be issued once per situation. After the move is committed or the human responds, the caller resets the Arbiter and decoder.

Game-over handling is not yet integrated into this decision flow. The caller must check whether LegalMoves returns any moves before treating NoLegalExplanation as a perception failure.