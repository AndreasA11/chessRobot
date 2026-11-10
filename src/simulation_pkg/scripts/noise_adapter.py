from simulation_types import Color, ObservationPhase, NoiseConfig

import chess
import numpy as np

class ObservationNoise:

    NUM_SQUARES = 64
    NUM_CLASSES = 3

    def __init__(self, config: NoiseConfig | None = None, seed: int | None = None):
        self.config = config or NoiseConfig()
        self.rng = np.random.default_rng(seed)
        self._validate_config()
        

    def _validate_config(self) -> None:
        probability_fields = [
            self.config.independent_error_rate,
            self.config.confident_error_rate,
            self.config.confident_error_probability,
            self.config.correlated_error_rate,
            self.config.correlated_neighbor_rate,
            self.config.occlusion_rate,
            self.config.confidence,
        ]
        for probability in probability_fields:
            if not 0.0 <= probability <= 1.0:
                raise ValueError("Noise probabilities must be between 0.0 and 1.0")
        if self.config.max_occlusion_size < 1:
            raise ValueError("max_occlusion_size must be at least 1")
        if self.config.max_occlusion_size > 8:
            raise ValueError("max_occlusion_size cannot exceed 8")

    def _true_class(self, board: chess.Board, square: int) -> int:
        piece = board.piece_at(square)
        if piece is None:
            return Color.EMPTY
        return Color.WHITE if piece.color == chess.WHITE else Color.BLACK

    def _board_classes(self, board: chess.Board) -> np.ndarray:
        return np.array(
            [self._true_class(board, sq) for sq in chess.SQUARES], dtype=np.int8
        )

    def _random_incorrect_class(self, true_class: int) -> int:
        incorrect = [c for c in range(self.NUM_CLASSES) if c != true_class]
        return int(self.rng.choice(incorrect))

    def _one_hot(self, class_index: int) -> np.ndarray:
        p = np.zeros(self.NUM_CLASSES, dtype=np.float32)
        p[class_index] = 1.0
        return p

    def _soft_probabilities(self, predicted_class: int, confidence: float) -> np.ndarray:
        p = np.full(self.NUM_CLASSES, (1.0 - confidence) / 2.0, dtype=np.float32)
        p[predicted_class] = confidence
        return p

    def _neighbors(self, square: int) -> list[int]:
        rank = chess.square_rank(square)
        file = chess.square_file(square)
        neighbors = []
        for dr in (-1, 0, 1):
            for df in (-1, 0, 1):
                if dr == 0 and df == 0:
                    continue
                r, f = rank + dr, file + df
                if 0 <= r < 8 and 0 <= f < 8:
                    neighbors.append(chess.square(f, r))
        return neighbors

    def _apply_correlated_errors(self, true_classes, predicted_classes) -> None:
        if self.rng.random() >= self.config.correlated_error_rate:
            return
        center = int(self.rng.integers(0, self.NUM_SQUARES))
        affected = [center]
        for n in self._neighbors(center):
            if self.rng.random() < self.config.correlated_neighbor_rate:
                affected.append(n)
        for square in affected:
            predicted_classes[square] = self._random_incorrect_class(
                int(true_classes[square])
            )

    def _generate_occlusion_mask(self) -> np.ndarray:
        mask = np.zeros(self.NUM_SQUARES, dtype=bool)
        if self.rng.random() >= self.config.occlusion_rate:
            return mask
        size = int(self.rng.integers(1, self.config.max_occlusion_size + 1))
        start_rank = int(self.rng.integers(0, 9 - size))
        start_file = int(self.rng.integers(0, 9 - size))
        for rank in range(start_rank, start_rank + size):
            for file in range(start_file, start_file + size):
                mask[chess.square(file, rank)] = True
        return mask

    def _get_phase_classes(
        self,
        board: chess.Board,
        move: chess.Move | None,
        phase: ObservationPhase,
    ) -> np.ndarray:
        """
        Ground-truth square classes for one observation phase.
        `board` must be the position BEFORE the move.

        SETTLED: the board as given (use the post-move board for "settled after").
        LIFTED:  source square empty; everything else unchanged
                 (a captured piece is still on its square).
        MOVING:  move partly executed:
          - capture:     captured piece removed, mover still in hand
                         (source AND destination empty)
          - en passant:  captured pawn removed, mover in hand
                         (source, destination and captured-pawn square empty)
          - castling:    king on its destination, rook still on its origin
          - otherwise:   identical to the post-move board
                         (includes promotions: only the class matters, not the piece)
        """
        classes = self._board_classes(board)

        if phase == ObservationPhase.SETTLED:
            return classes

        if move is None:
            raise ValueError("A move is required for mid-move observations.")
        if move not in board.legal_moves:
            raise ValueError(f"Illegal move {move.uci()} in {board.fen()}")

        classes[move.from_square] = Color.EMPTY

        if phase == ObservationPhase.LIFTED:
            return classes

        if phase == ObservationPhase.MOVING:
            after = board.copy()
            after.push(move)
            after_classes = self._board_classes(after)

            if board.is_castling(move):
                rank = chess.square_rank(move.from_square)
                kingside = chess.square_file(move.to_square) > chess.square_file(move.from_square)
                rook_from = chess.square(7 if kingside else 0, rank)
                rook_to = chess.square(5 if kingside else 3, rank)
                partial = after_classes.copy()
                partial[rook_to] = Color.EMPTY
                partial[rook_from] = self._true_class(board, rook_from)
                return partial

            if board.is_en_passant(move):
                captured = chess.square(
                    chess.square_file(move.to_square),
                    chess.square_rank(move.from_square),
                )
                classes[captured] = Color.EMPTY
                return classes  # destination is still empty too

            if board.is_capture(move):
                classes[move.to_square] = Color.EMPTY
                return classes

            return after_classes

        raise ValueError(f"Unsupported phase: {phase}")

    def generate(
        self,
        board: chess.Board,
        move: chess.Move | None = None,
        phase: ObservationPhase = ObservationPhase.SETTLED,
    ) -> tuple[np.ndarray, bool]:
        """
        Returns (probabilities float32 (192,), usable).
        `board` must be the position BEFORE `move` for LIFTED/MOVING phases.
        """
        true_classes = self._get_phase_classes(board, move, phase)
        predicted_classes = true_classes.copy()

        # Stage 1: independent errors.
        for square in chess.SQUARES:
            if self.rng.random() < self.config.independent_error_rate:
                predicted_classes[square] = self._random_incorrect_class(
                    int(true_classes[square])
                )

        # Stage 2: confidently incorrect predictions. Remember WHICH squares
        # got one, so stage 6 assigns their confidence deterministically
        # instead of re-rolling the dice.
        confident_error_mask = np.zeros(self.NUM_SQUARES, dtype=bool)
        for square in chess.SQUARES:
            if self.rng.random() < self.config.confident_error_rate:
                predicted_classes[square] = self._random_incorrect_class(
                    int(true_classes[square])
                )
                confident_error_mask[square] = True

        # Stage 3: correlated errors (they keep the normal `confidence`;
        # a square already in the confident mask stays confident).
        self._apply_correlated_errors(true_classes, predicted_classes)

        # Stage 4: block occlusion.
        occlusion_mask = self._generate_occlusion_mask()

        probabilities = np.zeros((self.NUM_SQUARES, self.NUM_CLASSES), dtype=np.float32)

        for square in chess.SQUARES:
            if occlusion_mask[square]:
                probabilities[square] = np.full(3, 1.0 / 3.0, dtype=np.float32)
                continue

            predicted_class = int(predicted_classes[square])

            # Stage 6: soft probabilities.
            if self.config.soft_observations:
                if confident_error_mask[square]:
                    confidence = self.config.confident_error_probability
                else:
                    confidence = self.config.confidence
                probabilities[square] = self._soft_probabilities(predicted_class, confidence)
            else:
                probabilities[square] = self._one_hot(predicted_class)

        usable = not np.any(occlusion_mask)
        return probabilities.flatten(), usable