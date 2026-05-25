#include "device_manager.hpp"
#include <iostream>
#include <algorithm>

DeviceManager& DeviceManager::instance() {
    static DeviceManager manager;
    return manager;
}

std::shared_ptr<DeviceConnection> DeviceManager::get_or_create_device(const std::string& ip) {
    std::lock_guard<std::mutex> lock(devices_mutex_);

    auto it = devices_.find(ip);
    if (it != devices_.end()) {
        return it->second;
    }

    auto device = std::make_shared<DeviceConnection>();
    device->client = std::make_shared<TelnetClient>(ip);
    device->authenticated = false;
    device->last_used = std::chrono::steady_clock::now();

    devices_[ip] = device;
    return device;
}

std::string DeviceManager::connect_device(const ConnectParams& params) {
    auto device = get_or_create_device(params.ip);

    if (device->client->connect(params)) {
        device->params = params;
        device->authenticated = true;
        device->last_used = std::chrono::steady_clock::now();
        return "OK";
    }

    device->authenticated = false;
    return device->client->get_last_error();
}

bool DeviceManager::ensure_device_connected(const std::string& ip) {
    std::lock_guard<std::mutex> lock(devices_mutex_);

    auto it = devices_.find(ip);
    if (it == devices_.end()) {
        return false;
    }

    auto device = it->second;
    return device->client->is_connected() && device->authenticated;
}

CommandResponse DeviceManager::execute_segment(const std::string& ip, const CommandSegment& segment) {
    CommandResponse response;
    response.success = false;

    if (!ensure_device_connected(ip)) {
        response.error_message = "Device not connected: " + ip;
        response.error_code = 1002;
        return response;
    }

    auto device = [this, ip]() -> std::shared_ptr<DeviceConnection> {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        auto it = devices_.find(ip);
        return (it != devices_.end()) ? it->second : nullptr;
    }();

    if (!device) {
        response.error_message = "Device not found: " + ip;
        response.error_code = 1006;
        return response;
    }

    response = device->client->execute_segment(segment, "");
    device->last_used = std::chrono::steady_clock::now();
    return response;
}

std::vector<CommandResponse> DeviceManager::execute_segments(const std::string& ip,
                                                              const std::vector<CommandSegment>& segments) {
    std::vector<CommandResponse> responses;

    if (!ensure_device_connected(ip)) {
        CommandResponse error_response;
        error_response.success = false;
        error_response.error_message = "Device not connected: " + ip;
        error_response.error_code = 1002;
        responses.push_back(error_response);
        return responses;
    }

    auto device = [this, ip]() -> std::shared_ptr<DeviceConnection> {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        auto it = devices_.find(ip);
        return (it != devices_.end()) ? it->second : nullptr;
    }();

    if (!device) {
        CommandResponse error_response;
        error_response.success = false;
        error_response.error_message = "Device not found: " + ip;
        error_response.error_code = 1006;
        responses.push_back(error_response);
        return responses;
    }

    for (size_t i = 0; i < segments.size(); ++i) {
        auto response = device->client->execute_segment(segments[i], std::to_string(i));
        responses.push_back(response);
    }

    device->last_used = std::chrono::steady_clock::now();
    return responses;
}

bool DeviceManager::disconnect_device(const std::string& ip) {
    std::lock_guard<std::mutex> lock(devices_mutex_);

    auto it = devices_.find(ip);
    if (it != devices_.end()) {
        it->second->client->disconnect();
        it->second->authenticated = false;
        devices_.erase(it);
        return true;
    }

    return false;
}

std::string DeviceManager::get_device_status(const std::string& ip) {
    std::lock_guard<std::mutex> lock(devices_mutex_);

    auto it = devices_.find(ip);
    if (it == devices_.end()) {
        return "not_found";
    }

    if (it->second->client->is_connected()) {
        return "connected";
    }

    return "disconnected";
}

int DeviceManager::get_active_connections_count() const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    return devices_.size();
}

std::vector<std::string> DeviceManager::get_active_devices() const {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    std::vector<std::string> result;
    for (const auto& pair : devices_) {
        result.push_back(pair.first);
    }
    return result;
}
