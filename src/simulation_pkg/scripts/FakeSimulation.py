
import numpy as np

import random
from enum import Enum

import chess
import chess.pgn

class Mode(Enum):
    PNG = 1
    HUMAN = 2
    NA = 3

class Color(Enum):
    EMPTY = 0
    WHITE = 1
    BLACK = 2

class FakeSimulation:

    def __init__(self):
        self.board = chess.Board()
        self.noisy_observation = None
        self.game = None
        self.current_node = None
        self.mode = Mode.NA

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