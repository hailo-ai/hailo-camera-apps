#include "webserver_test.hpp"

void monitor_logs(const std::string& log_file, std::atomic<bool>& stop_flag, std::atomic<bool>& error_detected) {
    std::ofstream(log_file, std::ofstream::out | std::ofstream::trunc).close();
    std::ifstream log_stream(log_file);
    std::string line;
    std::vector<std::string> errors = {"ERROR", "CRITICAL", "error", "critical", "std::logic_error", "std::runtime_error", "std::exception", "std::bad_alloc", "std::bad_function_call", "std::bad_typeid", "std::bad_cast", "std::bad_weak_ptr", "std::bad_array_new_length", "std::bad_exception", "std::bad_variant_access", "std::bad_optional_access", "std::bad_any_cast"};
    while (!stop_flag) {
        if (std::getline(log_stream, line)) {
            if(std::any_of(errors.begin(), errors.end(), [&line](const std::string& error){ return line.find(error) != std::string::npos; })){
                error_detected = true;
                std::cerr << "Error: " << line << std::endl;
            }
        } else {
            // If no new line is available, wait for a short period before trying again
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            log_stream.clear(); // Clear EOF flag if reached end of file
        }
    }
}

void run_server() {
    std::thread server_thread([]()
    {
        int result = std::system("GST_DEBUG=3 /usr/bin/medialib-webserver > " LOG_FILE " 2>&1 &");
        if (result != 0) {
            std::cerr << "Failed to start medialib-webserver" << std::endl;
        }
    });
    std::this_thread::sleep_for(std::chrono::seconds(5));
    server_thread.detach();
}

void teardown() {
    try{
        // Try to stop the server with Ctrl-C
        int result = std::system("pkill -2 -f medialib-webserver");
        REQUIRE(result == 0); // Failed to stop the server with SIGINT
        // Give some time for the server to shut down
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    catch(...){
        int result = std::system("pkill -9 -f medialib-webserver");
        REQUIRE(result == 0); // Failed to stop the server with SIGKILL
    }
}


size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

test_result test_get_endpoint(const std::string& endpoint) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl == NULL){
        return {false, "Failed to initialize curl"};
    }

    if(curl) {
        std::string url = std::string(SERVER_IP) + endpoint;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
        headers = curl_slist_append(headers, ("Referer: " + std::string(SERVER_IP) + "/").c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK){
            return {false, "Failed to perform curl"};
        }

        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if(response_code != 200){
            return {false, "Response code: " + std::to_string(response_code) + " instead of 200"};
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    return {true, ""};
}

nlohmann::json load_payload(const std::string& path) {
            std::ifstream file((std::string(PAYLOADS_PATH) + path).c_str());
            REQUIRE(file.is_open());

            nlohmann::json payload;
            file >> payload;
            file.close();
            return payload;
        }

test_result test_endpoint(const std::string& endpoint, const std::string& jsonData, const std::string& requestType) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl == NULL){
        return {false, "Failed to initialize curl"};
    }
    if(curl) {
        std::string url = std::string(SERVER_IP) + endpoint;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        if (requestType == "PATCH") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        } else if (requestType == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
        } else if (requestType == "PUT"){
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        }
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: text/plain");
        headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
        headers = curl_slist_append(headers, ("Referer: " + std::string(SERVER_IP) + "/").c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK){
            return {false, "Failed to perform curl: " + std::string(curl_easy_strerror(res))};
        }

        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if(response_code != 200){
            return {false, "Response code: " + std::to_string(response_code) + " instead of 200"};
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    return {true, ""};
}