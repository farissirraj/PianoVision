#include "network.h"

bool initializeConnection(const char* serverIP, int port) {
    if (clientSocket >= 0) {
        return true;
    }

    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0) {
        std::cerr << "[NET] Error creating socket: " << strerror(errno) << "\n";
        return false;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(port);
    if (inet_pton(AF_INET, serverIP, &serverAddr.sin_addr) <= 0) {
        std::cerr << "[NET] Invalid address: " << serverIP << "\n";
        close(clientSocket);
        clientSocket = -1;
        return false;
    }

    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[NET] Connection failed to " << serverIP << ":" << port
                  << " - " << strerror(errno) << "\n";
        close(clientSocket);
        clientSocket = -1;
        return false;
    }

    std::cout << "[NET] Connected to " << serverIP << ":" << port << "\n";
    return true;
}


bool sendString(const std::string& message) {
    if (clientSocket < 0) {
        std::cerr << "[NET] Not connected, cannot send: " << message << "\n";
        return false;
    }

    ssize_t bytesSent = send(clientSocket, message.c_str(), message.size(), 0);
    if (bytesSent < 0) {
        std::cerr << "[NET] Error sending data: " << strerror(errno) << "\n";
        return false;
    }

    std::cout << "[NET] Sent packet (" << bytesSent << " bytes): " << message << "\n";
    return true;
}

void closeConnection() {
    if (clientSocket >= 0) {
        close(clientSocket);
        std::cout << "[NET] Connection closed.\n";
        clientSocket = -1;
    }
}