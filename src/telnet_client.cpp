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
    : ip_(ip), port_(port), socket_fd_(-1), connected_(false), keepalive_running_(false) {}

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

    // Set non-blocking mode
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
    stop_keepalive_thread();
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

bool TelnetClient::matches_pattern(const std::string& text, const std::string& pattern) const {
    try {
        std::regex regex_pattern(pattern);
        return std::regex_search(text, regex_pattern);
    } catch (const std::exception& e) {
        return text.find(pattern) != std::string::npos;
    }
}

std::string TelnetClient::extract_last_prompt_line(const std::string& text, const std::string& prompt_pattern) const {
    // Split by newlines and find last line containing prompt pattern
    std::istringstream iss(text);
    std::string line;
    std::string last_matching_line;

    while (std::getline(iss, line)) {
        if (matches_pattern(line, prompt_pattern)) {
            last_matching_line = line;
        }
    }

    return last_matching_line;
}

bool TelnetClient::wait_for_prompt_unlocked(const std::string& prompt_pattern, int timeout_ms, std::string& captured_prompt) {
    auto start_time = std::chrono::steady_clock::now();
    captured_prompt = "";

    while (true) {
        // Check if we have prompt in buffer
        std::string matching_line = extract_last_prompt_line(receive_buffer_, prompt_pattern);
        if (!matching_line.empty()) {
            captured_prompt = matching_line;
            return true;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();

        if (elapsed >= timeout_ms) {
            return false;
        }

        int remaining = timeout_ms - elapsed;
        std::string data = read_until_timeout_unlocked(std::min(remaining, 1000));
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
    std::string temp_prompt;
    if (!wait_for_prompt_unlocked(params.username_prompt, params.timeout_ms, temp_prompt)) {
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
    if (!wait_for_prompt_unlocked(params.password_prompt, params.timeout_ms, temp_prompt)) {
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
        if (!wait_for_prompt_unlocked(params.enable_prompt, params.timeout_ms, temp_prompt)) {
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

    // Wait for command prompt and capture it
    if (!wait_for_prompt_unlocked(params.command_prompt, params.timeout_ms, current_prompt_)) {
        last_error_ = "Command prompt not received: " + params.command_prompt;
        return false;
    }

    // Execute terminal nopage command if specified
    if (!params.terminal_nopage.empty()) {
        receive_buffer_.clear();
        if (!send_data_unlocked(params.terminal_nopage + "\n")) {
            last_error_ = "Failed to send terminal nopage command";
            return false;
        }

        if (!wait_for_prompt_unlocked(params.command_prompt, params.timeout_ms, current_prompt_)) {
            last_error_ = "Failed to receive prompt after terminal nopage command";
            return false;
        }
    }

    return true;
}

bool TelnetClient::reconnect_if_needed() {
    if (!connected_ || socket_fd_ < 0) {
        socket_disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (!socket_connect()) {
            return false;
        }

        receive_buffer_.clear();
        if (!authenticate(current_params_)) {
            socket_disconnect();
            return false;
        }

        connected_ = true;
        return true;
    }
    return true;
}

void TelnetClient::keepalive_thread_func() {
    while (keepalive_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(current_params_.keepalive_interval_ms));
        
        if (!keepalive_running_) break;

        // Try to send keepalive command
        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            if (connected_ && socket_fd_ >= 0) {
                send_data_unlocked(current_params_.keepalive_command);
            }
        }
    }
}

void TelnetClient::start_keepalive_thread() {
    if (keepalive_running_) return;
    
    keepalive_running_ = true;
    keepalive_thread_ = std::thread(&TelnetClient::keepalive_thread_func, this);
}

void TelnetClient::stop_keepalive_thread() {
    if (!keepalive_running_) return;
    
    keepalive_running_ = false;
    if (keepalive_thread_.joinable()) {
        keepalive_thread_.join();
    }
}

bool TelnetClient::connect(const ConnectParams& params) {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (connected_) {
        return true;
    }

    if (!socket_connect()) {
        return false;
    }

    // Don't wait for banner, go directly to authentication
    if (!authenticate(params)) {
        socket_disconnect();
        return false;
    }

    connected_ = true;

    // Start keepalive thread
    start_keepalive_thread();

    return true;
}

CommandResponse TelnetClient::execute_segment(const CommandSegment& segment, const std::string& cmd_id) {
    CommandResponse response;
    response.command_id = cmd_id;
    response.success = false;
    response.error_code = 1;

    // Reconnect if needed
    if (!reconnect_if_needed()) {
        response.error_message = "Failed to reconnect to device";
        response.error_code = 1002;
        return response;
    }

    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (!connected_ || socket_fd_ < 0) {
        response.error_message = "Not connected to device";
        response.error_code = 1002;
        return response;
    }

    std::string accumulated_output;

    // Execute path commands
    for (const auto& path_cmd : segment.path) {
        receive_buffer_.clear();

        if (!send_data_unlocked(path_cmd + "\n")) {
            response.error_message = "Failed to send path command: " + path_cmd;
            response.error_code = 1005;
            return response;
        }

        // Wait for prompt after path command
        std::string new_prompt;
        if (!wait_for_prompt_unlocked(current_params_.command_prompt, current_params_.timeout_ms, new_prompt)) {
            response.error_message = "Prompt not received after path command: " + path_cmd;
            response.error_code = 1005;
            return response;
        }

        // Update current prompt for nested level
        if (!new_prompt.empty()) {
            current_prompt_ = new_prompt;
        }

        accumulated_output += receive_buffer_;
    }

    // Execute actual commands
    for (const auto& cmd : segment.commands) {
        receive_buffer_.clear();

        if (!send_data_unlocked(cmd + "\n")) {
            response.error_message = "Failed to send command: " + cmd;
            response.error_code = 1005;
            return response;
        }

        // Wait for prompt after command
        std::string temp_prompt;
        if (!wait_for_prompt_unlocked(current_params_.command_prompt, current_params_.timeout_ms, temp_prompt)) {
            response.error_message = "Prompt not received after command: " + cmd;
            response.error_code = 1005;
            return response;
        }

        // Extract output (remove echo and final prompt line)
        std::string output = receive_buffer_;
        size_t cmd_pos = output.find(cmd);
        if (cmd_pos != std::string::npos) {
            output = output.substr(cmd_pos + cmd.length());
        }

        // Remove last line (which contains the prompt)
        size_t last_newline = output.rfind('\n');
        if (last_newline != std::string::npos) {
            output = output.substr(0, last_newline);
        }

        // Trim leading newlines
        while (!output.empty() && (output.front() == '\n' || output.front() == '\r')) {
            output.erase(0, 1);
        }

        accumulated_output += output + "\n";
    }

    // Execute exit commands to return to root
    for (size_t i = 0; i < segment.path.size(); ++i) {
        receive_buffer_.clear();

        if (!send_data_unlocked("exit\n")) {
            response.error_message = "Failed to send exit command";
            response.error_code = 1005;
            return response;
        }

        // Wait for prompt
        std::string temp_prompt;
        if (!wait_for_prompt_unlocked(current_params_.command_prompt, current_params_.timeout_ms, temp_prompt)) {
            response.error_message = "Prompt not received after exit command";
            response.error_code = 1005;
            return response;
        }

        if (!temp_prompt.empty()) {
            current_prompt_ = temp_prompt;
        }
    }

    // Trim trailing whitespace
    while (!accumulated_output.empty() && (accumulated_output.back() == '\n' || accumulated_output.back() == '\r')) {
        accumulated_output.pop_back();
    }

    response.output = accumulated_output;
    response.success = true;
    response.error_code = 0;
    return response;
}

std::vector<CommandResponse> TelnetClient::execute_segments(const std::vector<CommandSegment>& segments) {
    std::vector<CommandResponse> responses;
    for (size_t i = 0; i < segments.size(); ++i) {
        responses.push_back(execute_segment(segments[i], std::to_string(i)));
    }
    return responses;
}
