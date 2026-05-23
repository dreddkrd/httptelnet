#include "http_server.hpp"
#include "device_manager.hpp"
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>

HTTPServer* g_server = nullptr;

void signal_handler(int signal) {
    std::cout << "\nShutdown signal received (" << signal << ")" << std::endl;
    if (g_server) {
        g_server->stop();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    int port = 8080;

    // Parse command line arguments
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "Invalid port number: " << argv[1] << std::endl;
            return 1;
        }
    }

    std::cout << "Starting HTTP Telnet Middleware..." << std::endl;
    std::cout << "Port: " << port << std::endl;

    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Start HTTP server
    HTTPServer server(port);
    g_server = &server;

    if (!server.start()) {
        std::cerr << "Failed to start HTTP server" << std::endl;
        return 1;
    }

    // Start background cleanup thread
    std::thread cleanup_thread([]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            DeviceManager::instance().cleanup_idle_connections(600000);  // 10 minutes
        }
    });
    cleanup_thread.detach();

    std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;

    // Keep the main thread alive
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
