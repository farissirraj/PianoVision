#include "config.h"
#include "cv.h"
#include "network.h"

#include <iostream>
#include <map>
#include <vector>
#include <string>

#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

vector<Point2f> load_points_from_file() {
    vector<Point2f> pts;

    ifstream fin(POINTS_FILE);
    if (!fin.is_open()) {
        cerr << "Could not open " << POINTS_FILE << endl;
        exit(1);
    }

    string contents((istreambuf_iterator<char>(fin)), istreambuf_iterator<char>());
    fin.close();

    const char* p = contents.c_str();
    vector<double> nums;

    while (*p) {
        if (*p == '-' || isdigit(static_cast<unsigned char>(*p))) {
            char* endptr = nullptr;
            double val = strtod(p, &endptr);
            nums.push_back(val);
            p = endptr;
        } else {
            ++p;
        }
    }

    if (nums.size() < REQUIRED_POINTS * 2) {
        cerr << "Not enough numbers in " << POINTS_FILE
             << ". Need " << REQUIRED_POINTS * 2 << ", found " << nums.size() << endl;
        exit(1);
    }

    for (int i = 0; i < REQUIRED_POINTS; ++i) {
        double x = nums[2 * i];
        double y = nums[2 * i + 1];
        pts.emplace_back(static_cast<float>(x), static_cast<float>(y));
    }

    cout << "Loaded " << pts.size() << " calibration points from "
         << POINTS_FILE << endl;

    return pts;
}

string mapToJson(const std::map<std::string, std::string>& data) {
    std::stringstream json;
    json << "{";
    bool first = true;
    for (const auto& kv : data) {
        if (!first) json << ",";
        json << "\"" << kv.first << "\":\"" << kv.second << "\"";
        first = false;
    }
    json << "}";
    return json.str();
}

bool calculate_all_key_positions(const vector<Point2f>& points) {
    WHITE_KEY_BOUNDARIES.clear();

    if (points.size() < 4) {
        cerr << "Error: Need at least 4 ROI points for line extrapolation.\n";
        return false;
    }

    Point2f roi_tl = points[0];
    Point2f roi_tr = points[1];
    Point2f roi_br = points[2];
    Point2f roi_bl = points[3];

    int num_lines = 9;
    for (int i = 0; i < num_lines; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(num_lines - 1);
        float top_x = (1.0f - t) * roi_tl.x + t * roi_tr.x;
        float top_y = (1.0f - t) * roi_tl.y + t * roi_tr.y;
        float bot_x = (1.0f - t) * roi_bl.x + t * roi_br.x;
        float bot_y = (1.0f - t) * roi_bl.y + t * roi_br.y;
        WHITE_KEY_BOUNDARIES.push_back({ Point2f(top_x, top_y), Point2f(bot_x, bot_y) });
    }

    return true;
}

void writeToFramebuffer(const Mat &frame, const char* fbdev)
{
    static int fbfd = -1;
    static char *fbp = nullptr;
    static long screensize = 0;

    if (fbfd < 0) {
        fbfd = open(fbdev, O_RDWR);
        if (fbfd < 0) {
            cerr << "Error opening framebuffer device " << fbdev << endl;
            return;
        }

        struct fb_var_screeninfo vinfo;
        struct fb_fix_screeninfo finfo;

        if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
            cerr << "Error reading fixed screen info" << endl;
            close(fbfd);
            fbfd = -1;
            return;
        }
        if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
            cerr << "Error reading variable screen info" << endl;
            close(fbfd);
            fbfd = -1;
            return;
        }

        screensize = vinfo.yres_virtual * finfo.line_length;
        fbp = (char*)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
        if (fbp == MAP_FAILED) {
            cerr << "Error mapping framebuffer" << endl;
            close(fbfd);
            fbfd = -1;
            fbp = nullptr;
            return;
        }
    }

    if (!fbp) return;

    Mat frameResized;
    if (frame.cols != FRAME_WIDTH || frame.rows != FRAME_HEIGHT) {
        resize(frame, frameResized, Size(FRAME_WIDTH, FRAME_HEIGHT));
    } else {
        frameResized = frame;
    }

    Mat frameRGB16;
    cvtColor(frameResized, frameRGB16, COLOR_BGR2BGR565);

    long bytesToCopy = std::min(screensize,
                                (long)(frameRGB16.total() * frameRGB16.elemSize()));
    memcpy(fbp, frameRGB16.data, bytesToCopy);
}


void start_live_tracking(
    const map<string, Mat>& KEY_MASKS,
    const map<string, vector<vector<Point>>>& KEY_MAPPING_POLYGONS,
    const Mat& roi_mask,
    const Mat& initial_frame_bgr
) {
    VideoCapture cap;
    if (!open_camera(cap)) {
        return;
    }

    Mat prev_rgb;
    cvtColor(initial_frame_bgr, prev_rgb, COLOR_BGR2RGB);
    prev_rgb.convertTo(prev_rgb, CV_32FC3);

    int frame_height = initial_frame_bgr.rows;
    int frame_width  = initial_frame_bgr.cols;

    map<string, double> previous_motion_centroid;

    cout << "\n--- LIVE TRACKING STARTED (framebuffer only). Press Ctrl+C to stop ---\n";

    while (true) {
        Mat frame;
        if (!cap.read(frame)) {
            cerr << "Failed to read frame from camera.\n";
            break;
        }

        // Flip across Y axis (mirror) like in Python version
        flip(frame, frame, 1);

        if (frame.cols != FRAME_WIDTH || frame.rows != FRAME_HEIGHT) {
            resize(frame, frame, Size(FRAME_WIDTH, FRAME_HEIGHT));
        }

        Mat frame_rgb;
        cvtColor(frame, frame_rgb, COLOR_BGR2RGB);
        Mat curr32;
        frame_rgb.convertTo(curr32, CV_32FC3);

        // Motion detection
        Mat diffSq = dist_sq(curr32, prev_rgb);

        Mat motion_mask;
        compare(diffSq, THRESHOLD * THRESHOLD, motion_mask, CMP_GT); // 0/255, CV_8U

        Mat motion_mask_roi;
        bitwise_and(motion_mask, roi_mask, motion_mask_roi);

        Mat motion_mask_u8;
        motion_mask_roi.convertTo(motion_mask_u8, CV_8UC1);

        vector<Point> motion_coords;
        findNonZero(motion_mask_u8, motion_coords);

        vector<string> highlighted_keys;
        map<string, double> current_motion_centroid;

        if (motion_coords.size() > 50) {
            // For each key, check overlap
            for (const auto& kv : KEY_MASKS) {
                const string& label  = kv.first;
                const Mat&    k_mask = kv.second;

                Mat overlap;
                bitwise_and(motion_mask_u8, k_mask, overlap);
                int overlapCount = countNonZero(overlap);

                if (overlapCount > 50) {
                    vector<Point> overlap_coords;
                    findNonZero(overlap, overlap_coords);
                    if (!overlap_coords.empty()) {
                        double sumY = 0.0;
                        for (const Point& p : overlap_coords) sumY += p.y;
                        double key_motion_y = sumY / overlap_coords.size();
                        current_motion_centroid[label] = key_motion_y;

                        auto itPrev = previous_motion_centroid.find(label);
                        if (itPrev != previous_motion_centroid.end()) {
                            double prev_y = itPrev->second;
                            if (key_motion_y > prev_y + MIN_VERTICAL_MOVEMENT) {
                                highlighted_keys.push_back(label);
                            }
                        }
                    }
                }
            }
        }

        // Visualize: start from camera frame
        Mat display = frame.clone();

        // 1) Static key drawing (outlines & fills)
        for (const auto& kv : KEY_MAPPING_POLYGONS) {
            const string& label = kv.first;
            const vector<vector<Point>>& polys = kv.second;

            if (label.find('#') != string::npos) {
                // black keys
                for (const auto& poly : polys) {
                    vector<vector<Point>> pvec{ poly };
                    fillPoly(display, pvec, Scalar(0, 0, 0));                // fill black
                    polylines(display, pvec, true, Scalar(255, 255, 255), 1); // white outline
                }
            } else {
                // white keys: outlines only (light gray)
                for (const auto& poly : polys) {
                    vector<vector<Point>> pvec{ poly };
                    polylines(display, pvec, true, Scalar(200, 200, 200), 1);
                }
            }
        }

        // 2) Dynamic highlighting on pressed keys
        for (const string& label : highlighted_keys) {
            auto itMask = KEY_MASKS.find(label);
            auto itPoly = KEY_MAPPING_POLYGONS.find(label);
            if (itMask == KEY_MASKS.end() || itPoly == KEY_MAPPING_POLYGONS.end())
                continue;

            const Mat& key_mask = itMask->second;
            const vector<vector<Point>>& polys = itPoly->second;

            // solid color layer
            Mat highlight_layer_bgr(display.size(), display.type(), Scalar::all(0));
            for (const auto& poly : polys) {
                vector<vector<Point>> pvec{ poly };
                fillPoly(highlight_layer_bgr, pvec, KEY_HIGHLIGHT_COLOR);
            }

            // blended
            Mat blended_key_area;
            addWeighted(highlight_layer_bgr, HIGHLIGHT_ALPHA,
                        display, 1.0 - HIGHLIGHT_ALPHA, 0.0, blended_key_area);

            // combine with mask
            Mat key_mask_bgr, key_mask_inv_bgr;
            cvtColor(key_mask, key_mask_bgr, COLOR_GRAY2BGR);
            Mat key_mask_inv;
            bitwise_not(key_mask, key_mask_inv);
            cvtColor(key_mask_inv, key_mask_inv_bgr, COLOR_GRAY2BGR);

            Mat blended_foreground, background_no_key;
            bitwise_and(blended_key_area, key_mask_bgr, blended_foreground);
            bitwise_and(display,          key_mask_inv_bgr, background_no_key);

            display = background_no_key + blended_foreground;
        }

        // 3) Draw note labels
        draw_note_labels(display, KEY_MAPPING_POLYGONS, GLOBAL_KEY_CENTROIDS);

        // 4) Status text
        if (!highlighted_keys.empty()) {
            string status = "KEYS PLAYED: ";
            for (size_t i = 0; i < highlighted_keys.size(); ++i) {
                if (i > 0) status += ", ";
                status += highlighted_keys[i];
            }
            putText(display, status,
                    Point(10, frame_height - 10),
                    FONT_HERSHEY_SIMPLEX, 0.7,
                    Scalar(0, 255, 0), 2);
        } else {
            string status = "No Downward Press Detected";
            putText(display, status,
                    Point(10, frame_height - 10),
                    FONT_HERSHEY_SIMPLEX, 0.7,
                    Scalar(0, 0, 255), 2);
        }

        // 5) SEND PRESSED NOTES OVER TCP (white / black separated)
        if (!highlighted_keys.empty()) {
            std::map<std::string, std::string> packet;
            std::stringstream white_ss;
            std::stringstream black_ss;

            for (const std::string& note : highlighted_keys) {
                if (note.find('#') != std::string::npos) {
                    // black key
                    black_ss << note << " ";
                } else {
                    // white key
                    white_ss << note << " ";
                }
            }

            if (!white_ss.str().empty()) {
                packet["white"] = white_ss.str();
            }
            if (!black_ss.str().empty()) {
                packet["black"] = black_ss.str();
            }

            if (!packet.empty()) {
                std::string json = mapToJson(packet);
                std::cout << "[NET] Built packet: " << json << "\n";

                if (clientSocket >= 0) {
                    bool ok = sendString(json);
                    if (!ok) {
                        std::cerr << "[NET] sendString failed for packet: " << json << "\n";
                    }
                } else {
                    std::cerr << "[NET] Socket not connected, would have sent: " << json << "\n";
                }
            }
        }

        // 6) Write to framebuffer (480x272)
        writeToFramebuffer(display, "/dev/fb0");

        // Update previous frame + centroids
        prev_rgb = curr32.clone();
        previous_motion_centroid = current_motion_centroid;

        // No waitKey / imshow -> stop with Ctrl+C from shell
    }

    cap.release();
}

// ------------------------
// MAIN
// ------------------------
int main() {
    // 0. Set up TCP connection
    if (!initializeConnection(SERVER_IP, SERVER_PORT)) {
        std::cerr << "[NET] Warning: could not connect to server, running without network.\n";
    }

    // 1. Initial frame capture
    VideoCapture temp_cap;
    if (!open_camera(temp_cap)) {
        cerr << "FATAL: Could not open camera for initial frame.\n";
        closeConnection();
        return -1;
    }

    Mat initial_frame_bgr;
    if (!temp_cap.read(initial_frame_bgr)) {
        cerr << "FATAL: Could not capture initial frame.\n";
        temp_cap.release();
        closeConnection();
        return -1;
    }
    temp_cap.release();

    // Flip initial frame across Y axis
    flip(initial_frame_bgr, initial_frame_bgr, 1);

    if (initial_frame_bgr.cols != FRAME_WIDTH || initial_frame_bgr.rows != FRAME_HEIGHT) {
        resize(initial_frame_bgr, initial_frame_bgr, Size(FRAME_WIDTH, FRAME_HEIGHT));
    }

    // 2. Load calibration points
    vector<Point2f> points_list = load_points_from_file();

    // 3. Compute white key boundaries
    if (!calculate_all_key_positions(points_list)) {
        cerr << "Key position calculation failed.\n";
        closeConnection();
        return -1;
    }
    cout << "Key mapping geometry calculated.\n";

    // 4. Initialize key mapping (polygons + centroids) + ROI mask
    map<string, vector<vector<Point>>> KEY_MAPPING_POLYGONS;
    Mat roi_mask;
    initialize_key_mapping(initial_frame_bgr, points_list, KEY_MAPPING_POLYGONS, roi_mask);

    // 5. Build key masks
    map<string, Mat> KEY_MASKS = build_key_masks(
        KEY_MAPPING_POLYGONS,
        initial_frame_bgr.cols,
        initial_frame_bgr.rows
    );

    // 6. Start live tracking (framebuffer only + TCP send)
    start_live_tracking(KEY_MASKS, KEY_MAPPING_POLYGONS, roi_mask, initial_frame_bgr);

    // 7. Cleanup
    closeConnection();
    return 0;
}


