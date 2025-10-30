#pragma once
#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <tl/expected.hpp>
#include <signal.h>
#include <cxxopts/cxxopts.hpp>
#include <signal.h>
#include <condition_variable>
#include <mutex>
#include "scenarios/generate_pipeline.hpp"


frontend_config_t get_current_frontend_config(std::shared_ptr<AppResources> app_resources)
{
    auto config_expected = app_resources->media_library->m_frontend->get_config();
    if (!config_expected)
    {
        throw std::runtime_error("Failed to get current frontend config");
    }

    frontend_config_t config = config_expected.value();
    return config;
}


void stop_pipeline_and_clean(std::shared_ptr<AppResources> app_resources)
{
    app_resources->pipeline->stop_pipeline();
    app_resources->media_library->stop_pipeline();
    app_resources->clear();
}


void initialize_pipeline_and_start(std::shared_ptr<AppResources> app_resources)
{
    // Configure frontend and encoders
    configure_frontend_and_encoders(app_resources);

    // Create pipeline and stages
    create_main_pipeline(app_resources);

    // Start pipeline
    std::cout << "Starting." << std::endl;
    REFERENCE_CAMERA_LOG_INFO("Starting.");
    app_resources->media_library->start_pipeline();
    app_resources->pipeline->start_pipeline();

    REFERENCE_CAMERA_LOG_INFO("Started playing");

}

void restart_from_scratch(std::shared_ptr<AppResources> app_resources)
{
    std::string medialib_config_path = app_resources->medialib_config_path;
    std::string profile_name = app_resources->profile_name;
    std::string host_ip = app_resources->host_ip;
    TestScenarios test_scenario = app_resources->test_scenario;
    stop_pipeline_and_clean(app_resources);

    app_resources->medialib_config_path = medialib_config_path;
    app_resources->profile_name = profile_name;
    app_resources->host_ip = host_ip;
    app_resources->test_scenario = test_scenario;
    initialize_pipeline_and_start(app_resources);
}

void configure_sensor_frontend_randomly(std::shared_ptr<AppResources> app_resources, int timeout, std::atomic<bool>& stop_requested)
{

    std::vector<std::tuple<bool, bool, rotation_angle_t, bool, flip_direction_t>> all_permutations = {
        {true, true, ROTATION_ANGLE_0, false, FLIP_DIRECTION_NONE},
        {true, true, ROTATION_ANGLE_90, false, FLIP_DIRECTION_NONE},
        {true, true, ROTATION_ANGLE_180, false, FLIP_DIRECTION_NONE},
        {true, true, ROTATION_ANGLE_270, false, FLIP_DIRECTION_NONE},
        {true,false ,ROTATION_ANGLE_0 ,true ,FLIP_DIRECTION_HORIZONTAL},
        {true,false ,ROTATION_ANGLE_90 ,true ,FLIP_DIRECTION_HORIZONTAL},
        {true,false ,ROTATION_ANGLE_180 ,true ,FLIP_DIRECTION_HORIZONTAL},
        {true,false ,ROTATION_ANGLE_270 ,true ,FLIP_DIRECTION_HORIZONTAL},
        {true,true ,ROTATION_ANGLE_0 ,true ,FLIP_DIRECTION_HORIZONTAL},
        {true,true ,ROTATION_ANGLE_90 ,true ,FLIP_DIRECTION_HORIZONTAL},
        {true,true ,ROTATION_ANGLE_180 ,true ,FLIP_DIRECTION_HORIZONTAL},
        {true,true ,ROTATION_ANGLE_270 ,true ,FLIP_DIRECTION_HORIZONTAL}
    };

    for (const auto& perm : all_permutations)
    {
        if (stop_requested) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(timeout));
        bool enable_dewarp = std::get<0>(perm);
        bool enable_rotation = std::get<1>(perm);
        rotation_angle_t rotation = std::get<2>(perm);
        bool enable_flip = std::get<3>(perm);
        flip_direction_t flip = std::get<4>(perm);

        std::cout << "Reconfiguring frontend with settings: "
                  << "LDC: " << enable_dewarp
                  << ", Rotation: " << enable_rotation
                  << ", Rotation val: " << rotation
                  << ", Flip: " << enable_flip
                  << ", Flip val: " << flip
                  << std::endl;

        frontend_config_t current_config = get_current_frontend_config(app_resources);

        current_config.ldc_config.dewarp_config.enabled = enable_dewarp;
        current_config.ldc_config.rotation_config.enabled = enable_rotation;
        current_config.ldc_config.rotation_config.angle = rotation;
        current_config.ldc_config.flip_config.enabled = enable_flip;
        current_config.ldc_config.flip_config.direction = flip;

        // Reconfigure the frontend
        media_library_return status = app_resources->media_library->m_frontend->set_config(current_config);
        if (status != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            std::cerr << "Failed to reconfigure frontend" << std::endl;
            continue;
        }

        std::cout << "Successfully reconfigured frontend" << std::endl;
    }
}


void configure_encoder_randomly(std::shared_ptr<AppResources> app_resources, int timeout, std::atomic<bool>& stop_requested)
{
    const std::vector<int> bitrates = {2000000, 4000000, 8000000, 16000000}; // in bps
    // const std::vector<rc_mode_t> rate_control_modes = {rc_mode_t::CVBR, rc_mode_t::VBR, rc_mode_t::HRD, rc_mode_t::CQP};
    const std::vector<rc_mode_t> rate_control_modes = {rc_mode_t::CVBR, rc_mode_t::VBR, rc_mode_t::HRD};
    const std::vector<int> gop_sizes = {2, 1, 5};

    for (int bitrate : bitrates)
    {
        for (const auto& rc_mode : rate_control_modes)
        {
            for (int gop_size : gop_sizes)
            {
                if (stop_requested) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::seconds(timeout));
                std::cout << "Reconfiguring encoder with settings: "
                          << "Bitrate: " << bitrate
                          << ", Rate Control Mode: " << rc_mode
                          << ", GOP Size: " << gop_size
                          << std::endl;
                auto encoder_config = app_resources->media_library->m_encoders.begin()->second->get_user_config();
                hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
                hailo_encoder_config.rate_control.bitrate.target_bitrate = bitrate;
                hailo_encoder_config.rate_control.rc_mode = rc_mode;
                hailo_encoder_config.gop.gop_size = gop_size;
                // Reconfigure the encoder
                media_library_return status = app_resources->media_library->m_encoders.begin()->second->set_config(encoder_config);
                if (status != media_library_return::MEDIA_LIBRARY_SUCCESS)
                {
                    std::cerr << "Failed to reconfigure encoder" << std::endl;
                    continue;
                }

                std::cout << "Successfully reconfigured encoder" << std::endl;
            }
        }
    }   
}


void switch_profiles_randomly(std::shared_ptr<AppResources> app_resources, int timeout, std::atomic<bool>& stop_requested)
{
    std::vector<std::string> profile_names = get_profile_names(app_resources->medialib_config_path);
    if (profile_names.empty())
    {
        std::cerr << "No profiles found to switch" << std::endl;
        return;
    }

    // Randomize the order of profile_names
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    std::random_shuffle(profile_names.begin(), profile_names.end());

    for (const std::string& profile : profile_names)
    {
        if (stop_requested) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(timeout));
        std::cout << "Switching to profile: " << profile << std::endl;
        app_resources->media_library->set_profile(profile);
        std::cout << "Successfully switched to profile: " << profile << std::endl;
    }
}