#include <cstdlib>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <atomic>
#include <catch2/catch.hpp>

#define SERVER_IP "http://10.0.0.1"
#define PAYLOADS_PATH "payloads/"
#define LOG_FILE "webserver-cout.log"

struct test_result
{
    bool error_detected;
    std::string error_message;

    bool if_error_print()
    {
        if (!error_detected)
        {
            std::cerr << "Error: " << error_message << std::endl;
        }
        return error_detected;
    }
};

void monitor_logs(const std::string &log_file, std::atomic<bool> &stop_flag, std::atomic<bool> &error_detected);
void run_server();
void teardown();
size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);
test_result test_get_endpoint(const std::string &endpoint);
nlohmann::json load_payload(const std::string &path);
test_result test_endpoint(const std::string &endpoint, const std::string &jsonData, const std::string &requestType);
