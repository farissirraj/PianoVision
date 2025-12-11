#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <cstdlib>   // strtod
#include <cctype>
#include <cstring>   // strerror

using namespace std;
using namespace cv;

extern const float THRESHOLD;
extern const char* POINTS_FILE;
extern const float MIN_VERTICAL_MOVEMENT;

// Segmentation constants
extern const int    DARK_KEY_THRESHOLD;
extern const double CONTOUR_EPSILON_FACTOR;
extern const int    REQUIRED_POINTS;

// Notes
extern const vector<string> WHITE_NOTES;
extern const vector<string> BLACK_NOTES;

// Screen / capture resolution
extern const int FRAME_WIDTH;
extern const int FRAME_HEIGHT;

// Highlight
extern const Scalar KEY_HIGHLIGHT_COLOR; // BGR (cyan-ish)
extern const double HIGHLIGHT_ALPHA;

// Networking
extern const char* SERVER_IP;
extern const int   SERVER_PORT;
extern int clientSocket;   // global socket

// Global for white key boundaries
extern vector<pair<Point2f, Point2f>> WHITE_KEY_BOUNDARIES;

// Global key centroids for labels
extern map<string, Point> GLOBAL_KEY_CENTROIDS;