// #include <opencv2/opencv.hpp>
// #include <iostream>

// using namespace cv;
// using namespace std;

// // Find fingertip point from a contour: highest point (min y)
// Point findFingertip(const vector<Point> &contour) {
//     Point fingertip = contour[0];
//     for (const auto &p : contour) {
//         if (p.y < fingertip.y) {  // smaller y == higher in image
//             fingertip = p;
//         }
//     }
//     return fingertip;
// }

// // Draw a semi-transparent piano overlay at the bottom of the frame
// void drawPianoOverlay(Mat &frameSmall) {
//     int width  = frameSmall.cols;
//     int height = frameSmall.rows;

//     // Piano will occupy bottom part of the frame
//     int pianoHeight = height / 3; // bottom third
//     Rect roi(0, height - pianoHeight, width, pianoHeight);

//     // Clone ROI to draw piano on
//     Mat overlayROI = frameSmall(roi).clone();

//     // Number of white keys across the width
//     int numWhiteKeys = 14;
//     float whiteKeyWidthF = static_cast<float>(roi.width) / numWhiteKeys;

//     // Draw white keys
//     for (int i = 0; i < numWhiteKeys; ++i) {
//         int x = static_cast<int>(i * whiteKeyWidthF);
//         int w = static_cast<int>(whiteKeyWidthF + 0.5f);
//         Rect whiteKeyRect(x, 0, w, roi.height);
//         rectangle(overlayROI, whiteKeyRect, Scalar(255, 255, 255), FILLED);
//         // Optional key borders
//         rectangle(overlayROI, whiteKeyRect, Scalar(0, 0, 0), 1);
//     }

//     // Draw black keys (approx piano pattern over repeating group of 7 white keys)
//     // Black keys at indices (mod 7) = 0,1,3,4,5  -> C#,D#,F#,G#,A#
//     for (int i = 0; i < numWhiteKeys - 1; ++i) {
//         int posInOctave = i % 7;
//         bool hasBlack =
//             (posInOctave == 0 || posInOctave == 1 ||
//              posInOctave == 3 || posInOctave == 4 ||
//              posInOctave == 5);

//         if (!hasBlack) continue;

//         float keyCenter = (i + 0.5f) * whiteKeyWidthF;
//         int blackKeyWidth  = static_cast<int>(whiteKeyWidthF * 0.6f);
//         int blackKeyHeight = static_cast<int>(roi.height * 0.6f);

//         int bx = static_cast<int>(keyCenter - blackKeyWidth / 2);
//         int by = 0;

//         // Clamp to ROI
//         if (bx < 0) bx = 0;
//         if (bx + blackKeyWidth > roi.width) {
//             blackKeyWidth = roi.width - bx;
//         }

//         Rect blackKeyRect(bx, by, blackKeyWidth, blackKeyHeight);
//         rectangle(overlayROI, blackKeyRect, Scalar(0, 0, 0), FILLED);
//     }

//     // Blend overlayROI with original ROI (50% opacity)
//     addWeighted(frameSmall(roi), 0.5, overlayROI, 0.5, 0.0, frameSmall(roi));
// }

// int main() {
//     // Open webcam at /dev/video0 explicitly
//     VideoCapture cap("/dev/video0", cv::CAP_V4L2);
//     cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
//     if (!cap.isOpened()) {
//         cerr << "Error: Cannot open /dev/video0" << endl;
//         return -1;
//     }

//     // Keep it small for speed
//     cap.set(CAP_PROP_FRAME_WIDTH, 320);
//     cap.set(CAP_PROP_FRAME_HEIGHT, 240);

//     // HSV range tuned for BLUE wrapper
//     int h_min = 90;   // typical blue band
//     int h_max = 130;
//     int s_min = 80;
//     int s_max = 255;
//     int v_min = 50;
//     int v_max = 255;

//     namedWindow("Frame", WINDOW_AUTOSIZE);
//     namedWindow("Mask", WINDOW_AUTOSIZE);

//     // Trackbars to tune BLUE HSV band
//     createTrackbar("H min", "Mask", &h_min, 179);
//     createTrackbar("H max", "Mask", &h_max, 179);
//     createTrackbar("S min", "Mask", &s_min, 255);
//     createTrackbar("S max", "Mask", &s_max, 255);
//     createTrackbar("V min", "Mask", &v_min, 255);
//     createTrackbar("V max", "Mask", &v_max, 255);

//     Mat frame, frameSmall, hsv, mask, morph;

//     while (true) {
//         if (!cap.read(frame)) {
//             cerr << "Error: Cannot read frame" << endl;
//             break;
//         }

//         // Optional: ensure small size
//         resize(frame, frameSmall, Size(320, 240));

//         // Convert to HSV
//         cvtColor(frameSmall, hsv, COLOR_BGR2HSV);

//         // Get HSV ranges from trackbars (for blue)
//         Scalar lowerHSV(h_min, s_min, v_min);
//         Scalar upperHSV(h_max, s_max, v_max);

//         // Threshold for blue wrappers
//         inRange(hsv, lowerHSV, upperHSV, mask);

//         // Morphological operations to reduce noise
//         Mat kernel = getStructuringElement(MORPH_ELLIPSE, Size(3, 3));
//         erode(mask, morph, kernel, Point(-1, -1), 1);
//         dilate(morph, morph, kernel, Point(-1, -1), 2);

//         // Find contours in the blue mask
//         vector<vector<Point>> contours;
//         vector<Vec4i> hierarchy;
//         findContours(morph, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

//         int fingerIndex = 0;

//         for (size_t i = 0; i < contours.size(); ++i) {
//             double area = contourArea(contours[i]);
//             if (area < 150.0) {
//                 // too small, likely noise
//                 continue;
//             }

//             const auto &contour = contours[i];

//             // Optional smoothing
//             vector<Point> approx;
//             approxPolyDP(contour, approx, 2.0, true);

//             if (approx.empty()) continue;

//             // Fingertip = highest point
//             Point fingertip = findFingertip(approx);

//             // Draw contour and fingertip
//             drawContours(frameSmall, vector<vector<Point>>{approx}, -1,
//                          Scalar(0, 255, 0), 1);
//             circle(frameSmall, fingertip, 5, Scalar(0, 0, 255), -1);

//             // Label with index and coordinates
//             string label = "F" + to_string(fingerIndex) + ":(" +
//                            to_string(fingertip.x) + "," +
//                            to_string(fingertip.y) + ")";
//             putText(frameSmall, label,
//                     fingertip + Point(5, -5),
//                     FONT_HERSHEY_SIMPLEX, 0.4,
//                     Scalar(255, 255, 255), 1);

//             cout << "Finger " << fingerIndex
//                  << " -> x: " << fingertip.x
//                  << ", y: " << fingertip.y << endl;

//             fingerIndex++;
//         }

//         // 🔹 Draw semi-transparent piano overlay at bottom
//         drawPianoOverlay(frameSmall);

//         imshow("Frame", frameSmall);
//         imshow("Mask", morph);

//         char key = (char)waitKey(1);
//         if (key == 27 || key == 'q') { // ESC or q
//             break;
//         }
//     }

//     cap.release();
//     destroyAllWindows();
//     return 0;
// }


#include <opencv2/opencv.hpp>
#include <iostream>
#include <algorithm>

using namespace cv;
using namespace std;

const int NUM_WHITE_KEYS = 14; // consistent across main & overlay

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

// Draw a semi-transparent piano overlay at the bottom of the frame.
// activeKeys = indices of white keys currently "pressed".
void drawPianoOverlay(Mat &frameSmall, const vector<int> &activeKeys) {
    int width  = frameSmall.cols;
    int height = frameSmall.rows;

    // Piano will occupy bottom part of the frame
    int pianoHeight = height / 3; // bottom third
    Rect roi(0, height - pianoHeight, width, pianoHeight);

    // Clone ROI to draw piano on
    Mat overlayROI = frameSmall(roi).clone();

    float whiteKeyWidthF = static_cast<float>(roi.width) / NUM_WHITE_KEYS;

    // Draw white keys
    for (int i = 0; i < NUM_WHITE_KEYS; ++i) {
        int x = static_cast<int>(i * whiteKeyWidthF);
        int w = static_cast<int>(whiteKeyWidthF + 0.5f);
        Rect whiteKeyRect(x, 0, w, roi.height);

        bool isActive = std::find(activeKeys.begin(), activeKeys.end(), i) != activeKeys.end();

        // Slightly darker for active key (visually like lower opacity)
        Scalar fillColor = isActive ? Scalar(200, 200, 200)  // pressed
                                    : Scalar(255, 255, 255); // normal

        rectangle(overlayROI, whiteKeyRect, fillColor, FILLED);
        // Key borders
        rectangle(overlayROI, whiteKeyRect, Scalar(0, 0, 0), 1);
    }

    // Draw black keys (approx piano pattern over repeating group of 7 white keys)
    // Black keys at indices (mod 7) = 0,1,3,4,5  -> C#,D#,F#,G#,A#
    for (int i = 0; i < NUM_WHITE_KEYS - 1; ++i) {
        int posInOctave = i % 7;
        bool hasBlack =
            (posInOctave == 0 || posInOctave == 1 ||
             posInOctave == 3 || posInOctave == 4 ||
             posInOctave == 5);

        if (!hasBlack) continue;

        float whiteKeyWidthF_local = whiteKeyWidthF;
        float keyCenter = (i + 1.0f) * whiteKeyWidthF_local; // center between i and i+1

        int blackKeyWidth  = static_cast<int>(whiteKeyWidthF_local * 0.6f);
        int blackKeyHeight = static_cast<int>(roi.height * 0.6f);

        int bx = static_cast<int>(keyCenter - blackKeyWidth / 2);
        int by = 0;

        // Clamp to ROI
        if (bx < 0) bx = 0;
        if (bx + blackKeyWidth > roi.width) {
            blackKeyWidth = roi.width - bx;
        }

        Rect blackKeyRect(bx, by, blackKeyWidth, blackKeyHeight);
        rectangle(overlayROI, blackKeyRect, Scalar(0, 0, 0), FILLED);
    }

    // Blend overlayROI with original ROI (50% opacity)
    addWeighted(frameSmall(roi), 0.5, overlayROI, 0.5, 0.0, frameSmall(roi));
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

        int width  = frameSmall.cols;
        int height = frameSmall.rows;

        // Piano geometry (must match drawPianoOverlay)
        int pianoHeight = height / 3;
        int pianoTop    = height - pianoHeight;
        float whiteKeyWidthF = static_cast<float>(width) / NUM_WHITE_KEYS;

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

        int fingerIndex = 0;
        vector<int> activeKeys; // which white keys are being touched

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

            cout << "Finger " << fingerIndex
                 << " -> x: " << fingertip.x
                 << ", y: " << fingertip.y << endl;

            // If finger is within the piano area, map it to a key index
            if (fingertip.y >= pianoTop) {
                int keyIndex = static_cast<int>(fingertip.x / whiteKeyWidthF);
                keyIndex = std::max(0, std::min(NUM_WHITE_KEYS - 1, keyIndex));
                if (std::find(activeKeys.begin(), activeKeys.end(), keyIndex) == activeKeys.end()) {
                    activeKeys.push_back(keyIndex);
                }
            }

            fingerIndex++;
        }

        // 🔹 Draw semi-transparent piano overlay at bottom with active keys highlighted
        drawPianoOverlay(frameSmall, activeKeys);

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
