Chess Robot — Current Overview

Last updated: October 14, 2026

## Goal

Build a ROS 2 chess-playing robot that can observe a chessboard, maintain a correct chess position, choose moves with Stockfish, and eventually execute those moves with a robot arm. Development is **simulation-first**: keep chess logic, planning, perception, and physical manipulation separate so the software can be tested without hardware and later transferred to a physical setup.

## General implementation path

1. **Perception → observation:** Read the board using a camera or other sensor and produce a simplified per-square observation: Empty, White, or Black.
2. **Observation → move inference:** Compare observed occupancy with the expected occupancy derived from the known BoardState to infer the likely human move.
3. **Move diagnosis → authoritative state:** Check the inferred move against chess rules, then update BoardState and publish its FEN representation.
4. **Planning:** Send the current FEN to Stockfish through a ROS 2 node, receive its UCI move, and apply that move to BoardState.
5. **Action:** Convert a selected chess move into a robot manipulation plan, simulate piece movement, and later add captures, special moves, visualization, motion planning, and physical hardware.

The architecture deliberately separates the complete chess position (BoardState) from what perception is expected to observe (Occupancy). Occupancy does not identify piece type or store turn, castling rights, en-passant state, or move counters. Those details are recovered from the existing position and the inferred move rather than delegated to the vision model.

## Current status

**Completed foundation**
- BoardState represents the board and game metadata, converts to/from FEN, parses UCI moves, describes moves, and applies moves.
- BoardStateNode exposes board-state updates through ROS 2.
- The UCI protocol layer and EngineProcess isolate Stockfish command/response parsing from process management.
- StockfishNode connects Stockfish to ROS 2, accepts FEN positions, searches, and publishes moves; stale search results are discarded when a newer position arrives.
- The FEN → Stockfish → UCI move → BoardState loop is documented as complete.
- Occupancy represents each square as empty/white/black.
- Infer compares observed occupancy with the expected board and classifies the observation as NoChange, PieceLifted, Candidate, or Anomaly.
- Diagnose checks inferred moves for implemented chess-legality conditions and explains failures.

**Current focus: reliable board perception**
- Tests of existing end-to-end chessboard-recognition repositories/models showed that their reported position accuracy is not sufficient for this project. A small number of incorrect squares makes a position unusable, and some models confuse empty squares with occupied ones.
- The key insight is that this system already knows the previous confirmed position. Rather than trusting a model to predict a complete FEN from one image, perception should provide evidence about each square, and Occupancy + Infer should use that evidence alongside the known state.
- The current preferred path is to build a custom three-state (Empty / White / Black) square classifier, initially trained on rendered data from the simulated camera and automatically labeled from PGN games, with domain randomization. Infer should use legal-move constraints and a “no move” hypothesis to score candidate explanations against the square evidence.
- Targeted human confirmation is planned as a fallback when the leading candidate moves are too close; corrections can become additional training data. Combining classical differencing or pretrained models is optional and should be retained only if tests show that it improves results.
- Evaluate perception by **move-recognition accuracy over full games and human interventions per game**, not only per-square or single-position accuracy.

## Key decisions and boundaries

- **Simulation first; hardware later.** The physical setup is not currently the bottleneck. A controlled camera/lighting/board setup can be engineered later to simplify perception. Consider alternative sensors only if the camera-based approach misses the accuracy/intervention target.
- **Keep perception deliberately simple.** The classifier reports per-square occupancy and color, not a full FEN or exact piece identity. The known BoardState supplies missing chess context.
- **Use prior state and chess rules.** Infer likely moves from the difference between expected and observed occupancy, constrain candidates using legality, and commit only when confidence is sufficient.
- **Keep modules decoupled.** BoardState, Occupancy, Infer, Diagnose, UCI/process handling, and robot manipulation have distinct responsibilities. ROS 2 is the middleware; ROS node boundaries currently use strings, while UCI remains internal to the Stockfish interface.
- **Perception dependency decision.** The project has moved to https://github.com/georg-wolflein/chesscog for further evalutaion as it gave the best results from initial testing
## Next steps

1. Finish and validate configurable Stockfish strength/skill settings.
2. Establish a perception test pipeline and benchmark the current chesscog option against the project’s move-recognition requirements.
3. Implement and evaluate the custom three-state classifier and evidence-based/legal-move inference.
4. Add targeted human confirmation if ambiguity remains.
5. Continue into move-to-manipulation planning, basic movement simulation, captures/special moves, visualization, and eventually realistic simulation and hardware.

**Success criterion:** reliably recognize and play moves across complete games, with an acceptable rate of human intervention—not merely achieve a high isolated image-classification score.
