import cv2
import ast
import glob
import os

POINTS_FILE = "calibration_points2.txt"
REQUIRED_POINTS = 6
calibration_points = []
calibrated = False

# Text instructions for each point
POINT_INSTRUCTIONS = [
    "1 - Top-Left of main ROI",
    "2 - Top-Right of main ROI",
    "3 - Bottom-Right of main ROI",
    "4 - Bottom-Left of main ROI",
    "5 - Left corner of C4 key",
    "6 - Right corner of C4 key"
]

def mouse_callback(event, x, y, flags, param):
    global calibration_points, calibrated
    if event == cv2.EVENT_LBUTTONDOWN:
        if len(calibration_points) < REQUIRED_POINTS:
            calibration_points.append((x, y))
            print(f"Point {len(calibration_points)} added: ({x}, {y})")

        if len(calibration_points) == REQUIRED_POINTS:
            calibrated = True

def run_calibration():
    global calibration_points, calibrated

    frame = cv2.imread("big_keys.jpg")
    cv2.namedWindow("Calibration")
    cv2.setMouseCallback("Calibration", mouse_callback)

    print("\n--- START CALIBRATION ---")
    print("Click 6 points in this order:")
    for instr in POINT_INSTRUCTIONS:
        print(instr)

    while not calibrated:
        display = frame.copy()

        # Draw current points
        for i, (x, y) in enumerate(calibration_points):
            color = (0, 255, 0) if i < 4 else (255, 0, 0)
            cv2.circle(display, (x, y), 5, color, -1)
            cv2.putText(display, str(i + 1), (x + 10, y - 10),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)

        # Display which point to click next
        h, w = display.shape[:2]
        if len(calibration_points) < REQUIRED_POINTS:
            instr_text = f"Next: {POINT_INSTRUCTIONS[len(calibration_points)]}"
            # Instruction text (above the status line)
            cv2.putText(display, instr_text, (10, h - 40),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)

        # Draw status text
        status_text = f"Points: {len(calibration_points)}/{REQUIRED_POINTS}. Click to annotate."
        # cv2.putText(display, status_text, (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 0, 0), 2)
        # Status text (bottom-most)
        cv2.putText(display, status_text, (10, h - 10),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 0, 0), 2)

        cv2.imshow("Calibration", display)

        if cv2.waitKey(1) & 0xFF == ord('q'):
            cv2.destroyAllWindows()
            return None

    cv2.destroyAllWindows()
    print("Calibration complete.")

    # Save points to file
    with open(POINTS_FILE, "w") as f:
        f.write(str(calibration_points))
    print(f"Saved {REQUIRED_POINTS} points to '{POINTS_FILE}'.")

    return calibration_points

if __name__ == "__main__":
    run_calibration()

    with open(POINTS_FILE, "r") as f:
        content = f.read()

    # Extract tuples like (1161, 1024)
    import re
    pairs = re.findall(r"\((\d+),\s*(\d+)\)", content)
    with open(POINTS_FILE, "w") as f:
        for x, y in pairs:
            f.write(f"{x} {y}\n")
    print("Done!")