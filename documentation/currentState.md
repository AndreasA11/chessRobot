CURRENT STATE DOCUMENTATION 

10/22/2-26


10/21/2026

Added small compatability changes between diagnose and decode.
This was done by adding a legalMoves which builds on diagnose. This gets every legal move for a given side to play and some board state.

10/20/2026

SoftObs and Decode have been implemented and initially tested.

SoftObs just gives us probabilites of what each square has out of {Empty, White, Black} for some given frame from the camera. The Decode class takes in some softobs probabilities and outputs some state so that boardState knows what to do with some given information.     
        Confident,           // one move clearly explains the evidence
        Ambiguous,           // top two moves too close; ask for help / re-observe
        NoChange,            // evidence does not favor any move over "nothing happened"
        NoLegalExplanation,  // some evidence of change, but no legal move explains it
        Unusable             // frame flagged unusable (hand, motion, arm); not scored

Where the pipeline is essentially: 
    SoftObs -> MoveDecoder (uses diagnoseMove for candidates) -> Decoding
    -> Arbiter (retry/ask/blame policy) -> Action
    -> committed move -> BoardState::applyMove

10/14/2026

after further testing it is unreliable for both piece detection and color detection.

We found that while they both may have high percentage accuracies, it still was not fitting enough for our use case.
The problem we face is that these models are trying to take in a single image and output a fen board state, however we dont just have one position, we know the previous position. 
A model that would be more useful would be: 
      Given previous fen position and curr image -> potential current fen position
However these models don't take in previous fen position so we lose a lot of information. 

Even more so the current fen position output is not necessarily needed, given our current implementation of occupancy and infer we just need the color and occupancy state of a square to determine the current position given the previous position. There are no repos out there that do this, probably because a software that determines the color occupancy of a square given an image is completely uselss (except in this case LOL)

An interesting repo is https://github.com/Kyouku/chess-move-recognition, where they do look at single image recognition vs an over the board video. However these results are still probably not useful as they are still outputing FEN positions at a lower accuracy than needed.

Where to go from here?:
      1. Build a small custom three state square classifier, then let occupancy and infer determine actual fen position
      2. take outputs from these models, maybe a combination of multiple of them? 
      3. ask for human help (as stated by Manuela Veloso when talking about her CoBot project sometimes human help is needed) -> https://www.cs.cmu.edu/~coral/projects/cobot/humanrobot.html 
      4. somehow figure out how to use our previous position knowledge to bias other models

A pipeline that combines these ideas:

      1. Custom three-state square classifier trained on our own
      board and pieces. Produces per-square evidence; Occupancy + Infer decode the
      position from it.
      2. Fuse diverse evidence sources (classical differencing, custom classifier,
      optionally pretrained models collapsed to empty/white/black). Only useful if
      errors are independent, so validate each source's contribution. We do not currently know this.
      3. Targeted human help (symbiotic autonomy, per Veloso's CoBot work): when the top
      two candidate moves are too close, ask a one-tap question about the specific
      ambiguity. Log corrections as new training data.
      4. 
            a) Use the known previous position as a prior: restrict to legal moves and score each against the evidence (Bayesian decoding in Infer). 
            b) Later, try feeding the previous frame to the model so it predicts per-square changes.
      - although 4b is unlikely to be implemented 

      Metric: move-recognition accuracy over full games, plus rate of human
      interventions, not per-square accuracy.

Note: hardware is currently not the main focus of the project, we want to build a simulation ready version that can be transferable to hardware when/if necessary

so ideas like:

      Physical setup is a design variable, not a constraint. Since the ML side is
      the limiting factor, we will engineer the environment to simplify perception:
      - Rigid overhead camera, manual exposure/focus/white balance, corner fiducials
      for automatic calibration.
      - Diffuse, enclosed, constant lighting; matte board and pieces.
      - High contrast between pieces and squares; consider colored piece tops.
      - Arm parks out of frame before capture; settle detection for human moves.
      - Optional: ID markers on piece tops (resolves promotions).
      Decision gate: if move accuracy over full games is below target after the rig
      is built, move to per-square sensors (Hall-effect/reed) or a hybrid.

are not currently being considered.

so ideal implementation path

A. Decoding (Infer, Bayesian legal-move scoring): restrict to legal moves plus a
   "no move" hypothesis, score each against per-square evidence, commit only above
   a margin, flag unspecified promotions. Validated against a synthetic noise model
   to set the per-square accuracy target for perception.
B. Custom three-state (empty/white/black) square classifier trained on rendered
   data from the simulated camera, auto-labeled from PGN games, with domain
   randomization. Outputs soft probabilities; Occupancy + Infer decode the position.
   Evaluated on held-out games/conditions by move-recognition accuracy.
C. Targeted human help (symbiotic autonomy, per Veloso's CoBot work): when the top
   two candidates are too close, ask a one-tap question about the specific
   ambiguity (simulated oracle). Report accuracy vs. interventions per game; log
   corrections as new training data.

Fallback (only if the above misses the accuracy/intervention target):
- Fuse additional evidence sources (classical differencing; pretrained models
  collapsed to empty/white/black), kept only if an ablation shows a gain.

Deferred: feeding the previous confirmed frame to the model (per-square change
prediction).

10/13/2026

update to using: https://github.com/georg-wolflein/chesscog (licensing reasons) and ease of use.
will have to run using python 3.10 instead of current 3.12 version so we cannot run it with our ros2 stuff, we have to run it seperately and gain info that way
initial local testing of that repo works, we will create a testing version in a different repo and merge later? Possibly just add it to environment in docker?


10/12/2026

The perception package is being started, we want to test the limits of what information we can get from perception as this might slightly change our implementation and overall impact of infer and occupancy. 

and using the version that works better for us. 

The reason we are starting the perception package before fine tuning and finishing board state is because we might be able to tell individual pieces apart instead of just color difference. 



10/10/2026

Diagnose and Infer have been implemented, this means that when we have board perception we will be able to convert the board perception given some infered move to change the internal board state. 

Diagnose evaluates a candidate Move against chess rules and returns a Diagnosis.
Its verdicts cover conditions such as an empty source square, the wrong side moving, capturing one's own piece, illegal movement geometry, blocked paths, leaving the king in check, invalid castling, and ambiguous promotion.
moveIsLegal() provides a boolean legality check, while describe() converts a verdict into a human-readable description.

Infer compares the current BoardState with an observed Occupancy and determines what physical board change most likely occurred.

Its Inference result distinguishes between:

      NoChange: the board matches the expected position.

      PieceLifted: a piece has been lifted but not yet placed.

      Candidate: the observed changes are consistent with a candidate move.

      Anomaly: the observed changes do not match a supported single-move pattern.

The result also records changed squares for debugging or visualization.

10/6/2026

Occupancy is being introduced as a simplified representation of the physical board.

Unlike BoardState, it only stores:

Empty
White
Black

for each square.

It intentionally does not store piece type, side to move, castling rights, en passant state, or move counters.

BoardState
    │
    │ lossy projection
    ▼
Occupancy

This allows the perception system to report what it can reliably observe from a physical board without requiring it to determine the complete chess state.

The planned perception flow is:

Physical Board
      │
      ▼
Camera / Sensor
      │
      ▼
Observed Occupancy
      │
      │ compare with expected state
      ▼
Candidate Move
      │
      ▼
Move Diagnosis
      │
      ▼
BoardState

The inference layer will use the existing BoardState to provide information that Occupancy does not contain, such as piece type, side to move, castling rights, and en passant state.

Date: 10/5/2026

EngineProcess and StockfishNode have been implemented for Stockfish communication.

EngineProcess has been implemented to manage the Stockfish process using POSIX pipes.

It can start Stockfish, write UCI commands to its stdin, read output lines from stdout, and shut the process down safely.

readLine() buffers raw engine output and returns one complete line at a time. writeLine() sends one complete command at a time and handles partial writes.

The process layer is independent of UCI and ROS. It only handles communication with the Stockfish process.

EngineProcess manages the Stockfish child process using POSIX pipes. It handles starting the process, writing commands, reading output lines, and shutting the process down.

StockfishNode provides the ROS 2 interface to Stockfish. It uses EngineProcess for process communication and the UCI library for building commands and parsing engine responses.

The node performs the UCI handshake, configures Stockfish using ROS parameters, accepts FEN positions, starts timed searches, and publishes the resulting UCI move.

The node also handles new FEN positions arriving while Stockfish is starting or searching. Outdated searches are stopped and their resulting moves are discarded.

Current ROS 2 flow:

BoardStateNode
      │
      │ FEN
      ▼
StockfishNode
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
StockfishNode
      │
      │ UCI move
      ▼
BoardStateNode


Next step is completing the full FEN → Stockfish → UCI move → BoardState loop and then beginning the robot manipulation layer.


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

