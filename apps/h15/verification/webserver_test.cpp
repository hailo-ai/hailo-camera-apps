#include "webserver_test.hpp"


TEST_CASE("Medialib-webserver startup and graceful shutdown with SIGINT", "[medialib-webserver]"){
    run_server();
    teardown();
}

TEST_CASE("Medialib-webserver test get endpoints", "[medialib-webserver]") {
    run_server();
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> error_detected(false);
    std::thread log_thread(monitor_logs, LOG_FILE, std::ref(stop_flag), std::ref(error_detected));
    try{
        std::vector<std::string> endpoints = {
            "/ai",
            "/encoder",
            "/frontend",
            "/isp/refresh",
            "/isp/powerline_frequency",
            "/isp/wdr",
            "/isp/awb",
            "/isp/stream_params",
            "/isp/auto_exposure", 
            "/isp/hdr",
            "/osd",
            "/osd/formats",
            "/osd/images",
            "/privacy_mask",
            "/Offer_RTC"
        };

        for (const auto& endpoint : endpoints) {
            test_result res = test_get_endpoint(endpoint);
            REQUIRE(res.error_detected == true);
            REQUIRE(!error_detected);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        stop_flag = true;
        log_thread.join();
        teardown();
    }
    catch(...){
        stop_flag = true;
        log_thread.join();
        teardown();
    }
}

TEST_CASE("Medialib-webserver test putting default values back to the server", "[medialib-webserver]") {
    run_server();
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> error_detected(false);
    std::thread log_thread(monitor_logs, LOG_FILE, std::ref(stop_flag), std::ref(error_detected));
    try{
        std::string body = load_payload("osd.json").dump();
        test_result res = test_endpoint("/osd", body, "PUT");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        REQUIRE(!error_detected);

        body = load_payload("privacy_mask.json").dump();
        res = test_endpoint("/privacy_mask", body, "PUT");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        REQUIRE(!error_detected);

        body = load_payload("frontend.json").dump();
        res = test_endpoint("/frontend", body, "PUT");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        REQUIRE(!error_detected);

        body = load_payload("encoder.json").dump();
        res = test_endpoint("/encoder", body, "POST");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        REQUIRE(!error_detected);

        stop_flag = true;
        log_thread.join();
        teardown();
    }
    catch(...){
        stop_flag = true;
        log_thread.join();
        teardown();
    }
}


TEST_CASE("Medialib-webserver test encoder change bitrate", "[medialib-webserver]") {
    run_server();
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> error_detected(false);
    std::thread log_thread(monitor_logs, LOG_FILE, std::ref(stop_flag), std::ref(error_detected));
    try{
        nlohmann::json body = load_payload("encoder.json");
        body["bitrate"] = 17000000;
        test_result res = test_endpoint("/encoder", body.dump(), "POST");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        //this it too low of a value but preset neet to override it
        body["bitrate"] = 10000;
        res = test_endpoint("/encoder", body.dump(), "POST");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        stop_flag = true;
        log_thread.join();
        teardown();
    }
    catch(...){
        stop_flag = true;
        log_thread.join();
        teardown();
    }
}

TEST_CASE("Medialib-webserver test frontend framerate", "[medialib-webserver]") {
    run_server();
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> error_detected(false);
    std::thread log_thread(monitor_logs, LOG_FILE, std::ref(stop_flag), std::ref(error_detected));
    try{
        //test frontend
        //change framerate
        nlohmann::json body_frontend = load_payload("frontend.json");
        body_frontend["output_video"]["resolutions"][0]["framerate"] = 20;
        nlohmann::json rotation_payload = nlohmann::json();
        rotation_payload["output_video"] = body_frontend["output_video"];
        test_result res = test_endpoint("/frontend", rotation_payload.dump(), "PATCH");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        REQUIRE(!error_detected);

        stop_flag = true;
        log_thread.join();
        teardown();

    }
    catch(...){
        stop_flag = true;
        log_thread.join();
        teardown();
    }
}

TEST_CASE("Medialib-webserver test frondend rotation", "[medialib-webserver]") {
    run_server();
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> error_detected(false);
    std::thread log_thread(monitor_logs, LOG_FILE, std::ref(stop_flag), std::ref(error_detected));
    try{

        //change rotation to 90
        nlohmann::json body_frontend = load_payload("frontend.json");
        body_frontend["rotation"]["enabled"] = true;
        body_frontend["rotation"]["angle"] = "ROTATION_ANGLE_90";
        test_result res = test_endpoint("/frontend", body_frontend.dump(), "PUT");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(5));
        REQUIRE(!error_detected);

        stop_flag = true;
        log_thread.join();
        teardown();
    }
    catch(...){
        stop_flag = true;
        log_thread.join();
        teardown();
    }
}

TEST_CASE("Medialib-webserver test frondend resolution", "[medialib-webserver]") {
    run_server();
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> error_detected(false);
    std::thread log_thread(monitor_logs, LOG_FILE, std::ref(stop_flag), std::ref(error_detected));
    try{

        //change resolution
        nlohmann::json body_frontend = load_payload("frontend.json");
        body_frontend["output_video"]["resolutions"][0]["width"] = 1280;
        body_frontend["output_video"]["resolutions"][0]["height"] = 720;
        nlohmann::json resolution_payload = nlohmann::json();
        resolution_payload["output_video"] = body_frontend["output_video"];
        test_result res = test_endpoint("/frontend", resolution_payload.dump(), "PATCH");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(5));
        REQUIRE(!error_detected);

        stop_flag = true;
        log_thread.join();
        teardown();
    }
    catch(...){
        stop_flag = true;
        log_thread.join();
        teardown();
    }
}

TEST_CASE("Medialib-webserver test osd", "[medialib-webserver]") {
    run_server();
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> error_detected(false);
    std::thread log_thread(monitor_logs, LOG_FILE, std::ref(stop_flag), std::ref(error_detected));
    try{
        //test delete all text items
        nlohmann::json body = load_payload("osd.json");
        body["text"]["items"] = nlohmann::json::array();
        test_result res = test_endpoint("/osd", body.dump(), "PUT");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        //test delete all dateTime items and put back back text items
        body = load_payload("osd.json");
        body["dateTime"]["items"] = nlohmann::json::array();
        res = test_endpoint("/osd", body.dump(), "PUT");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        //clean all osd items
        body = load_payload("osd.json");
        body["image"]["items"] = nlohmann::json::array();
        body["dateTime"]["items"] = nlohmann::json::array();
        body["text"]["items"] = nlohmann::json::array();
        res = test_endpoint("/osd", body.dump(), "PUT");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        //add them all back
        body = load_payload("osd.json");
        res = test_endpoint("/osd", body.dump(), "PUT");
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        stop_flag = true;
        log_thread.join();
        teardown();
    }
    catch(...){
        stop_flag = true;
        log_thread.join();
        teardown();
    }
}