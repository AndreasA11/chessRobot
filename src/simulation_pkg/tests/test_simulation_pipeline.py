"""End-to-end pipeline tests: ObservationPublisher -> /camera_board_observation.

Requires a sourced ROS 2 workspace containing board_observation_interface.
Skipped automatically otherwise.

Adjust PUBLISHER_MODULE if your node file has a different name.
"""
import importlib
import time

import chess
import numpy as np
import pytest

rclpy = pytest.importorskip("rclpy")
pytest.importorskip("board_observation_interface.msg")

from rclpy.executors import SingleThreadedExecutor  # noqa: E402
from rclpy.node import Node  # noqa: E402
from std_msgs.msg import String  # noqa: E402
from board_observation_interface.msg import BoardObservation  # noqa: E402

from conftest import decode, true_classes  # noqa: E402
from simulation_types import Mode  # noqa: E402

PUBLISHER_MODULE = "simulation_driver"
TOPIC = "camera_board_observation"
TIMEOUT = 10.0


# ---------------------------------------------------------------- harness

class Harness:
    """Owns rclpy context, the node under test, a collector node and an executor."""

    def __init__(self, params: dict):
        args = ["--ros-args"]
        for key, value in params.items():
            text = str(value).lower() if isinstance(value, bool) else str(value)
            args += ["-p", f"{key}:={text}"]
        rclpy.init(args=args)

        module = importlib.import_module(PUBLISHER_MODULE)
        self.node = module.ObservationPublisher()

        self.received: list[BoardObservation] = []
        self.probe = Node("pipeline_probe")
        self.probe.create_subscription(BoardObservation, TOPIC, self.received.append, 10)
        self.human_pub = self.probe.create_publisher(String, "human_move", 10)

        self.executor = SingleThreadedExecutor()
        self.executor.add_node(self.node)
        self.executor.add_node(self.probe)

        # Wait for discovery so the first message is not dropped.
        self.spin_until(lambda: self.node.publisher_.get_subscription_count() >= 1)

    def spin_until(self, condition, timeout=TIMEOUT) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if condition():
                return True
            self.executor.spin_once(timeout_sec=0.02)
        return condition()

    def spin_for(self, seconds: float):
        self.spin_until(lambda: False, timeout=seconds)

    def close(self):
        self.executor.shutdown()
        self.probe.destroy_node()
        self.node.destroy_node()
        rclpy.shutdown()


@pytest.fixture
def make_harness():
    created = []

    def _make(**params):
        h = Harness(params)
        created.append(h)
        return h

    yield _make
    for h in created:
        h.close()


def pgn_params(pgn_file, **extra):
    return dict(
        seed=0,
        mode=Mode.PNG.value,
        no_noise=True,
        pgn_file=pgn_file,
        replay_interval_sec=0.05,
        **extra,
    )


def expected_boards(ucis):
    board = chess.Board()
    out = [board.copy()]
    for uci in ucis:
        board.push_uci(uci)
        out.append(board.copy())
    return out


# ---------------------------------------------------------------- PGN mode

def test_node_starts_with_noise_free_config(make_harness, pgn_file):
    """Regression: no_noise=True used to crash on max_occlusion_size=0."""
    h = make_harness(**pgn_params(pgn_file))
    assert h.node.sim.mode == Mode.PNG


def test_pgn_replay_publishes_initial_plus_every_move(make_harness, pgn_file):
    h = make_harness(**pgn_params(pgn_file))
    h.node.run_obs()
    assert h.spin_until(lambda: len(h.received) >= 4)
    h.spin_for(0.4)  # nothing further should arrive after the game ends
    assert len(h.received) == 4


def test_pgn_replay_observations_match_ground_truth(make_harness, pgn_file):
    h = make_harness(**pgn_params(pgn_file))
    h.node.run_obs()
    assert h.spin_until(lambda: len(h.received) >= 4)

    boards = expected_boards(["e2e4", "e7e5", "g1f3"])
    for msg, board in zip(h.received, boards):
        assert np.array_equal(decode(msg.probs), true_classes(board))
        assert msg.usable is True


def test_message_contract(make_harness, pgn_file):
    h = make_harness(**pgn_params(pgn_file))
    h.node.run_obs()
    assert h.spin_until(lambda: len(h.received) >= 1)
    msg = h.received[0]

    assert len(msg.probs) == 192
    assert msg.header.frame_id == "chessboard"
    assert msg.header.stamp.sec > 0 or msg.header.stamp.nanosec > 0
    assert isinstance(msg.usable, bool)
    assert 0.0 <= msg.settle_score <= 1.0
    grid = np.asarray(msg.probs, dtype=np.float32).reshape(64, 3)
    assert np.allclose(grid.sum(axis=1), 1.0, atol=1e-5)


def test_timestamps_are_monotonic(make_harness, pgn_file):
    h = make_harness(**pgn_params(pgn_file))
    h.node.run_obs()
    assert h.spin_until(lambda: len(h.received) >= 4)
    stamps = [(m.header.stamp.sec, m.header.stamp.nanosec) for m in h.received]
    assert stamps == sorted(stamps)


def test_noisy_pgn_replay_is_reproducible_for_a_seed(make_harness, pgn_file):
    """Two runs with identical seed and noise params publish identical data."""
    runs = []
    for _ in range(2):
        h = Harness(
            dict(
                seed=7,
                mode=Mode.PNG.value,
                no_noise=False,
                pgn_file=pgn_file,
                replay_interval_sec=0.05,
                **{"noise.independent_error_rate": 0.1},
            )
        )
        try:
            h.node.run_obs()
            assert h.spin_until(lambda: len(h.received) >= 4)
            runs.append([list(m.probs) for m in h.received[:4]])
        finally:
            h.close()
    assert runs[0] == runs[1]


def test_missing_pgn_file_param_raises(make_harness):
    h = make_harness(seed=0, mode=Mode.PNG.value, no_noise=True)
    with pytest.raises(ValueError):
        h.node.run_obs()


def test_invalid_mode_param_raises():
    rclpy.init(args=["--ros-args", "-p", "mode:=999"])
    try:
        module = importlib.import_module(PUBLISHER_MODULE)
        with pytest.raises(ValueError):
            module.ObservationPublisher()
    finally:
        rclpy.shutdown()


def test_na_mode_raises_on_run(make_harness):
    h = make_harness(seed=0, mode=Mode.NA.value, no_noise=True)
    with pytest.raises(ValueError):
        h.node.run_obs()


# ---------------------------------------------------------------- Human mode

def send_move(h: Harness, uci: str):
    h.spin_until(lambda: h.human_pub.get_subscription_count() >= 1)
    h.human_pub.publish(String(data=uci))


def test_human_mode_publishes_after_each_legal_move(make_harness):
    h = make_harness(seed=0, mode=Mode.HUMAN.value, no_noise=True)
    h.node.run_obs()

    send_move(h, "e2e4")
    assert h.spin_until(lambda: len(h.received) >= 1)
    send_move(h, "e7e5")
    assert h.spin_until(lambda: len(h.received) >= 2)

    boards = expected_boards(["e2e4", "e7e5"])
    assert np.array_equal(decode(h.received[0].probs), true_classes(boards[1]))
    assert np.array_equal(decode(h.received[1].probs), true_classes(boards[2]))


@pytest.mark.parametrize("bad_move", ["zzzz", "", "e2e5", "e7e5"])
def test_human_mode_ignores_invalid_or_illegal_moves(make_harness, bad_move):
    h = make_harness(seed=0, mode=Mode.HUMAN.value, no_noise=True)
    h.node.run_obs()

    send_move(h, bad_move)  # e7e5 is illegal on move 1 (it is White's turn)
    h.spin_for(0.5)
    assert len(h.received) == 0

    # The node must survive and keep working afterwards.
    send_move(h, "e2e4")
    assert h.spin_until(lambda: len(h.received) >= 1)
    assert len(h.received) == 1