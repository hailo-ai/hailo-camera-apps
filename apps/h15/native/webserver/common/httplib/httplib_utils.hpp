#pragma once

#include <memory>
#include <functional>
#include <nlohmann/json.hpp>
#include <httplib.h>

class HTTPServer
{
private:
    class Impl;
    std::shared_ptr<Impl> m_impl;

public:
    using ExceptionHandler = std::function<void(const httplib::Request& req, httplib::Response& res, std::exception_ptr ep)>;

    HTTPServer();
    static std::shared_ptr<HTTPServer> create();
    void listen(const std::string &host, int port);
    void set_mount_point(const std::string &mount_point, const std::string &path);
    void Get(const std::string &pattern, std::function<void()> callback);
    void Get(const std::string &pattern, std::function<nlohmann::json()> callback);
    void Put(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback);
    void Patch(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback);
    void Post(const std::string &pattern, std::function<void(const nlohmann::json &)> callback);
    void Post(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback);
    void Post(const std::string &pattern, std::function<bool(const httplib::MultipartFormData &)> callback);
    void Redirect(const std::string &pattern, const std::string &target);
    void Delete(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback);
    void set_cors();
    void set_exception_handler(const ExceptionHandler& exception_handler);
};
