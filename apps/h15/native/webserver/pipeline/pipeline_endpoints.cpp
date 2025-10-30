#include "pipeline.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;

void CppPipeline::register_endpoints()
{
    m_resources->m_srv->Get(
        "/framerate", std::function<nlohmann::json()>([this]() {
            WEBSERVER_LOG_INFO("GET /framerate called");
            auto expected_profile = m_app_resources->media_library->get_current_profile();
            if (!expected_profile.has_value())
            {
                WEBSERVER_LOG_ERROR("Failed to get current profile");
                throw std::runtime_error("Failed to get current profile");
            }
            config_profile_t current_profile = expected_profile.value();
            uint32_t fps = current_profile.application_settings.application_input_streams.resolutions[0].framerate;
            nlohmann::json j;
            j["framerate"] = fps;
            WEBSERVER_LOG_INFO("GET /framerate completed");
            return j;
        }));

    m_resources->m_srv->Get("/flip", std::function<nlohmann::json()>([this]() {
                                WEBSERVER_LOG_INFO("GET /flip called");
                                auto expected_profile = m_app_resources->media_library->get_current_profile();
                                if (!expected_profile.has_value())
                                {
                                    WEBSERVER_LOG_ERROR("Failed to get current profile");
                                    throw std::runtime_error("Failed to get current profile");
                                }
                                config_profile_t current_profile = expected_profile.value();
                                std::string flip =
                                    flip_direction_to_string(current_profile.application_settings.flip.direction);
                                nlohmann::json j;
                                j["flip"] = flip;
                                WEBSERVER_LOG_INFO("GET /flip completed");
                                return j;
                            }));

    m_resources->m_srv->Get("/rotation", std::function<nlohmann::json()>([this]() {
                                WEBSERVER_LOG_INFO("GET /rotation called");
                                auto expected_profile = m_app_resources->media_library->get_current_profile();
                                if (!expected_profile.has_value())
                                {
                                    WEBSERVER_LOG_ERROR("Failed to get current profile");
                                    throw std::runtime_error("Failed to get current profile");
                                }
                                config_profile_t current_profile = expected_profile.value();
                                rotation_angle_t angle = m_rotate_done_in_dewarp
                                                             ? current_profile.application_settings.rotation.angle
                                                             : current_profile.application_settings.rotation.angle;
                                std::string rotation = rotation_angle_to_string(angle);
                                nlohmann::json j;
                                j["rotation"] = rotation;
                                WEBSERVER_LOG_INFO("GET /rotation completed");
                                return j;
                            }));

    m_resources->m_srv->Get("/dewarp", std::function<nlohmann::json()>([this]() {
                                WEBSERVER_LOG_INFO("GET /dewarp called");
                                auto expected_profile = m_app_resources->media_library->get_current_profile();
                                if (!expected_profile.has_value())
                                {
                                    WEBSERVER_LOG_ERROR("Failed to get current profile");
                                    throw std::runtime_error("Failed to get current profile");
                                }
                                config_profile_t current_profile = expected_profile.value();
                                bool dewarp = current_profile.iq_settings.dewarp.enabled;
                                nlohmann::json j;
                                j["dewarp"] = dewarp;
                                WEBSERVER_LOG_INFO("GET /dewarp completed");
                                return j;
                            }));

    m_resources->m_srv->Get("/grayscale", std::function<nlohmann::json()>([this]() {
                                WEBSERVER_LOG_INFO("GET /grayscale called");
                                auto expected_profile = m_app_resources->media_library->get_current_profile();
                                if (!expected_profile.has_value())
                                {
                                    WEBSERVER_LOG_ERROR("Failed to get current profile");
                                    throw std::runtime_error("Failed to get current profile");
                                }
                                config_profile_t current_profile = expected_profile.value();
                                bool grayscale = current_profile.iq_settings.grayscale.enabled;
                                nlohmann::json j;
                                j["grayscale"] = grayscale;
                                WEBSERVER_LOG_INFO("GET /grayscale completed");
                                return j;
                            }));

    m_resources->m_srv->Get("/freeze", std::function<nlohmann::json()>([this]() {
                                WEBSERVER_LOG_INFO("GET /freeze called");
                                bool freeze = m_app_resources->freeze_stage->is_freeze();
                                nlohmann::json j;
                                j["freeze"] = freeze;
                                WEBSERVER_LOG_INFO("GET /freeze completed");
                                return j;
                            }));

    m_resources->m_srv->Get("/detection", std::function<nlohmann::json()>([this]() {
                                WEBSERVER_LOG_INFO("GET /detection called");
                                bool detection = !m_app_resources->overlay_stage->get_skip();
                                nlohmann::json j;
                                j["detection"] = detection;
                                WEBSERVER_LOG_INFO("GET /detection completed");
                                return j;
                            }));

    m_resources->m_srv->Get(
        "/digital_zoom", std::function<nlohmann::json()>([this]() {
            WEBSERVER_LOG_INFO("GET /digital_zoom called");
            auto expected_profile = m_app_resources->media_library->get_current_profile();
            if (!expected_profile.has_value())
            {
                WEBSERVER_LOG_ERROR("Failed to get current profile");
                throw std::runtime_error("Failed to get current profile");
            }
            config_profile_t current_profile = expected_profile.value();
            uint32_t width = current_profile.application_settings.application_input_streams.resolutions[0]
                                 .dimensions.destination_width;
            uint32_t height = current_profile.application_settings.application_input_streams.resolutions[0]
                                  .dimensions.destination_height;
            nlohmann::json j;
            j["digital_zoom"]["enabled"] = current_profile.application_settings.digital_zoom.enabled;
            j["digital_zoom"]["mode"] =
                digital_zoom_mode_string_map.at(current_profile.application_settings.digital_zoom.mode);
            j["digital_zoom"]["magnification"] = current_profile.application_settings.digital_zoom.magnification;
            j["digital_zoom"]["digital_zoom_roi"]["x"] =
                absolut_to_relative(current_profile.application_settings.digital_zoom.roi.x, width);
            j["digital_zoom"]["digital_zoom_roi"]["y"] =
                absolut_to_relative(current_profile.application_settings.digital_zoom.roi.y, height);
            j["digital_zoom"]["digital_zoom_roi"]["width"] =
                absolut_to_relative(current_profile.application_settings.digital_zoom.roi.width, width);
            j["digital_zoom"]["digital_zoom_roi"]["height"] =
                absolut_to_relative(current_profile.application_settings.digital_zoom.roi.height, height);
            WEBSERVER_LOG_INFO("GET /digital_zoom completed");
            return j;
        }));

    m_resources->m_srv->Get(
        "/resolution", std::function<nlohmann::json()>([this]() {
            WEBSERVER_LOG_INFO("GET /resolution called");
            auto expected_profile = m_app_resources->media_library->get_current_profile();
            if (!expected_profile.has_value())
            {
                WEBSERVER_LOG_ERROR("Failed to get current profile");
                throw std::runtime_error("Failed to get current profile");
            }
            config_profile_t current_profile = expected_profile.value();
            WEBSERVER_LOG_DEBUG("Current profile resolution: {}x{}",
                                current_profile.application_settings.application_input_streams.resolutions[0]
                                    .dimensions.destination_width,
                                current_profile.application_settings.application_input_streams.resolutions[0]
                                    .dimensions.destination_height);
            std::string resolution =
                get_resolution_string(current_profile.application_settings.application_input_streams.resolutions[0]
                                          .dimensions.destination_width,
                                      current_profile.application_settings.application_input_streams.resolutions[0]
                                          .dimensions.destination_height);
            nlohmann::json j;
            j["resolution"] = resolution;
            WEBSERVER_LOG_INFO("GET /resolution completed");
            return j;
        }));

    m_resources->m_srv->Get(
        "/denoise", std::function<nlohmann::json()>([this]() {
            WEBSERVER_LOG_INFO("GET /denoise called");
            auto expected_profile = m_app_resources->media_library->get_current_profile();
            if (!expected_profile.has_value())
            {
                WEBSERVER_LOG_ERROR("Failed to get current profile");
                throw std::runtime_error("Failed to get current profile");
            }
            config_profile_t current_profile = expected_profile.value();
            nlohmann::json j;
            j["enabled"] = current_profile.iq_settings.denoise.enabled;
            j["sensor"] = current_profile.iq_settings.denoise.sensor;
            j["loobback_count"] = current_profile.iq_settings.denoise.loopback_count;
            j["method"] = denoise_string_map.at(current_profile.iq_settings.denoise.denoising_quality);
            j["network"]["network_path"] = current_profile.iq_settings.denoise.network_config.network_path;
            j["network"]["y_channel"] = current_profile.iq_settings.denoise.network_config.y_channel;
            j["network"]["uv_channel"] = current_profile.iq_settings.denoise.network_config.uv_channel;
            j["network"]["feedback_y_channel"] = current_profile.iq_settings.denoise.network_config.feedback_y_channel;
            j["network"]["feedback_uv_channel"] =
                current_profile.iq_settings.denoise.network_config.feedback_uv_channel;
            j["network"]["output_y_channel"] = current_profile.iq_settings.denoise.network_config.output_y_channel;
            j["network"]["output_uv_channel"] = current_profile.iq_settings.denoise.network_config.output_uv_channel;

            WEBSERVER_LOG_INFO("GET /denoise completed");
            return j;
        }));

    m_resources->m_srv->Get("/current_profile_name", std::function<nlohmann::json()>([this]() {
                                WEBSERVER_LOG_INFO("GET /current_profile_name called");
                                auto expected_profile = m_app_resources->media_library->get_current_profile();
                                if (!expected_profile.has_value())
                                {
                                    WEBSERVER_LOG_ERROR("Failed to get current profile");
                                    throw std::runtime_error("Failed to get current profile");
                                }
                                config_profile_t current_profile = expected_profile.value();
                                nlohmann::json j;
                                j["profile_name"] = current_profile.name;
                                WEBSERVER_LOG_INFO("GET /current_profile_name completed");
                                return j;
                            }));

    m_resources->m_srv->Get("/automatic_algorithms", std::function<nlohmann::json()>([this]() {
                                WEBSERVER_LOG_INFO("GET /automatic_algorithms called");
                                auto expected_profile = m_app_resources->media_library->get_current_profile_str();
                                if (!expected_profile.has_value())
                                {
                                    WEBSERVER_LOG_ERROR("Failed to get current profile");
                                    throw std::runtime_error("Failed to get current profile");
                                }
                                nlohmann::json full_profile = nlohmann::json::parse(expected_profile.value());
                                nlohmann::json j;
                                j["automatic_algorithms"] = full_profile["iq_settings"]["automatic_algorithms"];
                                WEBSERVER_LOG_INFO("GET /automatic_algorithms completed");
                                return j;
                            }));
    m_resources->m_srv->Put("/automatic_algorithms", [this](const nlohmann::json &j_body) {
        WEBSERVER_LOG_INFO("PUT /automatic_algorithms called");
        if (!j_body.contains("automatic_algorithms"))
        {
            WEBSERVER_LOG_ERROR("automatic_algorithms not found in request body");
            throw std::runtime_error("automatic_algorithms not found in request body");
        }
        auto automatic_algorithms = j_body;
        std::string automatic_algorithms_str = automatic_algorithms.dump();
        WEBSERVER_LOG_DEBUG("automatic_algorithms json: {}", automatic_algorithms_str);
        m_app_resources->media_library->set_automatic_algorithm_configuration(automatic_algorithms_str);
        WEBSERVER_LOG_INFO("PUT /automatic_algorithms completed");
        return nlohmann::json();
    });

    m_resources->m_srv->Put("/reset_stream", [this](const nlohmann::json &req) {
        WEBSERVER_LOG_INFO("POST /reset_stream called");
        stop();
        sleep(1);
        start();
        WEBSERVER_LOG_INFO("POST /reset_stream completed");
        return nlohmann::json();
    });
}
