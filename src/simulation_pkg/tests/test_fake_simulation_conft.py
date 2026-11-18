



import chess
import numpy as np
import pytest


from conftest import decode, make_config, true_classes

import sys
sys.path.insert(0, "../scripts") 

from simulation_types import Color
from fake_simulation import FakeSimulation
from noise_adapter import NoiseConfig, ObservationNoise, ObservationPhase

@pytest.fixture
def sim(clean_config):
    return FakeSimulation(noise_config=clean_config, seed=0)


def observe(sim, phase=ObservationPhase.SETTLED):
    """Works with both the original 2-tuple and the fixed 3-tuple return."""
    probs, usable, *rest = sim.get_observation(phase=phase)
    return probs, usable, (rest[0] if rest else 0.0)


# ---------------------------------------------------------------- PGN handling

def test_parse_pgn_and_peek(sim, pgn_file):
    sim.parse_pgn(pgn_file)
    assert sim.board.fen() == chess.Board().fen()
    assert sim.peek_pgn_move() == chess.Move.from_uci("e2e4")
    assert sim.peek_pgn_move() == chess.Move.from_uci("e2e4")  # peek is non-destructive


def test_parse_pgn_missing_game(sim, tmp_path):
    empty = tmp_path / "empty.pgn"
    empty.write_text("", encoding="utf-8")
    with pytest.raises(ValueError):
        sim.parse_pgn(str(empty))


def test_peek_without_pgn_raises(sim):
    with pytest.raises(RuntimeError):
        sim.peek_pgn_move()


def test_full_pgn_replay_reaches_end(sim, pgn_file):
    sim.parse_pgn(pgn_file)
    played = []
    while (move := sim.peek_pgn_move()) is not None:
        sim.apply_move(move)
        played.append(move.uci())
    assert played == ["e2e4", "e7e5", "g1f3"]
    assert sim.peek_pgn_move() is None


# ---------------------------------------------------------------- apply_move

def test_apply_move_returns_board_before(sim):
    before = sim.apply_move(chess.Move.from_uci("e2e4"))
    assert before.fen() == chess.Board().fen()
    assert sim.board.piece_at(chess.E4) is not None


def test_apply_illegal_move_raises_and_leaves_board_unchanged(sim):
    fen = sim.board.fen()
    with pytest.raises(ValueError):
        sim.apply_move(chess.Move.from_uci("e2e5"))
    assert sim.board.fen() == fen


def test_off_script_move_does_not_advance_pgn_cursor(sim, pgn_file):
    sim.parse_pgn(pgn_file)
    sim.apply_move(chess.Move.from_uci("d2d4"))  # not the scripted e2e4
    assert sim.current_node is sim.game


def test_ground_truth_is_a_copy(sim):
    gt = sim.get_ground_truth()
    gt.push_uci("e2e4")
    assert sim.board.fen() == chess.Board().fen()


# ---------------------------------------------------------------- observations

def test_settled_observation_matches_board(sim):
    sim.apply_move(chess.Move.from_uci("e2e4"))
    probs, usable, settle = observe(sim)
    assert probs.shape == (192,)
    assert usable
    assert 0.0 <= settle <= 1.0
    assert np.array_equal(decode(probs), true_classes(sim.board))


def test_observation_is_cached(sim):
    probs, *_ = observe(sim)
    assert np.array_equal(sim.noisy_observation, probs)


def test_lifted_observation_uses_pre_move_board(sim):
    """Requires the fix that stores board_before_move / last_move."""
    sim.apply_move(chess.Move.from_uci("e2e4"))
    probs, usable, settle = observe(sim, ObservationPhase.LIFTED)
    got = decode(probs)
    assert got[chess.E2] == Color.EMPTY
    assert got[chess.E4] == Color.EMPTY  # pawn is in hand, not yet placed
    assert got[chess.D2] == Color.WHITE
    assert not usable
    assert 0.0 <= settle <= 1.0


def test_moving_capture_observation(sim):
    sim.apply_move(chess.Move.from_uci("e2e4"))
    sim.apply_move(chess.Move.from_uci("d7d5"))
    sim.apply_move(chess.Move.from_uci("e4d5"))
    probs, usable, _ = observe(sim, ObservationPhase.MOVING)
    got = decode(probs)
    assert got[chess.E4] == Color.EMPTY
    assert got[chess.D5] == Color.EMPTY
    assert not usable


def test_midmove_phase_without_a_move_raises(sim):
    with pytest.raises((RuntimeError, ValueError)):
        sim.get_observation(phase=ObservationPhase.LIFTED)


def test_occluded_settled_observation_is_unusable():
    sim = FakeSimulation(make_config(occlusion_rate=1.0, max_occlusion_size=2), seed=0)
    _, usable, _ = observe(sim)
    assert not usable


# ---------------------------------------------------------------- reset

def test_reset_restores_start_and_rewinds_pgn(sim, pgn_file):
    sim.parse_pgn(pgn_file)
    sim.apply_move(sim.peek_pgn_move())
    observe(sim)
    sim.reset(seed=1)
    assert sim.board.fen() == chess.Board().fen()
    assert sim.current_node is sim.game
    assert sim.noisy_observation is None
    assert sim.peek_pgn_move() == chess.Move.from_uci("e2e4")


def test_reset_with_same_seed_reproduces_noise():
    cfg = make_config(independent_error_rate=0.2)
    sim = FakeSimulation(cfg, seed=5)
    first, *_ = observe(sim)
    sim.reset(seed=5)
    second, *_ = observe(sim)
    assert np.array_equal(first, second)