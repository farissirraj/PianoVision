#include "config.h"

#include <opencv2/opencv.hpp>
#include <map>
#include <vector>
#include <string>

using namespace cv;

bool open_camera(VideoCapture& cap);
Mat dist_sq(const Mat& c1, const Mat& c2);
Mat get_roi_mask(const Size& size, const vector<Point2f>& points);
Mat segment_keys_by_color(const Mat& frame, const Mat& roi_mask);

void initialize_key_mapping(
    const Mat& initial_frame,
    const vector<Point2f>& points_list,
    map<string, vector<vector<Point>>>& KEY_MAPPING_POLYGONS,
    Mat& roi_mask_out
);
map<string, Mat> build_key_masks(
    const map<string, vector<vector<Point>>>& KEY_MAPPING_POLYGONS,
    int frame_width,
    int frame_height
);
void draw_note_labels(
    Mat& frame,
    const map<string, vector<vector<Point>>>&,
    const map<string, Point>& key_centroids
);
