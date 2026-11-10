

from SimulationTypes import Color, ObservationPhase, Mode, NoiseConfig
from NoiseAdapter import ObservationNoise
import numpy as np

import random
from enum import Enum

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

        self.noise = ObservationNoise(
            config=self.noise_config,
            seed=self.seed,
        )


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

    
    from NoiseAdapter import ObservationPhase
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

        return observation, usable        