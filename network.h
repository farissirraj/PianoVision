#include "config.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>

using namespace std;

bool initializeConnection(const char* serverIP, int port);
bool sendString(const std::string& message);
void closeConnection();
