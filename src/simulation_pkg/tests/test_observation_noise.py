import chess
import numpy as np
import pytest

from FakeSimulation import Color
from ObservationNoise import NoiseConfig, ObservationNoise, ObservationPhase

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