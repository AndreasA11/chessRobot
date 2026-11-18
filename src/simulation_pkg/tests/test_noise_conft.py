
import chess
import numpy as np
import pytest

from conftest import decode, make_config, true_classes

import sys
sys.path.insert(0, "../scripts") 

from simulation_types import Color
from fake_simulation import FakeSimulation
from noise_adapter import NoiseConfig, ObservationNoise, ObservationPhase

# ---------------------------------------------------------------- config

@pytest.mark.parametrize(
    "field",
    [
        "independent_error_rate",
        "confident_error_rate",
        "confident_error_probability",
        "correlated_error_rate",
        "correlated_neighbor_rate",
        "occlusion_rate",
        "confidence",
    ],
)
@pytest.mark.parametrize("bad", [-0.1, 1.1])
def test_probability_fields_validated(field, bad):
    with pytest.raises(ValueError):
        ObservationNoise(make_config(**{field: bad}))


@pytest.mark.parametrize("size", [0, 9])
def test_occlusion_size_validated(size):
    with pytest.raises(ValueError):
        ObservationNoise(make_config(max_occlusion_size=size))


def test_no_noise_publisher_config_is_valid():
    """The config the publisher builds for no_noise=True must pass validation."""
    ObservationNoise(
        make_config(
            independent_error_rate=0.0,
            confident_error_rate=0.0,
            confident_error_probability=1.0,
            correlated_error_rate=0.0,
            correlated_neighbor_rate=0.0,
            occlusion_rate=0.0,
            max_occlusion_size=1,
            soft_observations=False,
            confidence=1.0,
        )
    )


# ---------------------------------------------------------------- output format

def test_output_shape_dtype_and_row_sums():
    noise = ObservationNoise(make_config(soft_observations=True, confidence=0.8), seed=1)
    probs, usable = noise.generate(chess.Board())
    assert probs.shape == (192,)
    assert probs.dtype == np.float32
    assert np.allclose(probs.reshape(64, 3).sum(axis=1), 1.0, atol=1e-6)
    assert isinstance(usable, bool)


def test_clean_observation_is_one_hot_ground_truth():
    board = chess.Board()
    probs, usable = ObservationNoise(make_config(), seed=0).generate(board)
    assert usable
    assert set(np.unique(probs)) == {0.0, 1.0}
    assert np.array_equal(decode(probs), true_classes(board))


def test_square_ordering_is_a1_to_h8_empty_white_black():
    probs, _ = ObservationNoise(make_config(), seed=0).generate(chess.Board())
    grid = probs.reshape(64, 3)
    assert grid[chess.A1].argmax() == Color.WHITE
    assert grid[chess.E1].argmax() == Color.WHITE
    assert grid[chess.E4].argmax() == Color.EMPTY
    assert grid[chess.H8].argmax() == Color.BLACK
    assert (Color.EMPTY, Color.WHITE, Color.BLACK) == (0, 1, 2)


def test_soft_probabilities_use_configured_confidence():
    noise = ObservationNoise(make_config(soft_observations=True, confidence=0.8), seed=0)
    probs, _ = noise.generate(chess.Board())
    grid = probs.reshape(64, 3)
    assert np.allclose(grid.max(axis=1), 0.8)
    assert np.allclose(np.sort(grid, axis=1)[:, 0], 0.1)


# ---------------------------------------------------------------- determinism

def test_same_seed_same_output():
    cfg = make_config(independent_error_rate=0.2, occlusion_rate=0.3, max_occlusion_size=4)
    a = ObservationNoise(cfg, seed=42).generate(chess.Board())
    b = ObservationNoise(cfg, seed=42).generate(chess.Board())
    assert np.array_equal(a[0], b[0]) and a[1] == b[1]


def test_different_seeds_differ():
    cfg = make_config(independent_error_rate=0.3)
    a, _ = ObservationNoise(cfg, seed=1).generate(chess.Board())
    b, _ = ObservationNoise(cfg, seed=2).generate(chess.Board())
    assert not np.array_equal(a, b)


# ---------------------------------------------------------------- noise stages

def test_independent_errors_flip_every_square_at_rate_one():
    board = chess.Board()
    probs, _ = ObservationNoise(make_config(independent_error_rate=1.0), seed=0).generate(board)
    assert np.all(decode(probs) != true_classes(board))


def test_independent_error_rate_is_roughly_respected():
    board = chess.Board()
    noise = ObservationNoise(make_config(independent_error_rate=0.1), seed=3)
    wrong = sum(
        int(np.sum(decode(noise.generate(board)[0]) != true_classes(board)))
        for _ in range(200)
    )
    assert 0.07 < wrong / (200 * 64) < 0.13


def test_confident_errors_are_wrong_and_use_confident_probability():
    board = chess.Board()
    cfg = make_config(
        confident_error_rate=1.0,
        confident_error_probability=0.9,
        soft_observations=True,
        confidence=0.6,
    )
    probs, _ = ObservationNoise(cfg, seed=0).generate(board)
    grid = probs.reshape(64, 3)
    assert np.all(grid.argmax(axis=1) != true_classes(board))
    assert np.allclose(grid.max(axis=1), 0.9)


def test_correlated_error_hits_exactly_one_square_without_neighbors():
    board = chess.Board()
    cfg = make_config(correlated_error_rate=1.0, correlated_neighbor_rate=0.0)
    probs, _ = ObservationNoise(cfg, seed=0).generate(board)
    assert np.sum(decode(probs) != true_classes(board)) == 1


def test_correlated_error_hits_whole_neighborhood_at_rate_one():
    board = chess.Board()
    cfg = make_config(correlated_error_rate=1.0, correlated_neighbor_rate=1.0)
    probs, _ = ObservationNoise(cfg, seed=0).generate(board)
    wrong = np.flatnonzero(decode(probs) != true_classes(board))
    assert 4 <= len(wrong) <= 9  # corner=4, edge=6, interior=9
    # All wrong squares lie within one 3x3 block.
    ranks = [chess.square_rank(s) for s in wrong]
    files = [chess.square_file(s) for s in wrong]
    assert max(ranks) - min(ranks) <= 2 and max(files) - min(files) <= 2


def test_occlusion_marks_unusable_and_uniform():
    cfg = make_config(occlusion_rate=1.0, max_occlusion_size=3)
    for seed in range(20):
        probs, usable = ObservationNoise(cfg, seed=seed).generate(chess.Board())
        grid = probs.reshape(64, 3)
        uniform = np.all(np.isclose(grid, 1 / 3), axis=1)
        assert not usable
        assert 1 <= uniform.sum() <= 9


def test_full_board_occlusion_is_in_bounds():
    cfg = make_config(occlusion_rate=1.0, max_occlusion_size=8)
    for seed in range(50):
        ObservationNoise(cfg, seed=seed).generate(chess.Board())  # must not raise


def test_no_occlusion_means_usable():
    _, usable = ObservationNoise(make_config(), seed=0).generate(chess.Board())
    assert usable


# ---------------------------------------------------------------- phases

def classes(board, move, phase):
    return ObservationNoise(make_config())._get_phase_classes(board, move, phase)


def test_settled_returns_board_as_is():
    board = chess.Board()
    assert np.array_equal(classes(board, None, ObservationPhase.SETTLED), true_classes(board))


def test_midmove_phases_require_a_move():
    for phase in (ObservationPhase.LIFTED, ObservationPhase.MOVING):
        with pytest.raises(ValueError):
            classes(chess.Board(), None, phase)


def test_illegal_move_rejected():
    with pytest.raises(ValueError):
        classes(chess.Board(), chess.Move.from_uci("e2e5"), ObservationPhase.LIFTED)


def test_lifted_empties_source_only():
    board = chess.Board()
    move = chess.Move.from_uci("e2e4")
    got = classes(board, move, ObservationPhase.LIFTED)
    expected = true_classes(board)
    expected[chess.E2] = Color.EMPTY
    assert np.array_equal(got, expected)


def test_moving_quiet_move_equals_post_move_board():
    board = chess.Board()
    move = chess.Move.from_uci("e2e4")
    after = board.copy()
    after.push(move)
    got = classes(board, move, ObservationPhase.MOVING)
    assert np.array_equal(got, true_classes(after))


def test_moving_capture_leaves_both_squares_empty():
    board = chess.Board()
    for uci in ("e2e4", "d7d5"):
        board.push_uci(uci)
    move = chess.Move.from_uci("e4d5")

    lifted = classes(board, move, ObservationPhase.LIFTED)
    assert lifted[chess.E4] == Color.EMPTY
    assert lifted[chess.D5] == Color.BLACK  # victim still on the board

    moving = classes(board, move, ObservationPhase.MOVING)
    assert moving[chess.E4] == Color.EMPTY
    assert moving[chess.D5] == Color.EMPTY  # victim removed, mover in hand


def test_moving_en_passant_clears_three_squares():
    board = chess.Board("rnbqkbnr/ppp1p1pp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3")
    move = chess.Move.from_uci("e5d6")
    assert board.is_en_passant(move)
    got = classes(board, move, ObservationPhase.MOVING)
    assert got[chess.E5] == Color.EMPTY
    assert got[chess.D6] == Color.EMPTY
    assert got[chess.D5] == Color.EMPTY  # captured pawn


@pytest.mark.parametrize(
    "uci,king_to,rook_from,rook_to",
    [
        ("e1g1", chess.G1, chess.H1, chess.F1),
        ("e1c1", chess.C1, chess.A1, chess.D1),
    ],
)
def test_moving_castling_king_moved_rook_not_yet(uci, king_to, rook_from, rook_to):
    board = chess.Board("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1")
    move = chess.Move.from_uci(uci)
    assert board.is_castling(move)
    got = classes(board, move, ObservationPhase.MOVING)
    assert got[chess.E1] == Color.EMPTY
    assert got[king_to] == Color.WHITE
    assert got[rook_from] == Color.WHITE  # rook still on its origin
    assert got[rook_to] == Color.EMPTY


def test_generate_in_midmove_phase_does_not_mutate_board():
    board = chess.Board()
    fen = board.fen()
    ObservationNoise(make_config(), seed=0).generate(
        board, chess.Move.from_uci("e2e4"), ObservationPhase.MOVING
    )
    assert board.fen() == fen