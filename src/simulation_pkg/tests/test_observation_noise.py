import chess
import numpy as np
import pytest

import sys
sys.path.insert(0, "../scripts") 

from simulation_types import Color
from noise_adapter import NoiseConfig, ObservationNoise, ObservationPhase


from conftest import decode, make_config, true_classes

E, W, B = Color.EMPTY.value, Color.WHITE.value, Color.BLACK.value
P = ObservationPhase


def clean_classes(board, move=None, phase=P.SETTLED):
    """Noise-free observation decoded back to a (64,) class array."""
    noise = ObservationNoise(NoiseConfig(independent_error_rate=0.0), seed=0)
    probs, usable = noise.generate(board, move, phase)
    assert usable
    return probs.reshape(64, 3).argmax(axis=1)


def sq(name):
    return chess.parse_square(name)


# ---------------------------------------------------------------- probabilities

ALL_NOISE = dict(independent_error_rate=0.2, confident_error_rate=0.2,
                 correlated_error_rate=0.5, occlusion_rate=0.5)


@pytest.mark.parametrize("cfg", [
    dict(),
    dict(soft_observations=True),
    dict(soft_observations=True, confidence=0.6, confident_error_probability=0.999),
    dict(soft_observations=False, **ALL_NOISE),
    dict(soft_observations=True, **ALL_NOISE),
])
@pytest.mark.parametrize("phase", list(P))
def test_distributions_sum_to_one(cfg, phase):
    board = chess.Board()
    move = chess.Move.from_uci("e2e4")
    for seed in range(25):
        noise = ObservationNoise(NoiseConfig(**cfg), seed=seed)
        probs, _ = noise.generate(board, move, phase)
        assert probs.shape == (192,) and probs.dtype == np.float32
        per_square = probs.reshape(64, 3)
        assert np.all(per_square >= 0.0)
        np.testing.assert_allclose(per_square.sum(axis=1), 1.0, atol=1e-5)


# ------------------------------------------------------- confident-error tracking

def test_every_confident_error_gets_confident_probability():
    cfg = NoiseConfig(independent_error_rate=0.0, confident_error_rate=1.0,
                      soft_observations=True, confidence=0.80,
                      confident_error_probability=0.99)
    probs, _ = ObservationNoise(cfg, seed=1).generate(chess.Board())
    p = probs.reshape(64, 3)
    truth = clean_classes(chess.Board())
    assert np.allclose(p.max(axis=1), 0.99)
    assert np.all(p.argmax(axis=1) != truth)


def test_ordinary_errors_do_not_get_confident_probability():
    cfg = NoiseConfig(independent_error_rate=1.0, confident_error_rate=0.0,
                      soft_observations=True, confidence=0.80,
                      confident_error_probability=0.99)
    p = ObservationNoise(cfg, seed=2).generate(chess.Board())[0].reshape(64, 3)
    assert np.allclose(p.max(axis=1), 0.80)


def test_confident_probability_only_on_wrong_squares():
    # Mixed case: any square at the confident level must be a wrong prediction,
    # and correct squares must sit at the normal level.
    cfg = NoiseConfig(independent_error_rate=0.0, confident_error_rate=0.4,
                      soft_observations=True, confidence=0.80,
                      confident_error_probability=0.99)
    truth = clean_classes(chess.Board())
    saw_confident = False
    for seed in range(30):
        p = ObservationNoise(cfg, seed=seed).generate(chess.Board())[0].reshape(64, 3)
        conf = np.isclose(p.max(axis=1), 0.99)
        saw_confident |= conf.any()
        assert np.all(p.argmax(axis=1)[conf] != truth[conf])
        assert np.allclose(p.max(axis=1)[~conf], 0.80)
    assert saw_confident


# --------------------------------------------------------------------- occlusion

def test_occlusion_marks_frame_unusable_with_uniform_squares():
    cfg = NoiseConfig(independent_error_rate=0.0, occlusion_rate=1.0)
    probs, usable = ObservationNoise(cfg, seed=0).generate(chess.Board())
    assert not usable
    assert np.any(np.isclose(probs.reshape(64, 3), 1 / 3).all(axis=1))


# -------------------------------------------------------------- phase semantics

def test_settled_matches_board():
    b = chess.Board()
    c = clean_classes(b)
    assert c[sq("a1")] == W and c[sq("e8")] == B and c[sq("e4")] == E


def test_quiet_move_lifted_and_moving():
    b = chess.Board()
    m = chess.Move.from_uci("e2e4")
    lifted = clean_classes(b, m, P.LIFTED)
    assert lifted[sq("e2")] == E and lifted[sq("e4")] == E
    moving = clean_classes(b, m, P.MOVING)
    assert moving[sq("e2")] == E and moving[sq("e4")] == W


def test_capture():
    b = chess.Board()
    for u in ("e2e4", "d7d5"):
        b.push_uci(u)
    m = chess.Move.from_uci("e4d5")
    lifted = clean_classes(b, m, P.LIFTED)
    assert lifted[sq("e4")] == E and lifted[sq("d5")] == B      # victim still there
    moving = clean_classes(b, m, P.MOVING)
    assert moving[sq("e4")] == E and moving[sq("d5")] == E      # victim removed, mover in hand
    after = b.copy(); after.push(m)
    assert clean_classes(after)[sq("d5")] == W


def test_en_passant():
    b = chess.Board("8/8/8/3pP3/8/8/8/4K2k w - d6 0 1")
    m = chess.Move.from_uci("e5d6")
    assert b.is_en_passant(m)
    lifted = clean_classes(b, m, P.LIFTED)
    assert lifted[sq("e5")] == E and lifted[sq("d5")] == B and lifted[sq("d6")] == E
    moving = clean_classes(b, m, P.MOVING)
    assert moving[sq("e5")] == E and moving[sq("d5")] == E and moving[sq("d6")] == E
    after = b.copy(); after.push(m)
    c = clean_classes(after)
    assert c[sq("d6")] == W and c[sq("d5")] == E


@pytest.mark.parametrize("fen,uci,king_to,rook_from,rook_to,color", [
    ("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1", "g1", "h1", "f1", W),
    ("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1c1", "c1", "a1", "d1", W),
    ("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8g8", "g8", "h8", "f8", B),
    ("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8", "c8", "a8", "d8", B),
])
def test_castling(fen, uci, king_to, rook_from, rook_to, color):
    b = chess.Board(fen)
    m = chess.Move.from_uci(uci)
    assert b.is_castling(m)
    king_from = uci[:2]
    lifted = clean_classes(b, m, P.LIFTED)
    assert lifted[sq(king_from)] == E and lifted[sq(king_to)] == E
    assert lifted[sq(rook_from)] == color and lifted[sq(rook_to)] == E
    moving = clean_classes(b, m, P.MOVING)       # king moved, rook not yet
    assert moving[sq(king_from)] == E and moving[sq(king_to)] == color
    assert moving[sq(rook_from)] == color and moving[sq(rook_to)] == E
    after = b.copy(); after.push(m)
    done = clean_classes(after)
    assert done[sq(rook_from)] == E and done[sq(rook_to)] == color


@pytest.mark.parametrize("piece", "qrbn")
def test_promotion(piece):
    b = chess.Board("8/P6k/8/8/8/8/8/K7 w - - 0 1")
    m = chess.Move.from_uci(f"a7a8{piece}")
    lifted = clean_classes(b, m, P.LIFTED)
    assert lifted[sq("a7")] == E and lifted[sq("a8")] == E
    moving = clean_classes(b, m, P.MOVING)
    assert moving[sq("a7")] == E and moving[sq("a8")] == W      # class only, piece type invisible


@pytest.mark.parametrize("piece", "qn")
def test_promotion_with_capture(piece):
    b = chess.Board("1n5k/P7/8/8/8/8/8/K7 w - - 0 1")
    m = chess.Move.from_uci(f"a7b8{piece}")
    assert b.is_capture(m)
    lifted = clean_classes(b, m, P.LIFTED)
    assert lifted[sq("a7")] == E and lifted[sq("b8")] == B
    moving = clean_classes(b, m, P.MOVING)
    assert moving[sq("a7")] == E and moving[sq("b8")] == E
    after = b.copy(); after.push(m)
    assert clean_classes(after)[sq("b8")] == W


# ------------------------------------------------------------------ error paths

def test_illegal_move_rejected():
    noise = ObservationNoise(seed=0)
    with pytest.raises(ValueError):
        noise.generate(chess.Board(), chess.Move.from_uci("e2e5"), P.LIFTED)


def test_phase_without_move_rejected():
    with pytest.raises(ValueError):
        ObservationNoise(seed=0).generate(chess.Board(), None, P.MOVING)




def test_confident_errors_receive_high_confidence():
    cfg = NoiseConfig(
        independent_error_rate=0.0,
        confident_error_rate=1.0,
        soft_observations=True,
        confidence=0.80,
        confident_error_probability=0.99,
    )

    board = chess.Board()
    truth = clean_classes(board)

    probs, _ = ObservationNoise(cfg, seed=0).generate(board)
    p = probs.reshape(64, 3)

    predicted = p.argmax(axis=1)
    max_probability = p.max(axis=1)

    # Every square should have high confidence.
    assert np.allclose(max_probability, 0.99)

    # Every square should be incorrectly classified.
    assert np.all(predicted != truth)



def test_ordinary_errors_receive_normal_confidence():
    cfg = NoiseConfig(
        independent_error_rate=1.0,
        confident_error_rate=0.0,
        soft_observations=True,
        confidence=0.80,
        confident_error_probability=0.99,
    )

    board = chess.Board()
    truth = clean_classes(board)

    probs, _ = ObservationNoise(cfg, seed=0).generate(board)
    p = probs.reshape(64, 3)

    predicted = p.argmax(axis=1)
    max_probability = p.max(axis=1)

    # Every square should have normal confidence.
    assert np.allclose(max_probability, 0.80)

    # Every square should be incorrectly classified.
    assert np.all(predicted != truth)



def test_confident_probability_only_on_wrong_squares():
    cfg = NoiseConfig(
        independent_error_rate=0.0,
        confident_error_rate=0.4,
        soft_observations=True,
        confidence=0.80,
        confident_error_probability=0.99,
    )

    board = chess.Board()
    truth = clean_classes(board)

    saw_confident = False
    saw_normal = False

    for seed in range(30):
        probs, _ = ObservationNoise(cfg, seed=seed).generate(board)
        p = probs.reshape(64, 3)

        predicted = p.argmax(axis=1)
        max_probability = p.max(axis=1)

        confident = np.isclose(max_probability, 0.99)
        normal = np.isclose(max_probability, 0.80)

        # Confident predictions must be wrong.
        assert np.all(predicted[confident] != truth[confident])

        # Normal-confidence predictions should be correct because
        # independent_error_rate is zero.
        assert np.all(predicted[normal] == truth[normal])

        saw_confident |= confident.any()
        saw_normal |= normal.any()

    # Across the seeds, both categories should appear.
    assert saw_confident
    assert saw_normal





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