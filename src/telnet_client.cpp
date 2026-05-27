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
    : ip_(ip), port_(port), socket_fd_(-1), keepalive_running_(false),
      connection_status_(ConnectionStatus::DISCONNECTED), failed_reconnect_attempts_(0),
      reconnection_thread_running_(false),
      last_reconnect_attempt_(std::chrono::steady_clock::now()) {}

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

    return true;
}

bool TelnetClient::socket_disconnect() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    return true;
}

bool TelnetClient::disconnect() {
    stop_keepalive_thread();
    stop_reconnection_thread();
    
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        socket_disconnect();
        connection_status_ = ConnectionStatus::DISCONNECTED;
    }
    return true;
}

bool TelnetClient::is_connected() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return connection_status_ == ConnectionStatus::CONNECTED && socket_fd_ >= 0;
}

ConnectionStatus TelnetClient::get_status() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return connection_status_;
}

std::string TelnetClient::get_status_string() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    switch (connection_status_) {
        case ConnectionStatus::CONNECTED:
            return "Connected";
        case ConnectionStatus::DISCONNECTED:
            return "Disconnected";
        case ConnectionStatus::UNAVAILABLE:
            return "Device unavailable";
        case ConnectionStatus::RECONNECTING:
            return "Reconnecting";
        default:
            return "Unknown";
    }
}

bool TelnetClient::check_connection_unlocked() {
    if (socket_fd_ < 0) {
        return false;
    }

    // Try to peek at socket to detect connection state
    char buf[1];
    int ret = ::recv(socket_fd_, buf, 1, MSG_PEEK);
    if (ret < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            return false;
        }
    } else if (ret == 0) {
        // Connection closed by remote
        return false;
    }

    return true;
}

bool TelnetClient::send_data_unlocked(const std::string& data) {
    if (socket_fd_ < 0) {
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
            return false;
        }
        sent += ret;
    }

    return true;
}

std::string TelnetClient::read_until_timeout_unlocked(int timeout_ms) {
    if (socket_fd_ < 0) {
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
                return result;
            }
            continue;
        }

        if (ret == 0) {
            break;  // Timeout
        }

        ssize_t n = ::recv(socket_fd_, buffer, sizeof(buffer), 0);
        if (n < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                return result;
            }
            break;
        }

        if (n == 0) {
            // Connection closed
            return result;
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

std::string TelnetClient::receive_until_prompt_unlocked(const std::string& prompt_pattern, int timeout_ms, std::string& captured_prompt) {
    auto start_time = std::chrono::steady_clock::now();
    captured_prompt = "";

    while (true) {
        // Check if we have prompt in buffer
        std::string matching_line = extract_last_prompt_line(receive_buffer_, prompt_pattern);
        if (!matching_line.empty()) {
            captured_prompt = matching_line;
            return receive_buffer_;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();

        if (elapsed >= timeout_ms) {
            // Check if connection is still alive
            if (!check_connection_unlocked()) {
                return "";
            }
            break;
        }

        // Read with adaptive timeout: use smaller chunks to check for prompt frequently
        int remaining = timeout_ms - elapsed;
        int read_timeout = std::min(remaining, 100);  // 100ms chunks instead of 1000ms
        std::string data = read_until_timeout_unlocked(read_timeout);
        if (!data.empty()) {
            receive_buffer_ += data;
        } else {
            // Check if connection is still alive even on empty reads
            if (!check_connection_unlocked()) {
                return "";
            }
        }
    }

    return receive_buffer_;
}

bool TelnetClient::authenticate(const ConnectParams& params) {
    current_params_ = params;

    // Wait for username prompt
    std::string temp_prompt;
    std::string username_response = receive_until_prompt_unlocked(params.username_prompt, params.timeout_ms, temp_prompt);
    if (temp_prompt.empty()) {
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
    std::string password_response = receive_until_prompt_unlocked(params.password_prompt, params.timeout_ms, temp_prompt);
    if (temp_prompt.empty()) {
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
        std::string enable_response = receive_until_prompt_unlocked(params.enable_prompt, params.timeout_ms, temp_prompt);
        if (temp_prompt.empty()) {
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
    std::string cmd_response = receive_until_prompt_unlocked(params.command_prompt, params.timeout_ms, current_prompt_);
    if (current_prompt_.empty()) {
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

        std::string nopage_response = receive_until_prompt_unlocked(params.command_prompt, params.timeout_ms, current_prompt_);
        if (current_prompt_.empty()) {
            last_error_ = "Failed to receive prompt after terminal nopage command";
            return false;
        }
    }

    return true;
}

bool TelnetClient::try_reconnect_unlocked() {
    // Close existing socket
    socket_disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Try to connect
    if (!socket_connect()) {
        return false;
    }

    receive_buffer_.clear();
    if (!authenticate(current_params_)) {
        socket_disconnect();
        return false;
    }

    return true;
}

bool TelnetClient::wait_for_reconnection_unlocked(int max_wait_ms) {
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();

        if (elapsed >= max_wait_ms) {
            return false;
        }

        // Check current status (released lock)
        {
            std::lock_guard<std::mutex> status_lock(status_mutex_);
            if (connection_status_ == ConnectionStatus::CONNECTED) {
                return true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void TelnetClient::reconnection_thread_func() {
    int reconnect_interval_ms = 15000;  // 15 seconds between reconnection attempts

    while (reconnection_thread_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_interval_ms));

        if (!reconnection_thread_running_) break;

        std::lock_guard<std::mutex> lock(socket_mutex_);
        std::lock_guard<std::mutex> status_lock(status_mutex_);

        // Only try to reconnect if status is UNAVAILABLE
        if (connection_status_ != ConnectionStatus::UNAVAILABLE) {
            continue;
        }

        // Try to reconnect
        if (try_reconnect_unlocked()) {
            failed_reconnect_attempts_ = 0;
            connection_status_ = ConnectionStatus::CONNECTED;
            status_changed_.notify_all();
            start_keepalive_thread();
        }
    }
}

void TelnetClient::start_reconnection_thread() {
    if (reconnection_thread_running_) return;

    reconnection_thread_running_ = true;
    reconnection_thread_ = std::thread(&TelnetClient::reconnection_thread_func, this);
}

void TelnetClient::stop_reconnection_thread() {
    if (!reconnection_thread_running_) return;

    reconnection_thread_running_ = false;
    if (reconnection_thread_.joinable()) {
        reconnection_thread_.join();
    }
}

void TelnetClient::keepalive_thread_func() {
    while (keepalive_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(current_params_.keepalive_interval_ms));

        if (!keepalive_running_) break;

        std::lock_guard<std::mutex> lock(socket_mutex_);
        std::lock_guard<std::mutex> status_lock(status_mutex_);

        if (connection_status_ != ConnectionStatus::CONNECTED || socket_fd_ < 0) {
            continue;
        }

        if (!send_data_unlocked(current_params_.keepalive_command)) {
            // Connection lost, mark as unavailable
            socket_disconnect();
            connection_status_ = ConnectionStatus::DISCONNECTED;
            start_reconnection_thread();
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
    std::lock_guard<std::mutex> status_lock(status_mutex_);

    if (connection_status_ == ConnectionStatus::CONNECTED) {
        return true;
    }

    if (!try_reconnect_unlocked()) {
        connection_status_ = ConnectionStatus::DISCONNECTED;
        return false;
    }

    failed_reconnect_attempts_ = 0;
    connection_status_ = ConnectionStatus::CONNECTED;
    start_keepalive_thread();

    return true;
}

CommandResponse TelnetClient::execute_segment(const CommandSegment& segment, const std::string& cmd_id) {
    CommandResponse response;
    response.command_id = cmd_id;
    response.success = false;
    response.error_code = 1;

    // First, try to ensure we have a valid connection
    {
        std::unique_lock<std::mutex> status_lock(status_mutex_);

        if (connection_status_ == ConnectionStatus::DISCONNECTED) {
            // Attempt immediate reconnection
            status_lock.unlock();
            
            std::lock_guard<std::mutex> socket_lock(socket_mutex_);
            std::lock_guard<std::mutex> status_lock_inner(status_mutex_);

            for (int attempt = 0; attempt < 3; ++attempt) {
                if (try_reconnect_unlocked()) {
                    failed_reconnect_attempts_ = 0;
                    connection_status_ = ConnectionStatus::CONNECTED;
                    start_keepalive_thread();
                    break;
                }
                failed_reconnect_attempts_++;

                if (attempt < 2) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            }

            if (connection_status_ != ConnectionStatus::CONNECTED) {
                connection_status_ = ConnectionStatus::UNAVAILABLE;
                start_reconnection_thread();
            }
        } else if (connection_status_ == ConnectionStatus::UNAVAILABLE) {
            // Wait for background reconnection thread to succeed
            if (status_changed_.wait_for(status_lock, std::chrono::seconds(5),
                [this] { return connection_status_ == ConnectionStatus::CONNECTED; }) == false) {
                response.error_message = "Device unavailable";
                response.error_code = 1009;
                return response;
            }
        }
    }

    // Now execute the segment with the valid connection
    std::lock_guard<std::mutex> lock(socket_mutex_);

    {
        std::lock_guard<std::mutex> status_lock(status_mutex_);
        if (connection_status_ != ConnectionStatus::CONNECTED || socket_fd_ < 0) {
            response.error_message = "Device not connected";
            response.error_code = 1002;
            return response;
        }
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
        receive_until_prompt_unlocked(current_params_.command_prompt, current_params_.timeout_ms, new_prompt);
        if (new_prompt.empty()) {
            response.error_message = "Prompt not received after path command: " + path_cmd;
            response.error_code = 1005;
            return response;
        }

        // Update current prompt for nested level
        if (!new_prompt.empty()) {
            current_prompt_ = new_prompt;
        }

        accumulated_output += receive_buffer_;
        receive_buffer_.clear();
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
        receive_until_prompt_unlocked(current_params_.command_prompt, current_params_.timeout_ms, temp_prompt);
        if (temp_prompt.empty()) {
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
        receive_buffer_.clear();
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
        receive_until_prompt_unlocked(current_params_.command_prompt, current_params_.timeout_ms, temp_prompt);
        if (temp_prompt.empty()) {
            response.error_message = "Prompt not received after exit command";
            response.error_code = 1005;
            return response;
        }

        if (!temp_prompt.empty()) {
            current_prompt_ = temp_prompt;
        }

        receive_buffer_.clear();
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
