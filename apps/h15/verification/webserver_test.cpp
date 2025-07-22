#include "webserver_test.hpp"

TEST_CASE("camera-viewer-server startup and graceful shutdown with SIGINT", "[camera-viewer-server]")
{
    run_server();
    teardown();
}

TEST_CASE("camera-viewer-server test get endpoints", "[camera-viewer-server]")
{
    run_server();
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> error_detected(false);
    std::thread log_thread(monitor_logs, LOG_FILE, std::ref(stop_flag), std::ref(error_detected));
    try
    {
        std::vector<std::string> endpoints = {
            "/encoder",
            "/medialib_config",
            "/current_profile_name",
            "/isp/refresh",
            "/isp/powerline_frequency",
            "/isp/wdr",
            "/isp/awb",
            "/isp/stream_params",
            "/isp/auto_exposure",
            "/osd",
            "/osd/formats",
            "/osd/images",
            "/privacy_mask",
            "/Offer_RTC",
            "/framerate",
            "/flip",
            "/rotation",
            "/dewarp",
            "/freeze",
            "/digital_image_stabilization",
            "/electronic_image_stabilization",
            "/grayscale",
            "/digital_zoom",
            "/detection",
            "/denoise",
            "/architecture",
        };

        for (const auto &endpoint : endpoints)
        {
            test_result res = test_get_endpoint(endpoint);
            if (!res.if_error_print())
            {
                std::cerr << "GET " << endpoint << " faild" << std::endl;
            }
            REQUIRE(res.error_detected == true);
            REQUIRE(!error_detected);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        stop_flag = true;
        log_thread.join();
        teardown();
    }
    catch (...)
    {
        stop_flag = true;
        log_thread.join();
        teardown();
    }
}

TEST_CASE("camera-viewer-server test putting default values back to the server", "[camera-viewer-server]")
{
    run_server();
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> error_detected(false);
    std::thread log_thread(monitor_logs, LOG_FILE, std::ref(stop_flag), std::ref(error_detected));
    try
    {
        std::string body = load_payload("osd.json").dump();
        test_result res = test_endpoint("/osd", body, "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        REQUIRE(!error_detected);

        body = load_payload("privacy_mask.json").dump();
        res = test_endpoint("/privacy_mask", body, "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        REQUIRE(!error_detected);

        body = load_payload("encoder.json").dump();
        res = test_endpoint("/encoder", body, "POST");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        REQUIRE(!error_detected);

        stop_flag = true;
        log_thread.join();
        teardown();
    }
    catch (...)
    {
        stop_flag = true;
        log_thread.join();
        teardown();
    }
}

TEST_CASE("camera-viewer-server test encoder change bitrate", "[camera-viewer-server]")
{
    run_server();
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> error_detected(false);
    std::thread log_thread(monitor_logs, LOG_FILE, std::ref(stop_flag), std::ref(error_detected));
    try
    {
        nlohmann::json body = load_payload("encoder.json");
        body["bitrate"] = 17000000;
        test_result res = test_endpoint("/encoder", body.dump(), "POST");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        // this it too low of a value but preset neet to override it
        body["bitrate"] = 10000;
        res = test_endpoint("/encoder", body.dump(), "POST");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        stop_flag = true;
        log_thread.join();
        teardown();
    }
    catch (...)
    {
        stop_flag = true;
        log_thread.join();
        teardown();
    }
}

TEST_CASE("camera-viewer-server test osd", "[camera-viewer-server]")
{
    run_server();
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> error_detected(false);
    std::thread log_thread(monitor_logs, LOG_FILE, std::ref(stop_flag), std::ref(error_detected));
    try
    {
        // test delete all text items
        nlohmann::json body = load_payload("osd.json");
        body["text"]["items"] = nlohmann::json::array();
        test_result res = test_endpoint("/osd", body.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        // test delete all dateTime items and put back back text items
        body = load_payload("osd.json");
        body["dateTime"]["items"] = nlohmann::json::array();
        res = test_endpoint("/osd", body.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        // clean all osd items
        body = load_payload("osd.json");
        body["image"]["items"] = nlohmann::json::array();
        body["dateTime"]["items"] = nlohmann::json::array();
        body["text"]["items"] = nlohmann::json::array();
        res = test_endpoint("/osd", body.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        // add them all back
        body = load_payload("osd.json");
        res = test_endpoint("/osd", body.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        stop_flag = true;
        log_thread.join();
        teardown();
    }
    catch (...)
    {
        stop_flag = true;
        log_thread.join();
        teardown();
    }
}

TEST_CASE("camera-viewer-server test change framerate -> zoom -> rotate -> resolution change -> zoom -> rotate",
          "[camera-viewer-server]")
{
    run_server();
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> error_detected(false);
    std::thread log_thread(monitor_logs, LOG_FILE, std::ref(stop_flag), std::ref(error_detected));
    try
    {
        // change framerate
        nlohmann::json payload = nlohmann::json();
        payload["framerate"] = 10;
        test_result res = test_endpoint("/framerate", payload.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        REQUIRE(!error_detected);

        // digital_zoom_roi
        payload = nlohmann::json();
        payload["digital_zoom"]["enabled"] = true;
        payload["digital_zoom"]["mode"] = "DIGITAL_ZOOM_MODE_MAGNIFICATION";
        payload["digital_zoom"]["magnification"] = 2;
        payload["digital_zoom"]["digital_zoom_roi"]["height"] = 0.5;
        payload["digital_zoom"]["digital_zoom_roi"]["width"] = 0.5;
        payload["digital_zoom"]["digital_zoom_roi"]["x"] = 0.3;
        payload["digital_zoom"]["digital_zoom_roi"]["y"] = 0.3;
        res = test_endpoint("/digital_zoom", payload.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        REQUIRE(!error_detected);

        // disable digital zoom
        payload["digital_zoom"]["enabled"] = false;
        res = test_endpoint("/digital_zoom", payload.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        REQUIRE(!error_detected);

        // change rotation to 90
        nlohmann::json body_frontend = nlohmann::json();
        body_frontend["rotation"] = "ROTATION_ANGLE_90";
        res = test_endpoint("/rotation", body_frontend.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(4));
        REQUIRE(!error_detected);

        // change resolution
        nlohmann::json resolution_payload = nlohmann::json();
        resolution_payload["resolution"] = "HD";
        res = test_endpoint("/resolution", resolution_payload.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(4));
        REQUIRE(!error_detected);

        // digital_zoom_roi
        payload = nlohmann::json();
        payload["digital_zoom"]["enabled"] = true;
        payload["digital_zoom"]["mode"] = "DIGITAL_ZOOM_MODE_ROI";
        payload["digital_zoom"]["magnification"] = 3;
        payload["digital_zoom"]["digital_zoom_roi"]["height"] = 0.5;
        payload["digital_zoom"]["digital_zoom_roi"]["width"] = 0.5;
        payload["digital_zoom"]["digital_zoom_roi"]["x"] = 0.3;
        payload["digital_zoom"]["digital_zoom_roi"]["y"] = 0.3;
        res = test_endpoint("/digital_zoom", payload.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        REQUIRE(!error_detected);

        // change rotation to 0
        body_frontend = nlohmann::json();
        body_frontend["rotation"] = "ROTATION_ANGLE_0";
        res = test_endpoint("/rotation", body_frontend.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(4));
        REQUIRE(!error_detected);

        // change dewarp
        nlohmann::json dewarp_payload = nlohmann::json();
        dewarp_payload["dewarp"] = true;
        res = test_endpoint("/dewarp", dewarp_payload.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        REQUIRE(!error_detected);

        // change rotation to 90
        body_frontend = nlohmann::json();
        body_frontend["rotation"] = "ROTATION_ANGLE_90";
        res = test_endpoint("/rotation", body_frontend.dump(), "PUT");
        res.if_error_print();
        REQUIRE(res.error_detected == true);
        std::this_thread::sleep_for(std::chrono::seconds(4));
        REQUIRE(!error_detected);

        stop_flag = true;
        log_thread.join();
        teardown();
    }
    catch (...)
    {
        stop_flag = true;
        log_thread.join();
        teardown();
    }
}
