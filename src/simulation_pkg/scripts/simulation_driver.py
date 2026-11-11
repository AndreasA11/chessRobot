from fake_simulation import FakeSimulation
from simulation_types import NoiseConfig, Mode
from dataclasses import fields

import rclpy
from rclpy.node import Node



from board_observation_interface.msg import BoardObservation

class observation_publisher(Node):

    def __init__(self):
        super().__init__('observation_publisher')

        self.publisher_ = self.create_publisher(BoardObservation,'camera_board_observation',10)

        # General simulation parameters
        self.declare_parameter('seed', 0)
        seed_value = self.get_parameter('seed').value

        self.declare_parameter('mode', 0)
        mode_value = self.get_parameter('mode').value
        self.mode_ = Mode(mode_value)

        self.declare_parameter('no_noise', False)
        self.no_noise_ = self.get_parameter('no_noise').value

        # Construct noise configuration
        if self.no_noise_:
            noise_config = NoiseConfig(
                independent_error_rate=0.0,
                confident_error_rate=0.0,
                confident_error_probability=1.0,
                correlated_error_rate=0.0,
                correlated_neighbor_rate=0.0,
                occlusion_rate=0.0,
                max_occlusion_size=0,
                soft_observations=False,
                confidence=1.0,
            )

        else:
            default_config = NoiseConfig()

            # Declare one ROS parameter for each NoiseConfig field
            for field in fields(NoiseConfig):
                if not field.init:
                    continue

                param_name = f'noise.{field.name}'
                default_value = getattr(default_config, field.name)

                self.declare_parameter(param_name, default_value)

            # Retrieve parameters and construct NoiseConfig
            noise_params = self.get_parameters_by_prefix('noise')

            config_dict = {
                name: param.value
                for name, param in noise_params.items()
            }

            noise_config = NoiseConfig(**config_dict)

        # Initialize simulation
        self.sim = FakeSimulation(noise_config=noise_config,seed=seed_value)
        self.sim.mode = self.mode_


    def publish_board_observation(self, board_obs_in : BoardObservation):
            msg = board_obs_in
            self.publisher_.publish(msg)
            self.get_logger().info('Publishing: "%s"' % msg.data)

    def png_obs(self):
        board_observation = None
        if(board_observation == None):
            raise ValueError(f"No observation was made: {board_observation}")
        self.publish_board_observation(board_observation)
      

    def human_obs(self):
        board_observation = None
        if(board_observation == None):
            raise ValueError(f"No observation was made: {board_observation}")
        self.publish_board_observation(board_observation)
       

    def run_obs(self) :
        if(self.mode_value == Mode.NA):
            raise ValueError(f"No Mode was selected: {self.mode_value}")
        elif(self.mode_value == Mode.PNG):
            self.png_obs()
        else:
            self.human_obs()


def main(args=None):
    rclpy.init(args=args)
    minimal_publisher = observation_publisher()
    rclpy.spin(minimal_publisher)
    minimal_publisher.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()
