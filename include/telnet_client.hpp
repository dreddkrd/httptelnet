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

enum class ConnectionStatus {
    CONNECTED,
    DISCONNECTED,
    UNAVAILABLE,  // Device unavailable after 3 failed reconnection attempts
    RECONNECTING   // Background reconnection in progress
};

struct ConnectParams {
    std::string ip;
    int port;
    std::string username;
    std::string password;
    std::string username_prompt;      // regex pattern for username prompt
    std::string password_prompt;      // regex pattern for password prompt
    std::string enable_password;      // optional
    std::string enable_prompt;        // regex pattern for enable prompt
    std::string command_prompt;       // regex pattern for command prompt ($ or #)
    std::string terminal_nopage;      // command to disable pagination (e.g., "terminal length 0")
    int timeout_ms;                   // connection timeout
    int keepalive_interval_ms;        // keepalive interval (default 30000 ms = 30 sec)
    std::string keepalive_command;    // keepalive command (default "\n")
};

struct CommandSegment {
    std::vector<std::string> path;    // navigation commands
    std::vector<std::string> commands; // actual commands to execute
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
    ConnectionStatus get_status() const;
    std::string get_status_string() const;

    // Command execution
    CommandResponse execute_segment(const CommandSegment& segment, const std::string& cmd_id);
    std::vector<CommandResponse> execute_segments(const std::vector<CommandSegment>& segments);

    // Getters
    std::string get_ip() const { return ip_; }
    int get_port() const { return port_; }
    std::string get_last_error() const { return last_error_; }

private:
    // Socket operations (called with lock held)
    bool socket_connect();
    bool socket_disconnect();
    bool send_data_unlocked(const std::string& data);
    std::string receive_until_prompt_unlocked(const std::string& prompt_pattern, int timeout_ms, std::string& captured_prompt);
    std::string read_until_timeout_unlocked(int timeout_ms);
    bool check_connection_unlocked();

    // Internal helpers
    bool authenticate(const ConnectParams& params);
    bool try_reconnect_unlocked();  // Single reconnection attempt
    bool wait_for_reconnection_unlocked(int max_wait_ms = 5000);  // Wait for successful reconnection

    // Reconnection thread functions
    void reconnection_thread_func();
    void start_reconnection_thread();
    void stop_reconnection_thread();

    bool matches_pattern(const std::string& text, const std::string& pattern) const;
    std::string extract_last_prompt_line(const std::string& text, const std::string& prompt_pattern) const;

    // Keepalive thread
    void keepalive_thread_func();
    void start_keepalive_thread();
    void stop_keepalive_thread();

    // Members
    std::string ip_;
    int port_;
    int socket_fd_;
    std::string last_error_;
    ConnectParams current_params_;

    // Buffers and synchronization
    std::string receive_buffer_;
    std::string current_prompt_;      // cached prompt for current level
    std::mutex socket_mutex_;

    // Connection status management
    ConnectionStatus connection_status_;
    std::mutex status_mutex_;
    std::condition_variable status_changed_;
    int failed_reconnect_attempts_;   // Counter for reconnection failures
    std::chrono::steady_clock::time_point last_reconnect_attempt_;

    // Keepalive thread
    std::thread keepalive_thread_;
    bool keepalive_running_;
    std::mutex keepalive_mutex_;

    // Background reconnection thread
    std::thread reconnection_thread_;
    bool reconnection_thread_running_;
    std::mutex reconnection_thread_mutex_;
};
