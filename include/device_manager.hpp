#pragma once

#include "telnet_client.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>

struct DeviceConnection {
    std::shared_ptr<TelnetClient> client;
    ConnectParams params;
    std::chrono::steady_clock::time_point last_used;
    bool authenticated;
};

class DeviceManager {
public:
    static DeviceManager& instance();

    // Connection lifecycle
    std::string connect_device(const ConnectParams& params);
    CommandResponse execute_segment(const std::string& ip, const CommandSegment& segment);
    std::vector<CommandResponse> execute_segments(const std::string& ip, 
                                                   const std::vector<CommandSegment>& segments);
    bool disconnect_device(const std::string& ip);
    std::string get_device_status(const std::string& ip);

    // Connection pool management
    int get_active_connections_count() const;
    std::vector<std::string> get_active_devices() const;

private:
    DeviceManager() = default;
    ~DeviceManager() = default;

    // Prevent copying
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    // Internal helpers
    std::shared_ptr<DeviceConnection> get_or_create_device(const std::string& ip);
    bool ensure_device_connected(const std::string& ip);

    // Members
    mutable std::mutex devices_mutex_;
    std::map<std::string, std::shared_ptr<DeviceConnection>> devices_;  // key: IP
};
