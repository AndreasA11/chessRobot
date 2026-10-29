std_msgs/Header header
float32[192] probs      # 64 squares x [empty, white, black], row-major a1..h8
bool usable             # false if motion/occlusion detected
float32 settle_score    # frame-difference based, optional