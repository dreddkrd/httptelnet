#pragma once

#include <string>
#include <memory>

class HTTPServer {
public:
    explicit HTTPServer(int port = 8080);
    ~HTTPServer();

    bool start();
    void stop();
    bool is_running() const { return running_; }

private:
    int port_;
    bool running_;
    void* server_;  // Opaque pointer to HTTP server implementation
};
