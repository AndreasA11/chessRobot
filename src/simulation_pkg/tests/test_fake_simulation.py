import chess
import numpy as np
import pytest

import sys
sys.path.insert(0, "../scripts") 

from FakeSimulation import Color, FakeSimulation
from NoiseAdapter import NoiseConfig, ObservationNoise, ObservationPhase


def test_get_ground_truth_returns_copy():
    sim = FakeSimulation()

    original_fen = sim.board.fen()

    board = sim.get_ground_truth()
    board.push_uci("e2e4")

    assert sim.board.fen() == original_fen
    assert board.fen() != original_fen


def test_get_observation_shape_and_dtype():
    sim = FakeSimulation()

    observation, usable = sim.get_observation()

    assert observation.shape == (192,)
    assert observation.dtype == np.float32
    assert isinstance(usable, bool)

def test_get_observation_does_not_mutate_board():
    sim = FakeSimulation()

    original_fen = sim.board.fen()

    sim.get_observation()

    assert sim.board.fen() == original_fen

def test_get_observation_matches_ground_truth_without_noise():
    sim = FakeSimulation(
        noise_config=NoiseConfig(independent_error_rate=0.0)
    )

    observation, usable = sim.get_observation()

    assert usable

    predicted = observation.reshape(64, 3).argmax(axis=1)
    truth = sim.get_ground_truth()

    expected = np.array([
        Color.EMPTY.value
        if truth.piece_at(square) is None
        else Color.WHITE.value
        if truth.piece_at(square).color == chess.WHITE
        else Color.BLACK.value
        for square in chess.SQUARES
    ])

    np.testing.assert_array_equal(predicted, expected)