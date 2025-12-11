#include "cv.h"

bool open_camera(VideoCapture& cap) {
    string pipeline =
        "v4l2src device=/dev/video0 ! "
        "image/jpeg, width=480, height=272, framerate=30/1 ! "
        "jpegdec ! videoconvert ! appsink";

    cap.open(pipeline, CAP_GSTREAMER);
    if (cap.isOpened()) {
        cout << "Opened camera with GStreamer MJPG pipeline.\n";
        return true;
    }

    cerr << "Could not open camera with GStreamer MJPG pipeline. Falling back to V4L2.\n";

    cap.open("/dev/video0", CAP_V4L2);
    if (!cap.isOpened()) {
        cerr << "Failed to open /dev/video0 with CAP_V4L2.\n";
        return false;
    }

    cap.set(CAP_PROP_FOURCC, VideoWriter::fourcc('M','J','P','G'));
    cap.set(CAP_PROP_FRAME_WIDTH,  FRAME_WIDTH);
    cap.set(CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT);

    cout << "Opened camera with V4L2 (/dev/video0).\n";
    return true;
}

Mat dist_sq(const Mat& c1, const Mat& c2) {
    Mat diff;
    subtract(c1, c2, diff);

    Mat sq;
    multiply(diff, diff, sq);

    vector<Mat> ch;
    split(sq, ch);
    Mat sumSq = ch[0] + ch[1] + ch[2];
    return sumSq;
}

Mat get_roi_mask(const Size& size, const vector<Point2f>& points) {
    Mat mask = Mat::zeros(size, CV_8UC1);
    if (points.size() < 4) return mask;

    vector<Point> pts;
    for (int i = 0; i < 4; ++i) {
        pts.emplace_back(cvRound(points[i].x), cvRound(points[i].y));
    }
    vector<vector<Point>> polys{ pts };
    fillPoly(mask, polys, Scalar(255));

    return mask;
}

Mat segment_keys_by_color(const Mat& frame, const Mat& roi_mask) {
    Mat hsv;
    cvtColor(frame, hsv, COLOR_BGR2HSV);

    vector<Mat> ch;
    split(hsv, ch);
    Mat v_channel = ch[2];

    Mat dark_mask;
    threshold(v_channel, dark_mask, DARK_KEY_THRESHOLD, 255, THRESH_BINARY_INV);

    Mat segmented;
    bitwise_and(dark_mask, roi_mask, segmented);
    return segmented;
}

void initialize_key_mapping(
    const Mat& initial_frame,
    const vector<Point2f>& points_list,
    map<string, vector<vector<Point>>>& KEY_MAPPING_POLYGONS,
    Mat& roi_mask_out
) {
    KEY_MAPPING_POLYGONS.clear();
    GLOBAL_KEY_CENTROIDS.clear();

    int frame_height = initial_frame.rows;
    int frame_width  = initial_frame.cols;

    roi_mask_out = get_roi_mask(Size(frame_width, frame_height), points_list);

    Mat dark_key_mask = segment_keys_by_color(initial_frame, roi_mask_out);

    vector<vector<Point>> contours;
    findContours(dark_key_mask, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

    vector<pair<int,int>> centroid_idx;
    for (int i = 0; i < (int)contours.size(); ++i) {
        Moments M = moments(contours[i]);
        if (M.m00 == 0.0) continue;
        int cx = static_cast<int>(M.m10 / M.m00);
        centroid_idx.push_back({ cx, i });
    }

    sort(centroid_idx.begin(), centroid_idx.end(),
         [](const pair<int,int>& a, const pair<int,int>& b) {
             return a.first < b.first;
         });

    vector<vector<Point>> sorted_contours;
    for (size_t i = 0; i < centroid_idx.size() && i < 5; ++i) {
        sorted_contours.push_back(contours[centroid_idx[i].second]);
    }

    vector<vector<Point>> BLACK_KEY_CONTOURS;
    vector<Point> TOP_EDGE_POINTS;
    int MIN_BLACK_KEY_Y_BOT = frame_height;

    for (auto& contour : sorted_contours) {
        double peri = arcLength(contour, true);
        vector<Point> approx;
        approxPolyDP(contour, approx, CONTOUR_EPSILON_FACTOR * peri, true);

        BLACK_KEY_CONTOURS.push_back(approx);

        for (const Point& p : approx) {
            if (p.y > 0) {
                MIN_BLACK_KEY_Y_BOT = min(MIN_BLACK_KEY_Y_BOT, p.y);
            }
        }

        vector<Point> key_points = approx;
        sort(key_points.begin(), key_points.end(),
             [](const Point& a, const Point& b){ return a.y < b.y; });

        vector<Point> top_corners;
        if (key_points.size() >= 2) {
            top_corners.push_back(key_points[0]);
            top_corners.push_back(key_points[1]);
        } else if (!key_points.empty()) {
            top_corners.push_back(key_points[0]);
        }

        sort(top_corners.begin(), top_corners.end(),
             [](const Point& a, const Point& b){ return a.x < b.x; });

        TOP_EDGE_POINTS.insert(TOP_EDGE_POINTS.end(), top_corners.begin(), top_corners.end());
    }

    sort(TOP_EDGE_POINTS.begin(), TOP_EDGE_POINTS.end(),
         [](const Point& a, const Point& b){ return a.x < b.x; });

    int BLACK_KEY_SPLIT_Y = MIN_BLACK_KEY_Y_BOT;
    if (BLACK_KEY_SPLIT_Y == frame_height) {
        BLACK_KEY_SPLIT_Y = static_cast<int>(frame_height * 0.7);
    }

    map<string, Point> black_key_centroids;
    map<string, Point> white_key_centroids;

    for (int i = 0; i < (int)BLACK_KEY_CONTOURS.size() && i < (int)BLACK_NOTES.size(); ++i) {
        const string& label = BLACK_NOTES[i];
        vector<Point> contour_poly = BLACK_KEY_CONTOURS[i];

        KEY_MAPPING_POLYGONS[label].push_back(contour_poly);

        Moments M = moments(contour_poly);
        if (M.m00 != 0.0) {
            int cx = static_cast<int>(M.m10 / M.m00);
            int cy = static_cast<int>(M.m01 / M.m00);
            black_key_centroids[label] = Point(cx, cy);
        }
    }

    int avg_cyan_y = frame_height / 2;
    if (!TOP_EDGE_POINTS.empty()) {
        double sumY = 0.0;
        for (const Point& p : TOP_EDGE_POINTS) sumY += p.y;
        avg_cyan_y = static_cast<int>(sumY / TOP_EDGE_POINTS.size());
    }

    for (size_t i = 0; i < WHITE_NOTES.size(); ++i) {
        const string& label = WHITE_NOTES[i];
        if (i + 1 >= WHITE_KEY_BOUNDARIES.size()) break;

        Point2f wl_left_top  = WHITE_KEY_BOUNDARIES[i].first;
        Point2f wl_left_bot  = WHITE_KEY_BOUNDARIES[i].second;
        Point2f wl_right_top = WHITE_KEY_BOUNDARIES[i + 1].first;
        Point2f wl_right_bot = WHITE_KEY_BOUNDARIES[i + 1].second;

        vector<Point> front_pts;
        front_pts.emplace_back(cvRound(wl_left_bot.x),  BLACK_KEY_SPLIT_Y);
        front_pts.emplace_back(cvRound(wl_right_bot.x), BLACK_KEY_SPLIT_Y);
        front_pts.emplace_back(cvRound(wl_right_bot.x), cvRound(wl_right_bot.y));
        front_pts.emplace_back(cvRound(wl_left_bot.x),  cvRound(wl_left_bot.y));

        vector<Point> back_pts;
        back_pts.emplace_back(cvRound(wl_left_top.x),  avg_cyan_y);
        back_pts.emplace_back(cvRound(wl_right_top.x), avg_cyan_y);
        back_pts.emplace_back(cvRound(wl_right_top.x), BLACK_KEY_SPLIT_Y);
        back_pts.emplace_back(cvRound(wl_left_top.x),  BLACK_KEY_SPLIT_Y);

        KEY_MAPPING_POLYGONS[label].push_back(front_pts);
        KEY_MAPPING_POLYGONS[label].push_back(back_pts);

        double sumX = 0.0, sumY2 = 0.0;
        for (const Point& p : front_pts) {
            sumX  += p.x;
            sumY2 += p.y;
        }
        if (!front_pts.empty()) {
            int cx = static_cast<int>(sumX / front_pts.size());
            int cy = static_cast<int>(sumY2 / front_pts.size());
            white_key_centroids[label] = Point(cx, cy);
        }
    }

    GLOBAL_KEY_CENTROIDS.insert(black_key_centroids.begin(), black_key_centroids.end());
    GLOBAL_KEY_CENTROIDS.insert(white_key_centroids.begin(), white_key_centroids.end());
}

map<string, Mat> build_key_masks(
    const map<string, vector<vector<Point>>>& KEY_MAPPING_POLYGONS,
    int frame_width,
    int frame_height
) {
    map<string, Mat> KEY_MASKS;

    for (const auto& kv : KEY_MAPPING_POLYGONS) {
        const string& label = kv.first;
        const vector<vector<Point>>& polys = kv.second;

        Mat mask = Mat::zeros(frame_height, frame_width, CV_8UC1);
        if (!polys.empty()) {
            fillPoly(mask, polys, Scalar(255));
        }
        KEY_MASKS[label] = mask;
    }

    return KEY_MASKS;
}

void draw_note_labels(
    Mat& frame,
    const map<string, vector<vector<Point>>>&,
    const map<string, Point>& key_centroids
) {
    for (const auto& kv : key_centroids) {
        const string& label = kv.first;
        Point c = kv.second;

        string display_label;
        for (char ch : label) {
            if (ch != '4' && ch != '5')
                display_label.push_back(ch);
        }

        Scalar color;
        double font_scale;
        int thickness;
        Point text_pos;

        if (label.find('#') != string::npos) { // black key
            color = Scalar(255, 255, 255);  // white text
            font_scale = 0.6;
            thickness  = 1;
            text_pos   = Point(c.x - 15, c.y - 20);
        } else { // white key
            color = Scalar(0, 0, 0);        // black text
            font_scale = 0.7;
            thickness  = 2;
            text_pos   = Point(c.x - 15, c.y);
        }

        putText(frame, display_label, text_pos,
                FONT_HERSHEY_SIMPLEX, font_scale, color, thickness);
    }
}

