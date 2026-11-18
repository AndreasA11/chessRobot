

from simulation_types import ObservationPhase, Mode, NoiseConfig
from noise_adapter import ObservationNoise
import numpy as np

import chess
import chess.pgn




class FakeSimulation:

    def __init__(
        self,
        noise_config: NoiseConfig | None = None,
        seed: int | None = None,
    ):
        self.board = chess.Board()

        self.noisy_observation = None
        self.game = None
        self.current_node = None
        self.mode = Mode.NA

        self.noise_config = noise_config or NoiseConfig()
        self.seed = seed 

        self.last_move = None
        self.board_before_move = None

        self.noise = ObservationNoise(
            config=self.noise_config,
            seed=self.seed,
        )

        



    def get_mode(self) -> Mode:
        return self.mode
    
    def parse_pgn(self, file_path: str) -> None:
        with open(file_path, encoding="utf-8") as pgn:
            self.game = chess.pgn.read_game(pgn)

        if self.game is None:
            raise ValueError(f"No PGN game found in {file_path}")

        self.board = self.game.board()
        self.current_node = self.game

    def peek_pgn_move(self) -> chess.Move | None:
        """Next scripted move, without applying it."""
        if self.current_node is None:
            raise RuntimeError("No PGN game has been loaded.")
        if self.current_node.is_end():
            return None
        return self.current_node.variation(0).move

    def apply_move(self, move: chess.Move) -> chess.Board:
        """Apply any move (PGN, human, engine) to ground truth. Returns the board BEFORE the move."""
        # Validate first, against the current position, before touching any state.
        if move not in self.board.legal_moves:
            raise ValueError(f"Illegal move {move.uci()} in {self.board.fen()}")

        before = self.board.copy()
        self.board.push(move)
        self.board_before_move = before
        self.last_move = move

        if self.current_node is not None and not self.current_node.is_end() \
                and self.current_node.variation(0).move == move:
            self.current_node = self.current_node.variation(0)
        return before

    def get_ground_truth(self) -> chess.Board:
        return self.board.copy()

    def get_observation(self, phase: ObservationPhase = ObservationPhase.SETTLED,) -> tuple[np.ndarray, bool]:
        """Returns (probs, usable, settle_score)."""
        if phase == ObservationPhase.SETTLED:
            probs, usable = self.noise.generate(board=self.board, phase=phase)
            settle = 0.0
        else:
            if self.last_move is None:
                raise RuntimeError("No move has been applied yet.")
            probs, _ = self.noise.generate(
                board=self.board_before_move,
                move=self.last_move,
                phase=phase,
            )
            usable = False  # hand is in motion
            settle = 0.5 if phase == ObservationPhase.LIFTED else 1.0
        self.noisy_observation = probs
        return probs, usable, settle    

    def reset(self, seed: int | None = None) -> None:
        """
        Reset the simulation to the beginning of the game.

        Args:
            seed: Seed used to initialize the observation noise generator.
        """
        self.seed = seed

        # Reset the ground-truth board.
        self.board.reset()

        # Rewind PGN traversal.
        self.current_node = self.game

        # Clear previous observation.
        self.noisy_observation = None

        # Reinitialize noise generator.
        self.noise = ObservationNoise(
            config=self.noise_config,
            seed=self.seed,
        )