
from numpy import np

class Inference:
    def infer_hard_labels(
        self,
        observation: np.ndarray,
    ) -> np.ndarray:
        probabilities = observation.reshape(64, 3)
        return probabilities.argmax(axis=1)