"""Shared helpers for the simulation package tests.

Assumptions (adjust if your package differs):
  * The modules under test live one directory above tests/.
  * Color is EMPTY=0, WHITE=1, BLACK=2 (matches the [empty, white, black] spec).
  * NoiseConfig has exactly the fields the publisher references.
"""
import sys
from pathlib import Path

import chess
import numpy as np
import pytest

import sys
sys.path.insert(0, "../scripts") 


from simulation_types import Color
from fake_simulation import FakeSimulation
from noise_adapter import NoiseConfig, ObservationNoise, ObservationPhase


def make_config(**overrides) -> NoiseConfig:
    """A fully noise-free, one-hot config; override individual fields per test."""
    base = dict(
        independent_error_rate=0.0,
        confident_error_rate=0.0,
        confident_error_probability=1.0,
        correlated_error_rate=0.0,
        correlated_neighbor_rate=0.0,
        occlusion_rate=0.0,
        max_occlusion_size=1,  # validator requires >= 1
        soft_observations=False,
        confidence=1.0,
    )
    base.update(overrides)
    return NoiseConfig(**base)


def true_classes(board: chess.Board) -> np.ndarray:
    out = np.zeros(64, dtype=np.int8)
    for sq in chess.SQUARES:
        piece = board.piece_at(sq)
        if piece is None:
            out[sq] = Color.EMPTY
        else:
            out[sq] = Color.WHITE if piece.color == chess.WHITE else Color.BLACK
    return out


def decode(probs) -> np.ndarray:
    """(192,) probabilities -> (64,) argmax class per square."""
    return np.asarray(probs, dtype=np.float32).reshape(64, 3).argmax(axis=1)


@pytest.fixture
def clean_config():
    return make_config()


@pytest.fixture
def pgn_file(tmp_path):
    path = tmp_path / "game.pgn"
    path.write_text('[Event "test"]\n\n1. e4 e5 2. Nf3 *\n', encoding="utf-8")
    return str(path)