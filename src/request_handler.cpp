#include "request_handler.hpp"
#include "device_manager.hpp"
#include <iostream>
#include <regex>

json RequestHandler::build_error_response(int code, const std::string& message) {
    json response;
    response["success"] = false;
    response["error_code"] = code;
    response["error_message"] = message;
    return response;
}

json RequestHandler::build_success_response(const json& data) {
    json response;
    response["success"] = true;
    response["error_code"] = ErrorCodes::SUCCESS;
    response["data"] = data;
    return response;
}

void RequestHandler::parse_connect_params(const json& request, json& params_json) {
//    json params_json;
    params_json["ip"] = "";
    params_json["port"] = 23;
    params_json["username"] = "";
    params_json["password"] = "";
    params_json["username_prompt"] = "(L|l)ogin:|(U|u)sername:";
    params_json["password_prompt"] = "(P|p)assword:";
    params_json["enable_password"] = "";
    params_json["enable_prompt"] = "(P|p)assword:";
    params_json["command_prompt"] = "[#$]";
    params_json["terminal_nopage"] = "";
    params_json["timeout_ms"] = 5000;
    params_json["keepalive_interval_ms"] = 30000;
    params_json["keepalive_command"] = "\n";

    // Fill params data from request
    for (const auto& it : params_json.items()) {
        if (request.contains(it.key())) params_json[it.key()] = request[it.key()];
    }
}

json RequestHandler::handle_connect(const json& request) {
    try {
        if (!request.contains("ip")) {
            return build_error_response(ErrorCodes::MISSING_PARAMS, "Missing 'ip' parameter");
        }

        std::string ip = request["ip"];

        // Validate IP address
        std::regex ip_regex("^(\\d{1,3}\\.){3}\\d{1,3}$");
        if (!std::regex_match(ip, ip_regex)) {
            return build_error_response(ErrorCodes::INVALID_IP, "Invalid IP address format");
        }

        json params_json;
        parse_connect_params(request, params_json);

        ConnectParams params;
        params.ip = params_json["ip"];
        params.port = params_json["port"];
        params.username = params_json["username"];
        params.password = params_json["password"];
        params.username_prompt = params_json["username_prompt"];
        params.password_prompt = params_json["password_prompt"];
        params.enable_password = params_json["enable_password"];
        params.enable_prompt = params_json["enable_prompt"];
        params.command_prompt = params_json["command_prompt"];
        params.terminal_nopage = params_json["terminal_nopage"];
        params.timeout_ms = params_json["timeout_ms"];
        params.keepalive_interval_ms = params_json["keepalive_interval_ms"];
        params.keepalive_command = params_json["keepalive_command"];

        std::string result = DeviceManager::instance().connect_device(params);
        if (result == "OK") {
            json data;
            data["ip"] = ip;
            data["status"] = "connected";
            return build_success_response(data);
        } else {
            return build_error_response(ErrorCodes::CONNECTION_FAILED, result);
        }
    } catch (const std::exception& e) {
        return build_error_response(ErrorCodes::INTERNAL_ERROR, std::string(e.what()));
    }
}

json RequestHandler::handle_execute(const json& request) {
    try {
        if (!request.contains("ip")) {
            return build_error_response(ErrorCodes::MISSING_PARAMS, "Missing 'ip' parameter");
        }

        std::string ip = request["ip"];

        // Check if device is connected
        std::string status = DeviceManager::instance().get_device_status(ip);
        if (status == "not_found") {
            return build_error_response(ErrorCodes::DEVICE_NOT_CONNECTED, 
                                      "Device not connected. Please connect first.");
        }

        if (!request.contains("segments")) {
            return build_error_response(ErrorCodes::MISSING_PARAMS, "Missing 'segments' parameter");
        }

        std::vector<CommandSegment> segments;
        if (request["segments"].is_array()) {
            for (const auto& seg_json : request["segments"]) {
                CommandSegment segment;

                // Parse path
                if (seg_json.contains("path") && seg_json["path"].is_array()) {
                    for (const auto& path_cmd : seg_json["path"]) {
                        segment.path.push_back(path_cmd.get<std::string>());
                    }
                }

                // Parse commands
                if (seg_json.contains("commands") && seg_json["commands"].is_array()) {
                    for (const auto& cmd : seg_json["commands"]) {
                        segment.commands.push_back(cmd.get<std::string>());
                    }
                }

                segments.push_back(segment);
            }
        } else {
            return build_error_response(ErrorCodes::INVALID_REQUEST, "Segments must be an array");
        }

        auto responses = DeviceManager::instance().execute_segments(ip, segments);
        json data = json::array();
        for (const auto& resp : responses) {
            json item;
            item["command_id"] = resp.command_id;
            item["success"] = resp.success;
            item["output"] = resp.output;
            if (!resp.success) {
                item["error_code"] = resp.error_code;
                item["error_message"] = resp.error_message;
            }
            data.push_back(item);
        }

        return build_success_response(data);
    } catch (const std::exception& e) {
        return build_error_response(ErrorCodes::INTERNAL_ERROR, std::string(e.what()));
    }
}

json RequestHandler::handle_status(const json& /* request */) {
    try {
        json data;
        data["active_connections"] = DeviceManager::instance().get_active_connections_count();

        auto devices = DeviceManager::instance().get_active_devices();
        json devices_array = json::array();
        for (const auto& ip : devices) {
            json device;
            device["ip"] = ip;
            device["status"] = DeviceManager::instance().get_device_status(ip);
            devices_array.push_back(device);
        }
        data["devices"] = devices_array;

        return build_success_response(data);
    } catch (const std::exception& e) {
        return build_error_response(ErrorCodes::INTERNAL_ERROR, std::string(e.what()));
    }
}

json RequestHandler::handle_disconnect(const json& request) {
    try {
        if (!request.contains("ip")) {
            return build_error_response(ErrorCodes::MISSING_PARAMS, "Missing 'ip' parameter");
        }

        std::string ip = request["ip"];
        bool result = DeviceManager::instance().disconnect_device(ip);

        if (result) {
            json data;
            data["ip"] = ip;
            data["status"] = "disconnected";
            return build_success_response(data);
        } else {
            return build_error_response(ErrorCodes::DEVICE_NOT_FOUND, "Device not found");
        }
    } catch (const std::exception& e) {
        return build_error_response(ErrorCodes::INTERNAL_ERROR, std::string(e.what()));
    }
}
