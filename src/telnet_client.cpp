#include "telnet_client.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <iostream>
#include <sstream>

TelnetClient::TelnetClient(const std::string& ip, int port)
    : ip_(ip), port_(port), socket_fd_(-1), connected_(false) {}

TelnetClient::~TelnetClient() {
    disconnect();
}

bool TelnetClient::socket_connect() {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        last_error_ = "Failed to create socket: " + std::string(strerror(errno));
        return false;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);

    if (inet_pton(AF_INET, ip_.c_str(), &server_addr.sin_addr) <= 0) {
        last_error_ = "Invalid IP address: " + ip_;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        last_error_ = "Failed to connect to " + ip_ + ":" + std::to_string(port_) + 
                     ": " + std::string(strerror(errno));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // Set non-blocking mode with timeout
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);

    connected_ = true;
    return true;
}

bool TelnetClient::socket_disconnect() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    connected_ = false;
    return true;
}

bool TelnetClient::disconnect() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    return socket_disconnect();
}

bool TelnetClient::is_connected() const {
    return connected_ && socket_fd_ >= 0;
}

bool TelnetClient::send_data_unlocked(const std::string& data) {
    if (!connected_ || socket_fd_ < 0) {
        last_error_ = "Socket not connected";
        return false;
    }

    ssize_t sent = 0;
    size_t total = data.length();
    const char* buf = data.c_str();

    while (sent < (ssize_t)total) {
        ssize_t ret = ::send(socket_fd_, buf + sent, total - sent, 0);
        if (ret < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                usleep(10000);  // 10ms
                continue;
            }
            last_error_ = "Send failed: " + std::string(strerror(errno));
            connected_ = false;
            return false;
        }
        sent += ret;
    }

    return true;
}

std::string TelnetClient::receive_data_unlocked(int timeout_ms) {
    return read_until_timeout_unlocked(timeout_ms);
}

std::string TelnetClient::read_until_timeout_unlocked(int timeout_ms) {
    if (!connected_ || socket_fd_ < 0) {
        return "";
    }

    std::string result;
    char buffer[4096];
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();

        if (elapsed >= timeout_ms) {
            break;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socket_fd_, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = (timeout_ms - elapsed) * 1000;
        if (tv.tv_usec > 1000000) tv.tv_usec = 1000000;

        int ret = select(socket_fd_ + 1, &readfds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno != EINTR) {
                connected_ = false;
            }
            break;
        }

        if (ret == 0) {
            break;  // Timeout
        }

        ssize_t n = ::recv(socket_fd_, buffer, sizeof(buffer), 0);
        if (n < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                connected_ = false;
            }
            break;
        }

        if (n == 0) {
            connected_ = false;
            break;
        }

        result.append(buffer, n);
    }

    return result;
}

bool TelnetClient::matches_pattern(const std::string& text, const std::string& pattern) {
    try {
        std::regex regex_pattern(pattern);
        return std::regex_search(text, regex_pattern);
    } catch (const std::exception& e) {
        return text.find(pattern) != std::string::npos;
    }
}

bool TelnetClient::wait_for_pattern_unlocked(const std::string& pattern, int timeout_ms) {
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        if (matches_pattern(receive_buffer_, pattern)) {
            return true;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();

        if (elapsed >= timeout_ms) {
            return false;
        }

        int remaining = timeout_ms - elapsed;
        std::string data = receive_data_unlocked(std::min(remaining, 1000));
        if (data.empty() && elapsed >= timeout_ms) {
            break;
        }
        receive_buffer_ += data;
    }

    return false;
}

bool TelnetClient::authenticate(const ConnectParams& params) {
    current_params_ = params;

    // Wait for username prompt
    if (!wait_for_pattern_unlocked(params.username_prompt, params.timeout_ms)) {
        last_error_ = "Username prompt not received: " + params.username_prompt;
        return false;
    }

    // Send username
    if (!send_data_unlocked(params.username + "\n")) {
        last_error_ = "Failed to send username";
        return false;
    }

    // Clear buffer
    receive_buffer_.clear();

    // Wait for password prompt
    if (!wait_for_pattern_unlocked(params.password_prompt, params.timeout_ms)) {
        last_error_ = "Password prompt not received: " + params.password_prompt;
        return false;
    }

    // Send password
    if (!send_data_unlocked(params.password + "\n")) {
        last_error_ = "Failed to send password";
        return false;
    }

    // Clear buffer
    receive_buffer_.clear();

    // Check if enable password is needed
    if (!params.enable_password.empty()) {
        if (!wait_for_pattern_unlocked(params.enable_prompt, params.timeout_ms)) {
            last_error_ = "Enable prompt not received: " + params.enable_prompt;
            return false;
        }

        // Send enable password
        if (!send_data_unlocked(params.enable_password + "\n")) {
            last_error_ = "Failed to send enable password";
            return false;
        }

        // Clear buffer
        receive_buffer_.clear();
    }

    // Wait for command prompt
    if (!wait_for_pattern_unlocked(params.command_prompt, params.timeout_ms)) {
        last_error_ = "Command prompt not received: " + params.command_prompt;
        return false;
    }

    return true;
}

bool TelnetClient::connect(const ConnectParams& params) {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (connected_) {
        return true;
    }

    if (!socket_connect()) {
        return false;
    }

    // Wait for initial banner
    receive_buffer_ = read_until_timeout_unlocked(params.timeout_ms);

    if (!authenticate(params)) {
        socket_disconnect();
        return false;
    }

    connected_ = true;
    return true;
}

bool TelnetClient::reconnect(const ConnectParams& params) {
    disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return connect(params);
}

CommandResponse TelnetClient::execute_command(const std::string& command, const std::string& cmd_id) {
    CommandResponse response;
    response.command_id = cmd_id;
    response.success = false;
    response.error_code = 1;

    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (!connected_ || socket_fd_ < 0) {
        response.error_message = "Not connected to device";
        response.error_code = 1002;
        return response;
    }

    // Clear buffer before sending command
    receive_buffer_.clear();

    // Send command
    if (!send_data_unlocked(command + "\n")) {
        response.error_message = "Failed to send command";
        response.error_code = 1005;
        return response;
    }

    // Read response until we get the prompt back
    std::string output = read_until_timeout_unlocked(current_params_.timeout_ms);
    receive_buffer_ = output;

    // Extract command output (remove echo and prompt)
    size_t cmd_pos = output.find(command);
    if (cmd_pos != std::string::npos) {
        output = output.substr(cmd_pos + command.length());
    }

    // Remove trailing prompt
    size_t prompt_pos = output.rfind(current_params_.command_prompt);
    if (prompt_pos != std::string::npos) {
        output = output.substr(0, prompt_pos);
    }

    // Trim whitespace
    while (!output.empty() && (output.front() == '\n' || output.front() == '\r')) {
        output.erase(0, 1);
    }
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    response.output = output;
    response.success = true;
    response.error_code = 0;
    return response;
}

std::vector<CommandResponse> TelnetClient::execute_commands(const std::vector<std::string>& commands) {
    std::vector<CommandResponse> responses;
    for (size_t i = 0; i < commands.size(); ++i) {
        responses.push_back(execute_command(commands[i], std::to_string(i)));
    }
    return responses;
}
