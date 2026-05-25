#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <regex>
#include <chrono>

struct ConnectParams {
    std::string ip;
    int port;
    std::string username;
    std::string password;
    std::string username_prompt;  // regex pattern for username prompt
    std::string password_prompt;  // regex pattern for password prompt
    std::string enable_password;
    std::string enable_prompt;    // regex pattern for enable prompt
    std::string command_prompt;   // regex pattern for command prompt ($ or #)
    int timeout_ms;
};

struct CommandRequest {
    std::string command;
    std::string command_id;
    std::chrono::steady_clock::time_point created_at;
};

struct CommandResponse {
    std::string command_id;
    bool success;
    std::string output;
    std::string error_message;
    int error_code;
};

class TelnetClient {
public:
    TelnetClient(const std::string& ip, int port = 23);
    ~TelnetClient();

    // Connection management
    bool connect(const ConnectParams& params);
    bool disconnect();
    bool is_connected() const;
    bool reconnect(const ConnectParams& params);

    // Command execution
    CommandResponse execute_command(const std::string& command, const std::string& cmd_id);
    std::vector<CommandResponse> execute_commands(const std::vector<std::string>& commands);

    // Getters
    std::string get_ip() const { return ip_; }
    int get_port() const { return port_; }
    std::string get_last_error() const { return last_error_; }

private:
    // Socket operations (locked versions - called with lock held)
    bool socket_connect();
    bool socket_disconnect();
    bool send_data_unlocked(const std::string& data);
    std::string receive_data_unlocked(int timeout_ms = 5000);
    bool wait_for_pattern_unlocked(const std::string& pattern, int timeout_ms = 5000);
    std::string read_until_timeout_unlocked(int timeout_ms);

    // Internal helpers
    bool authenticate(const ConnectParams& params);
    bool matches_pattern(const std::string& text, const std::string& pattern);

    // Members
    std::string ip_;
    int port_;
    int socket_fd_;
    bool connected_;
    std::string last_error_;
    ConnectParams current_params_;

    // Buffers and synchronization
    std::string receive_buffer_;
    std::mutex socket_mutex_;
};
