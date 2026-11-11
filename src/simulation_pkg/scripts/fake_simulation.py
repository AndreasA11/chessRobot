

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
        if move not in self.board.legal_moves:
            raise ValueError(f"Illegal move {move.uci()} in {self.board.fen()}")
        before = self.board.copy()
        self.board.push(move)
        if self.current_node is not None and not self.current_node.is_end() \
                and self.current_node.variation(0).move == move:
            self.current_node = self.current_node.variation(0)
        return before

    def get_ground_truth(self) -> chess.Board:
        return self.board.copy()

    def get_observation(self, phase: ObservationPhase = ObservationPhase.SETTLED,) -> tuple[np.ndarray, bool]:
        """
        Generate a noisy observation of the current ground-truth board.

        Returns:
            observation: Flattened float32 array of shape (192,).
            usable: Whether the observation is usable for inference.
        """
        observation, usable = self.noise.generate(
            board=self.board,
            phase=phase,
        )
        
        self.noisy_observation = observation
        return observation, usable        

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