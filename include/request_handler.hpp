#pragma once

#include "telnet_client.hpp"
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

class RequestHandler {
public:
    // Request handlers
    static json handle_connect(const json& request);
    static json handle_execute(const json& request);
    static json handle_status(const json& request);
    static json handle_disconnect(const json& request);

private:
    // Response builders
    static json build_error_response(int code, const std::string& message);
    static json build_success_response(const json& data = json::object());
    static void parse_connect_params(const json& request, json& params_json);
};

// Error codes
namespace ErrorCodes {
    constexpr int SUCCESS = 0;
    constexpr int INVALID_REQUEST = 1000;
    constexpr int MISSING_PARAMS = 1001;
    constexpr int DEVICE_NOT_CONNECTED = 1002;
    constexpr int CONNECTION_FAILED = 1003;
    constexpr int AUTHENTICATION_FAILED = 1004;
    constexpr int COMMAND_EXECUTION_FAILED = 1005;
    constexpr int DEVICE_NOT_FOUND = 1006;
    constexpr int INVALID_IP = 1007;
    constexpr int INTERNAL_ERROR = 1008;
}
