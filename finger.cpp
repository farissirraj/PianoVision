#include <opencv2/opencv.hpp>
#include <iostream>

using namespace cv;
using namespace std;

// Find fingertip point from a contour: highest point (min y)
Point findFingertip(const vector<Point> &contour) {
    Point fingertip = contour[0];
    for (const auto &p : contour) {
        if (p.y < fingertip.y) {  // smaller y == higher in image
            fingertip = p;
        }
    }
    return fingertip;
}

int main() {
    // Open webcam at /dev/video0 explicitly
    VideoCapture cap("/dev/video0", cv::CAP_V4L2);
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    if (!cap.isOpened()) {
        cerr << "Error: Cannot open /dev/video0" << endl;
        return -1;
    }

    // Keep it small for speed
    cap.set(CAP_PROP_FRAME_WIDTH, 320);
    cap.set(CAP_PROP_FRAME_HEIGHT, 240);

    // HSV range tuned for BLUE wrapper
    int h_min = 90;   // typical blue band
    int h_max = 130;
    int s_min = 80;
    int s_max = 255;
    int v_min = 50;
    int v_max = 255;

    namedWindow("Frame", WINDOW_AUTOSIZE);
    namedWindow("Mask", WINDOW_AUTOSIZE);

    // Trackbars to tune BLUE HSV band
    createTrackbar("H min", "Mask", &h_min, 179);
    createTrackbar("H max", "Mask", &h_max, 179);
    createTrackbar("S min", "Mask", &s_min, 255);
    createTrackbar("S max", "Mask", &s_max, 255);
    createTrackbar("V min", "Mask", &v_min, 255);
    createTrackbar("V max", "Mask", &v_max, 255);

    Mat frame, frameSmall, hsv, mask, morph;

    while (true) {
        if (!cap.read(frame)) {
            cerr << "Error: Cannot read frame" << endl;
            break;
        }

        // Optional: ensure small size
        resize(frame, frameSmall, Size(320, 240));

        // Convert to HSV
        cvtColor(frameSmall, hsv, COLOR_BGR2HSV);

        // Get HSV ranges from trackbars (for blue)
        Scalar lowerHSV(h_min, s_min, v_min);
        Scalar upperHSV(h_max, s_max, v_max);

        // Threshold for blue wrappers
        inRange(hsv, lowerHSV, upperHSV, mask);

        // Morphological operations to reduce noise
        Mat kernel = getStructuringElement(MORPH_ELLIPSE, Size(3, 3));
        erode(mask, morph, kernel, Point(-1, -1), 1);
        dilate(morph, morph, kernel, Point(-1, -1), 2);

        // Find contours in the blue mask
        vector<vector<Point>> contours;
        vector<Vec4i> hierarchy;
        findContours(morph, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

        // Optional: clear stdout each frame
        // system("clear"); // or std::cout << "\033[2J\033[H";

        int fingerIndex = 0;

        for (size_t i = 0; i < contours.size(); ++i) {
            double area = contourArea(contours[i]);
            if (area < 150.0) {
                // too small, likely noise
                continue;
            }

            const auto &contour = contours[i];

            // Optional smoothing
            vector<Point> approx;
            approxPolyDP(contour, approx, 2.0, true);

            if (approx.empty()) continue;

            // Fingertip = highest point
            Point fingertip = findFingertip(approx);

            // Draw contour and fingertip
            drawContours(frameSmall, vector<vector<Point>>{approx}, -1,
                         Scalar(0, 255, 0), 1);
            circle(frameSmall, fingertip, 5, Scalar(0, 0, 255), -1);

            // Label with index and coordinates
            string label = "F" + to_string(fingerIndex) + ":(" +
                           to_string(fingertip.x) + "," +
                           to_string(fingertip.y) + ")";
            putText(frameSmall, label,
                    fingertip + Point(5, -5),
                    FONT_HERSHEY_SIMPLEX, 0.4,
                    Scalar(255, 255, 255), 1);

            // Also print to stdout if you want to consume this elsewhere
            cout << "Finger " << fingerIndex
                 << " -> x: " << fingertip.x
                 << ", y: " << fingertip.y << endl;

            fingerIndex++;
        }

        imshow("Frame", frameSmall);
        imshow("Mask", morph);

        char key = (char)waitKey(1);
        if (key == 27 || key == 'q') { // ESC or q
            break;
        }
    }

    cap.release();
    destroyAllWindows();
    return 0;
}
