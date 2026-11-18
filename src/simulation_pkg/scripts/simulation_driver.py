from fake_simulation import FakeSimulation
from simulation_types import NoiseConfig, Mode, ObservationPhase

from dataclasses import fields

import chess
import rclpy
from rclpy.node import Node

from std_msgs.msg import String
from board_observation_interface.msg import BoardObservation


class ObservationPublisher(Node):

    def __init__(self):
        super().__init__('observation_publisher')

        # Publisher
        self.publisher_ = self.create_publisher(
            BoardObservation,
            'camera_board_observation',
            10
        )

        # General simulation parameters
        self.declare_parameter('seed', 0)
        seed_value = self.get_parameter('seed').value

        self.declare_parameter('mode', 0)
        mode_value = self.get_parameter('mode').value

        try:
            self.mode_ = Mode(mode_value)
        except ValueError:
            raise ValueError(f"Invalid simulation mode: {mode_value}")

        self.declare_parameter('no_noise', False)
        self.no_noise_ = self.get_parameter('no_noise').value

        # PGN parameters
        self.declare_parameter('pgn_file', '')
        self.pgn_file_ = self.get_parameter('pgn_file').value

        self.declare_parameter('replay_interval_sec', 2.0)
        self.replay_interval_sec_ = self.get_parameter(
            'replay_interval_sec'
        ).value

        # Construct noise configuration
        if self.no_noise_:
            noise_config = NoiseConfig(
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

        else:
            default_config = NoiseConfig()

            for field in fields(NoiseConfig):
                if not field.init:
                    continue

                param_name = f'noise.{field.name}'
                default_value = getattr(default_config, field.name)

                self.declare_parameter(param_name, default_value)

            noise_params = self.get_parameters_by_prefix('noise')

            config_dict = {
                name: param.value
                for name, param in noise_params.items()
            }

            noise_config = NoiseConfig(**config_dict)

        # Initialize simulation
        self.sim = FakeSimulation(
            noise_config=noise_config,
            seed=seed_value
        )

        self.sim.mode = self.mode_

        # PGN replay state
        self.initial_position_published_ = False
        self.replay_timer_ = None

        # Human move subscription
        self.human_subscription_ = None

        self.get_logger().info(
            f"Observation publisher initialized: {self.mode_.name}"
        )

  

    def publish_board_observation(
        self,
        probs,
        usable,
        settle_score=0.0
    ):
        msg = BoardObservation()

        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = 'chessboard'

        if len(msg.probs) != 192:
            raise ValueError(
                f"Expected 192 probabilities, received {len(msg.probs)}"
            )

        msg.probs = probs.tolist() if hasattr(probs, 'tolist') else list(probs)
        msg.usable = bool(usable)
        msg.settle_score = float(settle_score)

        

        self.publisher_.publish(msg)

        self.get_logger().info(
            f"Published board observation. Usable: {msg.usable}"
        )

    def generate_and_publish_observation(self, phase=ObservationPhase.SETTLED):
        probs, usable, settle = self.sim.get_observation(phase=phase)
        self.publish_board_observation(probs, usable, settle)


    def pgn_obs(self):
        if not self.pgn_file_:
            raise ValueError("No PGN file was provided.")

        self.sim.parse_pgn(self.pgn_file_)

        self.get_logger().info(
            f"Loaded PGN: {self.pgn_file_}"
        )

        # Publish initial board position first, then advance moves.
        self.initial_position_published_ = False

        self.replay_timer_ = self.create_timer(
            self.replay_interval_sec_,
            self.pgn_replay_callback
        )

    def pgn_replay_callback(self):

        # First publish the initial position.
        if not self.initial_position_published_:
            self.generate_and_publish_observation()
            self.initial_position_published_ = True
            return

        # Check for the next PGN move.
        move = self.sim.peek_pgn_move()

        if move is None:
            self.get_logger().info("PGN replay finished.")
            self.replay_timer_.cancel()
            return

        # Advance the simulated board.
        self.sim.apply_move(move)

        self.get_logger().info(
            f"Applied PGN move: {move.uci()}"
        )

        # Publish the resulting board observation.
        self.generate_and_publish_observation()


    def human_obs(self):

        self.human_subscription_ = self.create_subscription(
            String,
            'human_move',
            self.human_move_callback,
            10
        )

        self.get_logger().info(
            "Human mode ready. Waiting for UCI moves on /human_move."
        )

    def human_move_callback(self, msg: String):

        try:
            move = chess.Move.from_uci(msg.data.strip())

            self.sim.apply_move(move)

            self.get_logger().info(
                f"Received human move: {move.uci()}"
            )

            self.generate_and_publish_observation()

        except (ValueError, chess.InvalidMoveError):
            self.get_logger().error(
                f"Invalid human move received: {msg.data}"
            )


    def run_obs(self):

        if self.mode_ == Mode.NA:
            raise ValueError("No simulation mode was selected.")

        elif self.mode_ == Mode.PGN:
            self.pgn_obs()

        elif self.mode_ == Mode.HUMAN:
            self.human_obs()

        else:
            raise ValueError(f"Unsupported mode: {self.mode_}")


def main(args=None):

    rclpy.init(args=args)

    observation_publisher = ObservationPublisher()

    try:
        observation_publisher.run_obs()
        rclpy.spin(observation_publisher)

    except KeyboardInterrupt:
        pass

    finally:
        observation_publisher.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()