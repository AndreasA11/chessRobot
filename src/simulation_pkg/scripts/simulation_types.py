

from enum import IntEnum
from enum import Enum
from dataclasses import dataclass

class Color(IntEnum):
    EMPTY = 0
    WHITE = 1
    BLACK = 2

class ObservationPhase(Enum):
    SETTLED = "settled"
    LIFTED = "lifted"
    MOVING = "moving"

class Mode(Enum):
    PNG = 1
    HUMAN = 2
    NA = 3

@dataclass
class NoiseConfig:
    # Stage 1: independent per-square errors
    independent_error_rate: float = 0.05

    # Stage 2: confidently incorrect predictions
    confident_error_rate: float = 0.0
    confident_error_probability: float = 0.99

    # Stage 3: correlated errors
    correlated_error_rate: float = 0.0
    correlated_neighbor_rate: float = 0.5

    # Stage 4: block occlusion
    occlusion_rate: float = 0.0
    max_occlusion_size: int = 3

    # Stage 6: soft probabilities
    soft_observations: bool = False
    confidence: float = 0.95