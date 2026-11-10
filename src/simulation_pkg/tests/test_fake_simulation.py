import chess
import numpy as np
import pytest

import sys
sys.path.insert(0, "../scripts") 

from simulation_types import Color
from fake_simulation import FakeSimulation
from noise_adapter import NoiseConfig, ObservationNoise, ObservationPhase


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


def test_simulation_uses_noise_config():
    config = NoiseConfig(
        independent_error_rate=0.0,
        soft_observations=False,
    )

    sim = FakeSimulation(noise_config=config, seed=42)

    assert sim.noise_config is config
    assert sim.noise.config is config


def test_simulation_observation_is_reproducible():
    config = NoiseConfig(independent_error_rate=0.2)

    sim1 = FakeSimulation(noise_config=config, seed=42)
    sim2 = FakeSimulation(noise_config=config, seed=42)

    obs1, usable1 = sim1.get_observation()
    obs2, usable2 = sim2.get_observation()

    np.testing.assert_array_equal(obs1, obs2)
    assert usable1 == usable2

def test_multiple_observations_do_not_change_ground_truth():
    sim = FakeSimulation()

    original_fen = sim.get_ground_truth().fen()

    for _ in range(10):
        sim.get_observation()

    assert sim.get_ground_truth().fen() == original_fen



def test_reset_restores_initial_board():
    sim = FakeSimulation()

    sim.board.push_uci("e2e4")
    sim.board.push_uci("e7e5")

    sim.reset(seed=42)

    assert sim.board.fen() == chess.Board().fen()

def test_reset_clears_observation():
    sim = FakeSimulation()

    sim.get_observation()
    sim.noisy_observation = np.zeros(192, dtype=np.float32)

    sim.reset(seed=42)

    assert sim.noisy_observation is None

def test_reset_reproduces_observations():
    config = NoiseConfig(independent_error_rate=0.2)

    sim = FakeSimulation(noise_config=config, seed=42)

    first_observation, first_usable = sim.get_observation()

    sim.reset(seed=42)

    second_observation, second_usable = sim.get_observation()

    np.testing.assert_array_equal(
        first_observation,
        second_observation,
    )
    assert first_usable == second_usable