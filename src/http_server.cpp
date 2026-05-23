#include "http_server.hpp"
#include "request_handler.hpp"
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <thread>
#include <iostream>

using json = nlohmann::json;

HTTPServer::HTTPServer(int port) : port_(port), running_(false), server_(nullptr) {}

HTTPServer::~HTTPServer() {
    stop();
}

bool HTTPServer::start() {
    auto server = new httplib::Server();
    server_ = server;
    running_ = true;

    // POST /connect
    server->Post("/connect", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto request = json::parse(req.body);
            auto response = RequestHandler::handle_connect(request);
            res.set_content(response.dump(), "application/json");
            res.status = 200;
        } catch (const std::exception& e) {
            json error = json::object();
            error["success"] = false;
            error["error_code"] = 1000;
            error["error_message"] = std::string("Invalid JSON: ") + e.what();
            res.set_content(error.dump(), "application/json");
            res.status = 400;
        }
    });

    // POST /execute
    server->Post("/execute", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto request = json::parse(req.body);
            auto response = RequestHandler::handle_execute(request);
            res.set_content(response.dump(), "application/json");
            res.status = 200;
        } catch (const std::exception& e) {
            json error = json::object();
            error["success"] = false;
            error["error_code"] = 1000;
            error["error_message"] = std::string("Invalid JSON: ") + e.what();
            res.set_content(error.dump(), "application/json");
            res.status = 400;
        }
    });

    // GET /status
    server->Get("/status", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto request = json::object();
            auto response = RequestHandler::handle_status(request);
            res.set_content(response.dump(), "application/json");
            res.status = 200;
        } catch (const std::exception& e) {
            json error = json::object();
            error["success"] = false;
            error["error_code"] = 1008;
            error["error_message"] = std::string(e.what());
            res.set_content(error.dump(), "application/json");
            res.status = 500;
        }
    });

    // POST /disconnect
    server->Post("/disconnect", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto request = json::parse(req.body);
            auto response = RequestHandler::handle_disconnect(request);
            res.set_content(response.dump(), "application/json");
            res.status = 200;
        } catch (const std::exception& e) {
            json error = json::object();
            error["success"] = false;
            error["error_code"] = 1000;
            error["error_message"] = std::string("Invalid JSON: ") + e.what();
            res.set_content(error.dump(), "application/json");
            res.status = 400;
        }
    });

    // Start server in a background thread
    std::thread server_thread([this, server]() {
        server->listen("0.0.0.0", port_);
    });
    server_thread.detach();

    std::cout << "HTTP Server started on port " << port_ << std::endl;
    return true;
}

void HTTPServer::stop() {
    if (server_) {
        auto server = static_cast<httplib::Server*>(server_);
        server->stop();
        delete server;
        server_ = nullptr;
    }
    running_ = false;
}
