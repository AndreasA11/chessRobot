# Implementation

## 1. Implementation Overview

The chess robot is organized into three main stages:

1. **Perception:** Determine the current physical board state.
2. **Planning:** Use Stockfish to select a chess move.
3. **Action:** Convert the selected move into a robot manipulation plan and execute it.

Development begins with the chess state representation and engine integration, followed by board perception and move recognition, and finally robot manipulation and simulation.

### 1.1 Architecture

```text
       PERCEPTION              PLANNING                ACTION

    ┌─────────────┐        ┌─────────────┐        ┌─────────────┐
    │ Chess Board │        │  Stockfish  │        │    Robot    │
    │             │        │             │        │             │
    │   Observe   │───────▶│ Decide Move │───────▶│ Execute Move│
    └─────────────┘        └─────────────┘        └─────────────┘
           │                      │                      │
           ▼                      ▼                      ▼
       BoardState               Move               Physical Board
                                                        Change
```

### 1.2 Current Implementation Order

| Component | Status |
|---|---|
| BoardState representation | Complete |
| FEN conversion | Complete |
| UCI move parsing | Complete |
| BoardStateNode ROS 2 interface | Complete |
| BoardState and BoardStateNode tests | Complete |
| UCI protocol library | Complete |
| Stockfish ROS 2 integration | Complete |
| FEN → Stockfish → UCI move → BoardState loop | Complete |
| User-configurable Stockfish strength | In progress |
| Occupancy representation | Complete |
| Board-state change inference | Complete |
| Move diagnosis and legality checking | Complete |
| Soft observations and move decoding | Complete |
| Robot manipulation representation | Not started |
| Robot manipulation planning | Not started |
| Basic movement simulation | Not started |
| Captures and special-move handling | Not started |
| Board and robot visualization | Not started |
| Camera-based perception | Planned |
| Realistic simulation and motion planning | Planned |
| Physical hardware integration | Future work |

*Complete indicates that a component has been implemented. It does not necessarily mean that the complete system has been validated end to end.*

---

# 2. Chess State Representation

## 2.1 BoardState

`BoardState` is the authoritative representation of the current chess position. It stores the information required to describe a complete chess position and apply moves.

The board uses an 8×8 array of `Piece` values:

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

| Array index | Chess square |
|---|---|
| `board_[0][0]` | a1 |
| `board_[1][4]` | e2 |
| `board_[3][4]` | e4 |
| `board_[7][7]` | h8 |

The implementation uses a square-based representation rather than bitboards. This keeps the internal representation straightforward and makes it convenient to work with individual squares during perception and robot planning.

Stockfish handles engine-side chess calculations such as move generation, attack detection, and position evaluation.

### Stored position information

In addition to piece placement, `BoardState` stores:

- Side to move.
- Castling rights.
- En passant target square.
- Halfmove clock.
- Fullmove number.

These fields are necessary to represent a chess position accurately and convert it to and from FEN.

### Public interface

The main operations include:

```cpp
Piece at(Square) const;
Color sideToMove() const;

MoveInfo describeMove(const Move&) const;
void applyMove(const Move&);
```

**`at()`**

Returns the piece occupying the requested square.

**`sideToMove()`**

Returns the color whose turn it is.

**`describeMove()`**

Determines the information required to apply a move, including:

- The piece being moved.
- The captured piece.
- The capture square.
- Castling information.
- Promotion information.

This information is determined before modifying the board.

**`applyMove()`**

Updates the position after a move. It handles the state changes associated with the supplied move.

`applyMove()` assumes the move is legal. Full chess move validation is intentionally handled outside `BoardState`, keeping state mutation separate from move diagnosis and legality checking.

## 2.2 FEN Conversion

Forsyth–Edwards Notation (FEN) provides the interface between `BoardState` and Stockfish.

The conversion layer is implemented in the `FEN` namespace:

```cpp
BoardState FEN::fromFEN(const std::string&);
std::string FEN::toFEN(const BoardState&);
```

**`toFEN()`** converts the current `BoardState` into a FEN string so the position can be passed to Stockfish.

**`fromFEN()`** constructs a `BoardState` from an existing FEN string. This is useful for initializing positions in tests and simulations.

The engine interaction follows this sequence:

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

---

# 3. Stockfish Integration

The Stockfish integration is divided into three layers:

- **UCI:** Constructs and parses protocol messages.
- **EngineProcess:** Manages the Stockfish operating-system process.
- **StockfishNode:** Connects the engine to ROS 2 and manages its state.

This separation keeps protocol handling, process management, and ROS-specific behavior independent.

## 3.1 UCI Protocol Library

Stockfish communicates through the Universal Chess Interface (UCI) protocol.

The `UCI` namespace is responsible for constructing commands sent to Stockfish and parsing messages received from it.

### Message representation

Incoming protocol messages are parsed into typed structures and represented using a `std::variant`:

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

For example, the Stockfish response:

```text
bestmove e2e4
```

is parsed into a `BestMove` message containing the move string.

### Responsibilities

The UCI library handles:

- Constructing UCI commands.
- Parsing `uciok`.
- Parsing `readyok`.
- Parsing engine identification messages (`id`).
- Parsing available engine options (`option`).
- Parsing search information (`info`).
- Parsing selected moves (`bestmove`).
- Representing unrecognized messages as `Unknown`.

The UCI layer uses protocol-level move strings, such as:

```text
e2e4
e7e8q
```

It does not directly depend on the internal `Move` representation. Conversion between UCI strings and internal chess moves is handled outside the protocol parser.

**Design decision:** UCI is independent of ROS 2 and process management. This makes the protocol implementation reusable and allows it to be tested without launching a ROS node or Stockfish process.

## 3.2 EngineProcess

`EngineProcess` manages the Stockfish child process and the pipes used to communicate with it.

It is independent of both ROS 2 and the UCI protocol. Its responsibility is limited to starting, communicating with, and shutting down the external process.

### Process communication

Two pipes handle normal communication:

```text
                  Stockfish
                ┌──────────┐
                │          │
      stdin ◀───┤          ├───▶ stdout
                └──────────┘
                    ▲  │
                    │  │
                toEngine_
                fromEngine_
                    │  │
                    ▼  ▼
                StockfishNode
```

The parent process writes commands through `toEngine_` and reads output through `fromEngine_`.

A third pipe is used during startup to report an `exec()` failure from the child process.

### `writeLine()`

```cpp
void writeLine(const std::string&);
```

Sends a complete command to Stockfish.

The function appends a newline automatically. Because it may be called from multiple threads, a mutex prevents concurrent writes from being mixed together.

The implementation also handles partial writes. A system `write()` call is not guaranteed to send the entire command, so `writeLine()` continues writing until all bytes have been sent.

### `readLine()`

```cpp
bool readLine(std::string&);
```

Reads one complete line of Stockfish output.

A system `read()` call does not guarantee that the returned data corresponds to one complete line. It may contain part of a line, one line, or multiple lines.

`buffer_` retains unread output between calls. `readLine()` extracts a complete line when one is available.

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
  Complete line
```

The Stockfish reader thread repeatedly calls `readLine()`:

```cpp
std::string line;

while (engine.readLine(line)) {
    // Process the complete output line.
}
```

The function returns `false` when the engine reaches EOF or reading fails.

**Threading decision:** Multiple threads may send commands, but output is consumed by a single reader thread. This keeps the read side of the process interface serialized.

### Shutdown

`shutdown()` closes Stockfish's standard input so the process can exit normally.

It waits briefly for a clean exit. If the process does not exit, it is terminated and reaped.

The destructor calls `shutdown()` to clean up the process and its remaining resources.

## 3.3 StockfishNode

`StockfishNode` provides the ROS 2 interface to Stockfish.

It connects the `EngineProcess` and UCI layers while managing engine configuration, search requests, and incoming responses.

### Responsibilities

- Start and configure Stockfish.
- Perform the UCI handshake.
- Receive FEN positions through ROS 2.
- Start engine searches.
- Handle Stockfish responses.
- Publish the selected UCI move.
- Manage new positions arriving while a search is active.

### ROS 2 parameters

| Parameter | Purpose |
|---|---|
| `engine_path` | Stockfish executable path |
| `threads` | Number of engine threads |
| `hash_mb` | Engine hash size in MB |
| `skill_level` | Stockfish skill level |
| `move_time_ms` | Search duration |
| `fen_topic` | Input topic for FEN positions |
| `move_topic` | Output topic for UCI moves |

The default search duration is 1000 ms.

### UCI initialization

When the node starts, it sends:

```text
uci
```

After receiving `uciok`, it configures Stockfish:

```text
setoption name Threads value ...
setoption name Hash value ...
setoption name Skill Level value ...
ucinewgame
isready
```

The engine is considered ready after receiving `readyok`.

### Search flow

When a FEN position arrives on `fen_topic`, the node sends:

```text
position fen <FEN>
go movetime <time>
```

Stockfish responds with search information and eventually a `bestmove` message. The selected move is published on `move_topic`.

```text
FEN topic
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

### Engine state management

The node tracks its lifecycle using the following states:

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

The node must also handle positions arriving while the engine is starting or searching.

**While starting:** A received FEN is stored in `pendingFen_` until the engine becomes ready.

**While searching:** A new FEN causes the current search to be stopped. The resulting `bestmove` is discarded because it belongs to an outdated position.

Only the newest pending FEN is retained. This prevents the engine from publishing a move calculated for an obsolete board state.

### Handling engine output

Each line read by `EngineProcess` is passed to:

```cpp
uci::parseLine(line);
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

The UCI library interprets the protocol message. `StockfishNode` decides what action to take based on its current state and the message type.

### Complete engine move flow

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

ROS messages currently use strings at the node boundaries. The UCI protocol remains internal to the Stockfish interface rather than being exposed as the ROS message format.

---

# 4. Board Perception and Move Recognition

The perception system must determine what changed on the physical chessboard and identify the move that most likely caused that change.

Three responsibilities are separated:

1. **Occupancy:** Represent what the perception system observes.
2. **Inference and decoding:** Determine which move best explains the observation.
3. **Diagnosis:** Validate a candidate move against chess rules.

This prevents perception uncertainty from being confused with chess legality.

## 4.1 Occupancy

`Occupancy` is a lossy projection of `BoardState`. It represents only whether a square is empty or occupied by a white or black piece.

```cpp
enum class Cell : uint8_t {
    Empty,
    White,
    Black
};
```

Each square contains one `Cell` value.

### Information represented

| Information | BoardState | Occupancy |
|---|---:|---:|
| Empty squares | Yes | Yes |
| Piece color | Yes | Yes |
| Piece type | Yes | No |
| Side to move | Yes | No |
| Castling rights | Yes | No |
| En passant target | Yes | No |
| Move counters | Yes | No |

### Why Occupancy exists

A physical perception system may reliably determine that a square contains a piece without reliably identifying the exact chess piece.

For example, a camera may determine that a white piece occupies e4. This becomes `Cell::White`.

The camera cannot directly observe information such as the side to move, castling rights, or en passant target. That information remains in the authoritative `BoardState`.

### Construction

An `Occupancy` can be constructed from an existing board state:

```cpp
Occupancy occupancy(boardState);
```

This conversion discards piece-type information:

```text
BoardState
    │
    │ Discard piece type
    ▼
Occupancy
```

The reverse conversion is not generally possible. A white pawn, bishop, or queen on the same square all produce the same occupancy value.

**Design decision:** `Occupancy` is an observation representation, not a second authoritative chess state. The perception system must not independently maintain a complete chess position.

## 4.2 Hard-Label Inference

The `Infer` namespace provides a hard-label method for identifying a physical board change.

It compares the expected occupancy derived from `BoardState` against an observed `Occupancy`.

```text
                BoardState
                    │
                    ▼
             Expected Occupancy
                    │
                    │ Compare
                    ▼
Camera ─────▶ Observed Occupancy
                    │
                    ▼
              Changed Squares
                    │
                    ▼
               Candidate Move
```

For example, suppose the expected occupancy contains:

```text
e2 = White
e4 = Empty
```

The observed occupancy contains:

```text
e2 = Empty
e4 = White
```

The system can infer that a white piece moved from e2 to e4. The piece type comes from the existing `BoardState`, which identifies the piece on e2 as a white pawn.

### Interface

```cpp
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
```

### Results

| Result | Meaning |
|---|---|
| `NoChange` | Observed occupancy matches the expected position. |
| `PieceLifted` | A piece appears to have been removed but not yet placed. |
| `Candidate` | The changes match a supported single-move pattern. |
| `Anomaly` | The changes cannot be explained by a supported move pattern. |

`move` is meaningful when the result is `Candidate`. `changed` records the squares that differ from the expected occupancy and can be used for debugging or visualization.

**Design decision:** Hard-label inference is retained as a fallback. It is not the primary move-recognition method because one unrelated square-classification error can cause an otherwise valid observation to be rejected.

## 4.3 Diagnose: Move Validation

`Diagnose` checks whether a candidate move satisfies the implemented chess rules.

Its interface is:

```cpp
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
```

### Diagnosis results

| Verdict | Meaning |
|---|---|
| `Legal` | The move passes the implemented legality checks. |
| `NoPieceAtSource` | No piece occupies the source square. |
| `WrongTurn` | The piece does not belong to the side to move. |
| `CapturesOwnPiece` | The destination contains a piece of the moving side. |
| `IllegalPieceGeometry` | The move does not match the piece's movement rules. |
| `Blocked` | Another piece obstructs the move. |
| `LeavesKingInCheck` | The move leaves the moving side's king in check. |
| `CastlingNotAllowed` | The attempted castle violates castling requirements. |
| `AmbiguousPromotion` | A pawn reaches its promotion rank without a specified promotion piece. |

`squares` identifies relevant squares associated with the diagnosis, such as a blocking piece or a piece giving check.

`moveIsLegal()` returns whether `diagnoseMove()` produces `Verdict::Legal`. `describe()` converts a verdict into a human-readable description.

**Design decision:** Diagnosis is separate from perception and inference. A move can be physically recognizable while still being illegal under chess rules.

## 4.4 LegalMoves

`LegalMoves` provides a shared function for enumerating legal moves from a `BoardState`.

```cpp
Diagnose::legalMoves(board);
```

Each result includes a move and a `promotionUnspecified` flag.

Promotions are represented once rather than as four separate candidate moves for queen, rook, bishop, and knight. The promotion piece can be requested separately when the move is committed.

An empty result means that no legal moves are available. This may indicate checkmate or stalemate, so the caller must distinguish game-over conditions from perception failures.

**Design decision:** `LegalMoves` is the common source of legal candidates for both `Decode` and `Arbiter`. This avoids duplicating move enumeration across the perception decision pipeline.

---

# 5. Probabilistic Board Observation

## 5.1 SoftObs

`SoftObs` represents the probabilistic observation of one camera frame.

Instead of assigning exactly one label to each square, it stores the probability that the square is empty, occupied by a white piece, or occupied by a black piece.

### Representation

```cpp
struct SoftObs {
    std::array<float, 192> p;
};
```

The array contains 192 floats:

- 64 board squares.
- 3 probabilities per square.
- Probability classes: Empty, White, Black.

The indexing convention is:

```text
index = (rank * 8 + file) * 3 + classIndex

classIndex:
    0 = Empty
    1 = White
    2 = Black
```

This matches the proposed `BoardObservation` message layout and uses row-major chess order from a1 to h8.

### Probability access

`classIndex()` maps a `Cell` value to its probability-array index.

`prob()` retrieves the probability for a specific square and class:

```cpp
float prob(Square s, Cell c) const;
```

`set()` writes all three probabilities for a square:

```cpp
void set(
    Square s,
    float pEmpty,
    float pWhite,
    float pBlack
);
```

For example:

| Square | P(empty) | P(white) | P(black) |
|---|---:|---:|---:|
| g1 | 0.97 | 0.02 | 0.01 |
| f3 | 0.02 | 0.96 | 0.02 |
| a6 | 0.46 | 0.02 | 0.52 |

This representation preserves uncertainty from the perception model.

### Hard-label conversion

`hardLabels()` converts the probabilities into an `Occupancy` using argmax for each square.

The tie-breaking behavior is:

- Empty wins ties against White or Black.
- White wins a tie against Black when White is greater than Empty.
- Black is selected only when it is greater than both Empty and White.

This provides the hard-label observation used by `Infer::inferMove()` as a fallback.

The original probabilities remain available for probabilistic scoring.

### Constructing observations from hard labels

`fromHard()` creates a `SoftObs` from an existing `Occupancy`:

```cpp
static SoftObs fromHard(
    const Occupancy& o,
    float confidence = 0.99f
);
```

The selected class receives the supplied confidence. The remaining probability is divided equally between the other two classes:

```text
rest = (1 - confidence) / 2
```

This is useful for unit tests and the noise simulator. A known board can be converted into soft evidence and then corrupted according to a configurable noise model.

**Design decision:** `SoftObs` is independent of the camera classifier. It defines the probability representation consumed by the decoder, allowing tests and simulated observations to use the same interface as real perception.

## 5.2 Why Probabilistic Observations?

The soft-observation design addresses two important failure modes.

**Example 1: An unrelated misclassified square**

A player moves a knight from g1 to f3.

The observation strongly supports:

- g1 becoming empty.
- f3 becoming occupied by white.

However, a6 is incorrectly classified as black due to glare.

A hard-label comparison sees three changed squares and may reject the frame as an anomaly.

The soft decoder evaluates evidence relevant to legal candidate moves. Since a6 is unchanged by every candidate, its probability evidence does not distinguish the candidates. The strong evidence at g1 and f3 allows Nf3 to win.

**Example 2: An ambiguous capture**

Suppose two legal moves produce the same destination occupancy but differ in their source squares:

- A black pawn on d5 captures on e4.
- A black bishop on g6 captures on e4.

The destination evidence strongly supports a black piece on e4, but the model is uncertain about whether d5 or g6 became empty.

A hard-label decoder may choose one source square and commit the wrong move.

The soft decoder retains the uncertainty. If the two candidate scores are too close, it returns `Ambiguous` rather than selecting arbitrarily.

This prevents the system from silently corrupting its authoritative `BoardState`.

**Design decision:** The decoder should use the evidence that distinguishes legal candidate moves, rather than requiring all 64 squares to be classified correctly.

The benefit of this approach must still be measured against hard-label inference using the planned noise simulator.

---

# 6. Decode

`Decode` determines which legal move best explains a sequence of soft observations.

It does not modify or commit the `BoardState`. The board remains authoritative until the Arbiter returns an action that the caller accepts.

## 6.1 Configuration

The decoder uses `DecoderConfig` to control scoring and candidate selection:

```cpp
struct DecoderConfig {
    double probFloor = 1e-3;
    double minScore = 3.0;
    double margin = 3.0;
};
```

| Parameter | Default | Purpose |
|---|---:|---|
| `probFloor` | 0.001 | Minimum probability used before taking logarithms. |
| `minScore` | 3.0 | Minimum evidence required relative to the no-move hypothesis. |
| `margin` | 3.0 | Required score difference between the best and second-best candidate. |

### Probability floor

Probabilities are clamped to at least `probFloor` before logarithms are taken.

This prevents a single confidently incorrect square from producing an arbitrarily large negative score.

At the default floor of \(10^{-3}\), the maximum penalty from one such probability is approximately:

\[
\ln(1/0.001) \approx 6.9
\]

### Minimum score

`minScore` controls how much evidence is required for a candidate move to be proposed.

A move must beat the no-move hypothesis by at least this amount. Unexplained changes are also evaluated against this threshold.

The default value of 3.0 nats corresponds to approximately a 20:1 likelihood ratio.

### Candidate margin

`margin` controls how much better the best candidate must score than the runner-up before the result is considered confident.

If the difference is too small, the decoder reports ambiguity rather than choosing between similarly supported moves.

## 6.2 Candidate Representation

Each candidate stores:

```cpp
struct Delta {
    Square sq;
    Cell was;
    Cell after;
};

struct Candidate {
    Move move;
    std::vector<Delta> footprint;
    bool promotionUnspecified = false;
    double score = 0.0;
};
```

A `Delta` describes one square affected by a candidate move:

- `sq`: the affected square.
- `was`: its value in the committed position.
- `after`: its expected value after the candidate move.

The candidate's `footprint` contains only squares that change when the move is played.

For example, a normal move from e2 to e4 has a footprint containing:

```text
e2: White → Empty
e4: Empty → White
```

Unchanged squares are excluded.

**Design decision:** Scoring only the changed squares allows unrelated perception errors to have less influence on move selection. Squares that remain unchanged across all candidates do not help distinguish those candidates.

`promotionUnspecified` records whether the candidate is a promotion whose promoted piece has not yet been selected.

`score` stores the accumulated log-likelihood ratio against the no-move hypothesis.

## 6.3 Accumulated Evidence

The decoder maintains evidence across multiple frames.

Its internal state includes:

```cpp
DecoderConfig cfg_;
std::vector<Candidate> cands_;
int frames_ = 0;

std::array<Cell, 64> was_;
std::array<std::array<double, 3>, 64> changeLR_;
```

`was_` stores the committed-position cell for each square, using index:

```text
rank * 8 + file
```

`changeLR_` stores accumulated log-ratio evidence for each possible class at each square. It is used to distinguish an unchanged board from an observation that contains changes not explained by any legal candidate.

Each candidate separately accumulates its score across observations.

This allows the decoder to combine evidence from multiple frames instead of making a decision based on a single image.

## 6.4 Public Interface

```cpp
class Decode {
public:
    explicit Decode(DecoderConfig cfg = {});

    void reset(const BoardState& b);

    Decoding observe(
        const SoftObs& obs,
        bool usable = true
    );

    void clearEvidence();

    int legalMoveCount() const;
};
```

**`reset()`**

Called whenever the committed position changes.

It rebuilds the legal-move candidate cache and clears accumulated evidence.

**`observe()`**

Adds one frame of probabilistic evidence and returns the current decoding result.

Frames with `usable == false` are ignored and do not contribute to accumulated evidence.

**`clearEvidence()`**

Clears accumulated evidence while preserving the legal-move cache. This is useful when observation should restart after a human-help request without rebuilding the candidate moves.

**`legalMoveCount()`**

Returns the number of cached legal candidates.

## 6.5 Decoding Results

```cpp
struct Decoding {
    enum class Kind {
        Confident,
        Ambiguous,
        NoChange,
        NoLegalExplanation,
        Unusable
    };

    Kind kind = Kind::NoChange;

    Move best{};
    Move runnerUp{};

    double bestScore = 0.0;
    double runnerUpScore = 0.0;
    double margin = 0.0;

    double unexplainedChange = 0.0;

    bool promotionUnspecified = false;
    int framesUsed = 0;
};
```

| Result | Meaning |
|---|---|
| `Confident` | One legal move sufficiently explains the accumulated evidence. |
| `Ambiguous` | The top candidates are too close in score. |
| `NoChange` | The observation is consistent with the committed board position. |
| `NoLegalExplanation` | The board appears different, but no legal move adequately explains the evidence. |
| `Unusable` | The frame was marked unusable and was not scored. |

The result contains the best and runner-up moves, their scores, their margin, the unexplained-change evidence, and the number of frames used.

`promotionUnspecified` indicates that the best candidate is a promotion whose promoted piece still needs to be selected.

## 6.6 Evaluation abd Observation

MoveDecoder scoring implementation
1. Candidate move scoring
Each candidate contains a move footprint: the squares that would change if that move were made. Each Delta stores:
- sq: affected square.
- was: expected cell value before the move.
- after: expected cell value after the move.
For every usable observation, the decoder adds the following score for each square in the candidate's footprint:

```math
\(\Delta \text{score} = \log P(\text{after}) - \log\) P\((\text{was}) \%\%\)
```

Therefore, the candidate's cumulative score is:

```math
S(m) \(= \sum_{t=1}^{T} \sum_{d \in \text{footprint}(m)} \left[ \log P_t(d.\text{after}) - \log P_t(d.\text{was}) \right] \%\%\)
```

This is a log-likelihood ratio against the no-move hypothesis. A positive contribution means the observation supports the square's new value more than its original value. A negative contribution means it supports the original value.

Squares not touched by a candidate are omitted because their evidence is identical under that candidate and the no-move hypothesis, so they cancel out.

The decoder accumulates these scores across frames rather than making a decision from a single image.

### 2. Unexplained-change scoring
Separately from candidate scoring, the decoder tracks whether individual squares appear different from the committed board position.

For every square and every possible cell value other than its original value, it accumulates:

```math
LR_{s,c} \(= \sum_{t=1}^{T} \left[ \log P_t(c) - \log P_t(\text{was}_s) \right] \%\%\)
```

For each square, it takes the strongest alternative value:

```math
M_s \(= \max_\){c \(\ne \text{was}_s\)} LR_{s,c}
```

Only positive evidence contributes to the total:

```math
U \(= \sum_{s=1}^{64} \max\)(0, M_s)
```



This becomes unexplainedChange.

The distinction is important:
- Candidate scores measure how well a particular legal move explains the observations.
- unexplainedChange measures how strongly the observations indicate that the board differs from the committed position, regardless of whether a legal move explains it.
This allows the decoder to distinguish an unchanged board from a changed board that cannot be explained by any legal move.
3. Probability floor
Before taking logarithms, probabilities are clamped:
$$
\log P(c) \rightarrow \log(\max(P(c),probFloor))
$$

This prevents zero probabilities from producing negative infinity and limiting the numerical behavior of the scoring system.

The default probFloor is 0.001.
4. Selecting the best candidate
After accumulating evidence, evaluate() finds the candidate with the highest score and the candidate with the second-highest score.

It records:
- best and bestScore
- runnerUp and runnerUpScore
- margin = bestScore - runnerUpScore
- promotionUnspecified
- unexplainedChange
- framesUsed
If there is only one legal candidate, the runner-up score is negative infinity and the margin is positive infinity.
5. Decoding decision
The decoder classifies the accumulated evidence using minScore and margin.
Condition	Result	Meaning
bestScore >= minScore and margin >= margin threshold	Confident	A legal move explains the observations and clearly beats the alternatives.
bestScore >= minScore but margin is too small	Ambiguous	A legal move explains the observations, but multiple candidates remain plausible.
bestScore < minScore and unexplainedChange >= minScore	NoLegalExplanation	The board appears to have changed, but no legal candidate explains it sufficiently.
bestScore < minScore and unexplainedChange < minScore	NoChange	There is insufficient evidence of a move or another board change.

If cands_ is empty, evaluate() returns NoLegalExplanation. This can mean either that there are no legal moves (checkmate or stalemate) or that reset() was not called. The caller should check game-over conditions separately.
6. Observation handling
observe() performs the following steps:
1. If the frame is unusable, return Unusable without updating evidence or incrementing the frame count.
2. Define a probability lookup that applies the configured probability floor.
3. Update every candidate's score using its move footprint.
4. Update the independent per-square change evidence.
5. Increment frames_.
6. Call evaluate() to classify the accumulated evidence.
clearEvidence() can be used to clear accumulated scores and frame history while retaining the legal-move cache. This allows the system to restart observation after human intervention without rebuilding the candidates.
Important implementation detail
The current implementation accumulates evidence directly into Candidate::score and changeLR_. Consequently, each call to observe() contributes another frame's evidence. evaluate() itself does not modify the accumulated scores; it only reads them and produces a Decoding.

One consideration for later testing: evidence accumulation assumes observations are sufficiently independent. Consecutive camera frames can be highly correlated, so treating every frame as independent may make confidence thresholds trigger too quickly. This is something to evaluate with the noise simulator rather than assume away.

The private `evaluate()` function evaluates the accumulated evidence and produces the current `Decoding` result.

The exact scoring and classification logic is implemented inside the decoder. The configuration parameters define the probability floor, minimum evidence threshold, and candidate separation required for a confident result.

The decoder's central responsibility is to distinguish:

- No meaningful board change.
- A board change explained by one legal move.
- A board change explained by multiple similarly plausible moves.
- A board change that cannot be explained by a legal move.

It does not decide what the system should say or do. That is the responsibility of `Arbiter`.

---

# 7. Arbiter

`Arbiter` is the policy layer between move decoding and application behavior.

It consumes the decoder's result and determines what the system should do next.

It is a pure-logic component: it does not depend on ROS 2 or camera images. It receives the current board, the latest decoding result, and the hard-label view of the same observation.

```cpp
Action step(
    const BoardState& b,
    const Decoding& d,
    const Occupancy& latestHard
);
```

The Arbiter separates perception problems from human move errors:

- Perception uncertainty should cause the system to wait, re-observe, or ask for clarification.
- A recognizable illegal move may be reported to the human with a diagnosis.
- A move should only be committed after the decoder and Arbiter have established sufficient confidence.

## 7.1 Configuration

The Arbiter uses `ArbiterConfig` to define its frame thresholds:

```cpp
struct ArbiterConfig {
    int confirmFrames = 2;
    int ambiguousFrameLimit = 5;
    int noLegalFrameLimit = 6;
    int unusableFrameLimit = 30;
};
```

| Parameter | Default | Purpose |
|---|---:|---|
| `confirmFrames` | 2 | Consecutive confident frames for the same move before committing. |
| `ambiguousFrameLimit` | 5 | Consecutive ambiguous frames before asking which move occurred. |
| `noLegalFrameLimit` | 6 | Consecutive unexplained frames before investigating the hard-label observation. |
| `unusableFrameLimit` | 30 | Consecutive unusable frames before requesting a clear view. |

These are initial tuning values, not validated performance settings. They should be adjusted using the noise simulator and full-game evaluation.

## 7.2 Action Representation

The Arbiter returns an `Action` describing what the caller should do:

```cpp
struct Action {
    enum class Kind {
        Wait,
        Commit,
        AskPromotion,
        AskWhichMove,
        ReportIllegal,
        AskCheckBoard,
        RequestClearView
    };

    Kind kind = Kind::Wait;

    Move move{};
    Move alt{};

    Diagnose::Verdict verdict = Diagnose::Verdict::Legal;

    std::vector<Square> squares;
    std::string message;
};
```

### Action types

| Action | Meaning |
|---|---|
| `Wait` | No action is needed yet; continue observing. |
| `Commit` | Apply `move` to `BoardState`, then reset the decoder and Arbiter. |
| `AskPromotion` | The move is known except for the promoted piece. Ask the human, set the promotion, and commit. |
| `AskWhichMove` | Ask the human to choose between `move` and `alt`. |
| `ReportIllegal` | The human most likely played `move`, which is illegal according to `verdict`. |
| `AskCheckBoard` | The camera observation cannot be explained; ask the human to verify or correct the board. |
| `RequestClearView` | The board has been blocked for too long; request an unobstructed view. |

The remaining fields provide context for the action:

- `move` stores the primary candidate.
- `alt` stores the alternative candidate for `AskWhichMove`.
- `verdict` stores the diagnosis for `ReportIllegal`.
- `squares` identifies squares that differ and can be highlighted.
- `message` contains text intended for the player.

## 7.3 Decision Behavior

The Arbiter's private handlers separate the different decoder outcomes:

```cpp
Action onConfident(
    const BoardState& b,
    const Decoding& d
);

Action onAmbiguous(const Decoding& d);

Action onNoLegalExplanation(
    const BoardState& b,
    const Occupancy& hard
);
```

The main `step()` function dispatches based on the decoder result.

### Confident

A confident result is not committed immediately.

The Arbiter requires the same move to appear for `confirmFrames` consecutive frames. It then revalidates the candidate using `diagnoseMove()`.

If the move is legal, it returns `Commit`.

If the move is a promotion without a specified promoted piece, it returns `AskPromotion`.

This provides a second validation boundary between perception and authoritative board-state updates.

### Ambiguous

The Arbiter counts consecutive ambiguous frames.

After `ambiguousFrameLimit`, it returns `AskWhichMove`, providing the best and runner-up candidates for the human to distinguish.

Until the threshold is reached, it continues observing.

### NoLegalExplanation

The Arbiter initially assumes that an unexplained board change may be a perception error.

It continues observing for `noLegalFrameLimit` frames before using the hard-label fallback.

After that threshold, it:

1. Converts the latest observation to hard labels through `SoftObs::hardLabels()`.
2. Runs `Infer::inferMove()` against the current `BoardState`.
3. If a candidate is found, passes it to `Diagnose::diagnoseMove()`.
4. Returns `ReportIllegal` if the physical move is recognizable but illegal.
5. Otherwise returns `AskCheckBoard`.

This prevents the system from blaming the player based on uncertain probabilistic evidence alone.

### Unusable

An unusable frame is not suitable for move recognition, typically because a hand, arm, or moving piece obstructs the view.

The Arbiter waits while the frame remains unusable.

After `unusableFrameLimit` consecutive unusable frames, it returns `RequestClearView`.

Unusable frames do not modify the other decision counters.

### NoChange

`NoChange` is the normal state while the human is thinking.

The Arbiter waits for further observations.

## 7.4 Internal State and One-Shot Actions

The Arbiter maintains state across frames:

```cpp
ArbiterConfig cfg_;

bool haveLast_ = false;
Decoding::Kind lastKind_ = Decoding::Kind::NoChange;
bool issued_ = false;

int streak_ = 0;
Move streakMove_{};

int ambiguous_ = 0;
int noLegal_ = 0;
int unusable_ = 0;

bool clearAsked_ = false;
```

The state supports:

- Tracking the previous decoder result kind.
- Counting consecutive confident frames for the same move.
- Counting ambiguous, unexplained, and unusable frames.
- Preventing repeated issuance of the same action.

Each non-`Wait` action is issued once. Further frames return `Wait` until the decoder's verdict changes kind or the Arbiter is reset.

This prevents repeated requests or repeated commits while the same situation is still being handled.

## 7.5 Caller Contract

The caller is responsible for invoking the Arbiter once per frame:

```cpp
Decoding d = decoder.observe(soft, usable);

Action a = arbiter.step(
    board,
    d,
    soft.hardLabels()
);
```

The caller then handles the returned action.

The contract is:

1. Call `step()` once per frame using the decoder's latest result.
2. Handle the returned action.
3. After acting on `Commit`, `AskPromotion`, `AskWhichMove`, `ReportIllegal`, or `AskCheckBoard`, call `reset()`.
4. Also reset after the committed position changes.
5. Each non-`Wait` action is issued once until the decoder result kind changes or `reset()` is called.

`Commit` requires the caller to apply the move to `BoardState`, then reset both the decoder and Arbiter.

`AskPromotion` requires the caller to obtain the promoted piece, complete the move, and commit it.

Human-help actions require the caller to handle the response and restart observation as appropriate.

### Game-over handling

Checkmate and stalemate must be checked before the observation pipeline runs.

The decoder cannot distinguish a position with no legal moves from a board observation that has no legal explanation. The caller must check `LegalMoves` before treating `NoLegalExplanation` as a perception failure.

**Design decision:** The Arbiter owns decision policy, not application side effects. It returns explicit actions so that ROS communication, UI prompts, board mutation, and other application behavior remain outside the pure-logic component.

---

# 8. Complete Perception Decision Flow

The perception pipeline combines the components described above:

```text
             BoardState
                  │
                  ▼
           Expected Occupancy
                  │
                  │
Camera ──▶ BoardObservation
                  │
                  ▼
                SoftObs
                  │
                  ▼
             Decode
                  │
                  ▼
               Decoding
                  │
                  ▼
                Arbiter
                  │
          ┌───────┼───────────┐
          ▼       ▼           ▼
        Commit  Clarify      Wait
          │       │           │
          ▼       ▼           ▼
      BoardState Human      Observe
       update   response     again
```

The probabilistic path is the primary move-recognition method. Hard-label inference is retained as a fallback when the decoder cannot explain an observation.

The authoritative `BoardState` is only updated after a move has been accepted and validated.

---

# 9. Camera-Based Perception Package

The perception package will convert camera images into board observations that can be consumed by the existing move-recognition pipeline.

Its output is a probability distribution for each of the 64 squares. The package does not determine chess moves or maintain a separate chess state.

## 9.1 BoardObservationInterface

The proposed `BoardObservationInterface` contains:

| Field | Description |
|---|---|
| `probs` | 192 floats: 64 squares × 3 classes (Empty, White, Black). |
| `usable` | Indicates whether the frame is suitable for move decoding. |
| `settle_score` | Optional frame-difference value for logging and tuning. |
| `header` | Timestamp information. |

The probability array uses:

```text
index = (rank * 8 + file) * 3 + classIndex

classIndex:
    0 = Empty
    1 = White
    2 = Black
```

Squares are stored in row-major chess order, beginning with a1 and ending with h8.

`usable` is false when a hand, arm, or moving piece obstructs the board or otherwise makes the observation unsuitable for decoding.

## 9.2 Runtime Pipeline

The intended camera pipeline is:

```text
Camera Image
     │
     ▼
Board Localization
     │
     ▼
64 Square Crops
     │
     ▼
Three-Class Classifier
     │
     ▼
Calibrated Probabilities
     │
     ▼
Settle / Usable Detection
     │
     ▼
BoardObservationInterface
```

The runtime implementation will be provided by `board_perception_node`.

A separate `fake_perception_node` will replay PGN games and publish observations generated from known board states with configurable noise. This allows the move-recognition system to be evaluated before a camera classifier is trained.

---

# 10. Perception Development and Evaluation Plan

The perception system will be developed in phases. The initial goal is to determine how accurate perception must be before investing in a trained image classifier.

## 10.1 Phase 0: Fake Perception Node and Noise Sweep

**Goal:** Determine the perception accuracy required by the move decoder.

The fake perception node will replay PGN games. For each move, it will publish observations representing:

- Unusable frames while a hand is present.
- A lifted-piece frame.
- Settled observations after the move.

### Noise model

The simulator should support configurable:

- Independent per-square errors, including weak and confident errors.
- Errors correlated across neighboring squares.
- Blocks of occluded squares.
- Confidently incorrect square predictions.
- Mid-move observations.

### Evaluation

Sweep the per-square error rate and measure:

1. Silent wrong commits per 1,000 moves.
2. Human interventions per game.
3. Frame yield.

Compare hard-label inference (`inferMove()` on hard labels) against probabilistic `Decode` using identical observations.

Use the results to tune `DecoderConfig` and `ArbiterConfig`.

**Exit criterion:** Produce an evaluation curve and determine whether the perception targets are achievable. This phase should also establish whether the additional complexity of soft observations provides a meaningful improvement.

## 10.2 Phase 1: Simulation Setup

**Goal:** Produce chessboard images with known ground truth.

The simulator has not yet been selected. Gazebo, Isaac Sim, Blender, or another suitable tool may be used. The default should be whichever simulator best fits the rest of the project.

Initial assumptions:

- Use an overhead camera looking directly down at the board.
- Use simple 3D board and piece models.
- Randomize piece and board colors and textures.
- Use a known board pose so the homography can be calculated from the camera and board poses rather than detected from image corners.

An overhead camera is preferred initially because it reduces occlusion and makes square occupancy and piece color easier to observe.

**Exit criterion:** Render a chess position from FEN and save the resulting image with its 64 ground-truth square labels.

## 10.3 Phase 2: Dataset Generation

**Goal:** Generate a reproducible, labeled dataset.

Use `python-chess` to replay PGN games, render positions, and label each square using the known board state.

### Domain randomization

Randomize:

- Lighting direction and intensity.
- Camera pose.
- Board and piece colors and textures.
- Shadows.
- Hand and arm occlusion.
- Mid-move frames.

### Dataset organization

Split the dataset by game and rendering condition rather than randomly splitting individual frames. Hold out entire lighting conditions and textures so that evaluation measures generalization.

An initial target is approximately 2,000 positions, or 128,000 square samples. The dataset can grow if the learning curves indicate that more data is needed.

Store:

- Images or square crops.
- Square labels in CSV or JSON.
- Random seeds.
- Configuration used to generate each sample.

**Exit criterion:** Training, validation, and test datasets can be regenerated from a single command.

## 10.4 Phase 3: Square Classifier

**Goal:** Train a three-class square classifier that produces calibrated probabilities.

### Model

The initial model options are MobileNetV3-Small and ResNet-18.

MobileNetV3-Small is the default because the runtime target is resource-constrained. A larger model should only be used if the smaller model does not meet the requirements.

### Input and output

Each input is a crop corresponding to one square. The crop should extend beyond the square boundary sufficiently to avoid cutting off tall pieces.

All 64 square crops from a frame should be processed as one batch.

The model outputs a softmax distribution over:

- Empty.
- White.
- Black.

The classifier must not apply argmax internally. Probabilities must be preserved for `SoftObs` and `Decode`.

### Probability calibration

Fit a temperature parameter on the validation set so that predicted confidence corresponds more closely to actual accuracy.

For example, predictions assigned 99% confidence should be correct approximately 99% of the time.

This is important because the decoder's confidence and margin tests depend on the quality of the probability estimates.

### Evaluation

Report:

- Overall accuracy.
- Confusion matrix.
- Calibration curve.
- Errors by square location.
- Errors by lighting condition.

Export the trained model to ONNX.

**Exit criterion:** The classifier meets the per-square target established in Phase 0 on held-out conditions and has acceptable probability calibration.

## 10.5 Phase 4: Perception ROS 2 Node

**Goal:** Publish `BoardObservation` at runtime using the trained classifier.

The node will:

1. Subscribe to the camera image and `CameraInfo`, if available.
2. Warp the board using the known homography.
3. Extract 64 square crops.
4. Run the ONNX model.
5. Apply probability calibration.
6. Publish the resulting `BoardObservation`.

### Square ordering

Image row zero usually corresponds to rank 8, while the observation interface expects a1 first.

The image-to-chessboard orientation conversion must happen in exactly one place and must have a corresponding test.

### Implementation language

Python is the default for faster iteration. C++ with ONNX Runtime remains an option if runtime performance requires it.

The ROS message interface remains the same regardless of implementation language.

### Parameters

The node should support:

- Model path.
- Calibration temperature.
- Camera topic.
- Crop margin.
- Settle thresholds.

## 10.6 Phase 5: Settle and Usable Detection

**Goal:** Determine when a camera frame is suitable for move decoding.

Frame differencing will be performed within the board region. A large change indicates motion, such as a moving piece or hand, and should result in `usable = false`.

Require a configurable number of consecutive quiet frames before setting `usable = true`.

The initial default is three quiet frames.

Additional considerations:

- Foreground detection outside the board may help detect an approaching hand or arm.
- Later, the robot arm should provide a topic or flag indicating whether it is parked.
- Observations should remain unusable while the robot arm is moving.

The Arbiter currently requests a clearer view after 30 consecutive unusable frames. The camera frame rate and this threshold should be selected together so the timeout corresponds to an appropriate amount of real time.

## 10.7 Phase 6: End-to-End Evaluation

**Goal:** Verify that the complete perception and move-recognition pipeline meets the project targets.

Run simulated full games through `board_state_node` using the perception pipeline.

Compare the resulting behavior against the evaluation targets.

Log each failure with a cause, such as:

- Occlusion.
- Mid-move frame.
- Capture color error.
- Low-margin candidate tie.
- Probability calibration error.

If failures are concentrated in one category, address that category before adding more complexity.

---

# 11. Evaluation Targets

The following are proposed system targets. They are not yet validated results.

| Metric | Target |
|---|---:|
| Silent wrong commits | 0 per 1,000 moves |
| Human interventions | 5 per game |
| Frame yield | 99% |
| Move-commit latency | 3 seconds |
| Per-square classifier accuracy | 75% |

### Metric interpretation

**Silent wrong commits** are the primary reliability metric. A move committed incorrectly without human intervention can corrupt the authoritative board state and affect every subsequent decision.

**Human interventions** include questions about which move occurred, requests to check the board, and illegal-move reports.

**Frame yield** measures the fraction of moves committed without questions.

**Move-commit latency** measures the time between a piece being placed and the system committing the corresponding move.

**Per-square accuracy** is a diagnostic metric, not the ultimate objective. The decoder only needs sufficiently reliable evidence on the squares that distinguish legal candidate moves. Full-game move recognition is therefore more important than maximizing isolated square-classification accuracy.

---

# 12. Fallback Approaches

These approaches should only be considered if Phase 6 shows that the primary perception pipeline does not meet its targets.

1. **Classical image differencing:** Add differencing as a second evidence source and fuse per-square evidence by summing log-probabilities. Keep it only if an ablation test demonstrates an improvement. Its errors may be correlated with classifier errors.
2. **Existing chess recognition models:** Collapse a pretrained model, such as `chesscog`, into the three occupancy classes as an additional weak evidence source.
3. **Sensor board:** Consider per-square Hall-effect or reed switches if the project eventually reaches physical hardware.

Feeding the previous confirmed frame into the model to predict per-square changes is deferred.

---

# 13. Perception Package Layout

The proposed package structure is:

```text
perception_pkg/
├── config/
│   └── camera, model, and settle parameters
├── perception_pkg/
│   └── Python modules for:
│       ├── board warping
│       ├── square cropping
│       ├── classifier runtime
│       ├── settle detection
│       └── ROS 2 node
├── training/
│   ├── data generation
│   ├── dataset management
│   ├── training
│   ├── calibration
│   └── ONNX export
├── models/
│   └── exported model and calibration temperature
├── tests/
│   └── unit tests and golden frames
└── launch/
    └── real and fake perception launch files
```

**Shared preprocessing decision:** Training and runtime code must use the same square-cropping implementation. This ensures that the classifier receives the same type of input during training and deployment.

---

# 14. Testing Strategy

Testing should cover individual components as well as the complete ROS 2 pipeline.

| Area | Test |
|---|---|
| Board geometry | Warping a known rendered board produces correctly labeled square crops. |
| Square ordering | Verify that image orientation maps to the expected a1-to-h8 message order. |
| Observation message | A known position produces the expected 192-float probability array. Compare against `SoftObs::fromHard()` for the same position. |
| Calibration | High-confidence predictions achieve corresponding accuracy on validation data. |
| Settle detection | Scripted static, hand-entering, and hand-leaving sequences produce the expected `usable` values. |
| ROS integration | Fake perception publishes observations that can be consumed by `board_state_node`. |
| Full-game regression | Scripted games are processed without incorrect board-state commits. |
| Golden frames | Previously problematic images remain available for regression testing. |

The evaluation simulator should retain the same game and observation inputs when comparing the hard-label and soft-observation approaches. This ensures that performance differences are caused by the decoding strategy rather than different input data.

---

# 15. Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Overconfident probabilities | An incorrect high-confidence prediction may pass the decoder's margin test. | Probability calibration, decoder probability floors, and calibration tests. |
| Simulation overfitting | Good simulated performance may not transfer to real camera images. | Held-out rendering conditions, extensive domain randomization, and treating simulation results as pipeline validation rather than proof of real-world accuracy. |
| Correlated perception errors | Shadows or occlusions may affect multiple neighboring squares. | Include correlated noise in Phase 0 and analyze failures by region in Phase 6. |
| Square-ordering errors | A flipped board may produce plausible but incorrect observations. | Perform orientation conversion in one location and test it explicitly. |
| Excessive latency | Too many settle frames delay move recognition. | Measure latency during full-game evaluation and tune settle thresholds. |

---

# 16. Next Implementation Steps

The next development work should follow this order:

1. Complete the user-configurable Stockfish strength interface.
2. Build the fake perception node and configurable noise model.
3. Compare hard-label inference against soft decoding on identical observations.
4. Tune `DecoderConfig` and `ArbiterConfig` using the evaluation results.
5. Select and configure a chessboard simulator.
6. Generate a reproducible labeled dataset.
7. Train and calibrate the three-class square classifier.
8. Implement the runtime perception node.
9. Integrate the perception node with the existing ROS 2 board-state pipeline.
10. Verify full-game move recognition against the evaluation targets.
11. Represent chess moves in a form suitable for robot manipulation.
12. Generate robot manipulation plans from `BoardState` and `Move`.
13. Simulate basic piece movement, followed by captures and special moves.
14. Add visualization, realistic motion planning, and eventually physical hardware integration.