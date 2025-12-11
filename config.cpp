#include "config.h"

const float THRESHOLD = 100.0f;
const char* POINTS_FILE = "calibration_points.txt";
const float MIN_VERTICAL_MOVEMENT = 5.0f;

// Segmentation constants
const int    DARK_KEY_THRESHOLD      = 80;
const double CONTOUR_EPSILON_FACTOR  = 0.05;
const int    REQUIRED_POINTS         = 6;

// Notes
const vector<string> WHITE_NOTES = { "C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5" };
const vector<string> BLACK_NOTES = { "C#4", "D#4", "F#4", "G#4", "A#4" };

// Screen / capture resolution
const int FRAME_WIDTH  = 480;
const int FRAME_HEIGHT = 272;

// Highlight
const Scalar KEY_HIGHLIGHT_COLOR = Scalar(255, 255, 0); // BGR (cyan-ish)
const double HIGHLIGHT_ALPHA     = 0.4;

// Networking
const char* SERVER_IP   = "192.168.137.1";
const int   SERVER_PORT = 5050;
int clientSocket        = -1;   // global socket

// Global for white key boundaries
vector<pair<Point2f, Point2f>> WHITE_KEY_BOUNDARIES;

// Global key centroids for labels
map<string, Point> GLOBAL_KEY_CENTROIDS;