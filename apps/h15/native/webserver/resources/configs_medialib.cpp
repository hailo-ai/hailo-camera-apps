#include "configs.hpp"
#include "media_library/config_manager.hpp"
#include "media_library/encoder_config_types.hpp"
#include "media_library/media_library_types.hpp"
#include "pipeline/pipeline.hpp"

#define DEFAULT_CONFIGS_PATH "/etc/imaging/cfg/medialib_configs/"
#define APPEND_CONFIG_PATH(path) DEFAULT_CONFIGS_PATH path
#define DEFAULT_MEDIALIB_CONFIG_PATH APPEND_CONFIG_PATH("webserver_medialib_config.json")

using namespace webserver::resources;

ConfigResourceMedialib::ConfigResourceMedialib(std::shared_ptr<EventBus> event_bus, std::string config_path)
    : ConfigResourceBase(event_bus)
{
    std::string medialib_config_path = DEFAULT_MEDIALIB_CONFIG_PATH;
    if (!config_path.empty())
    {
        medialib_config_path = config_path;
    }
    // Load default medialib config
    auto medialib_config = load_config_from_file(medialib_config_path);
    if (!medialib_config.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to load default medialib config: {}", medialib_config.error());
        throw std::runtime_error("Failed to load default medialib config: " + medialib_config.error());
    }
    m_medialib_config = medialib_config.value();

    // Load default profile config
    m_default_profile_name = m_medialib_config["default_profile"];

    subscribe_callback({EventType::PROFILE_UPDATE, EventType::PIPELINE_READY}, EventPriority::EVENT_PRIORITY_VERY_HIGH,
                       [this](ResourceStateChangeNotification notification) {
                           WEBSERVER_LOG_INFO("Received PROFILE_UPDATE notification");
                           auto state = notification.getResourceStateFromBase<ProfileState>();
                           m_current_profile = state->value;
                           auto conf_succsess = extract_profile_data(m_default_profile_name);
                           if (!conf_succsess.has_value())
                           {
                               WEBSERVER_LOG_ERROR("Failed to extract profile data: {}", conf_succsess.error());
                               throw std::runtime_error("Failed to extract profile data: " + conf_succsess.error());
                           }
                       });
}

void ConfigResourceMedialib::reset_config()
{
    auto result = switch_profile(m_default_profile_name);
    if (!result.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to switch profile: {}", result.error());
        throw std::runtime_error("Failed to switch profile: " + result.error());
    }

    on_resource_change(EventType::CHANGE_DETECTION, std::make_shared<DetectionState>(DetectionState(true)));
    auto conf_succsess = extract_profile_data(m_default_profile_name);
    if (!conf_succsess.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to extract profile data: {}", conf_succsess.error());
        throw std::runtime_error("Failed to extract profile data: " + conf_succsess.error());
    }
}

tl::expected<void, std::string> ConfigResourceMedialib::extract_profile_data(const std::string &profile_name)
{
    m_current_profile_name = profile_name;
    auto profile_json = get_profile(profile_name);
    if (!profile_json.has_value())
    {
        return tl::make_unexpected("Failed to load default profile: " + profile_json.error());
    }

    auto profile_with_gyro = enable_gyro_if_exist(profile_json.value());
    if (!profile_with_gyro.has_value())
    {
        return tl::make_unexpected("Failed to enable gyro: " + profile_with_gyro.error());
    }
    m_profile = profile_with_gyro.value();

    // Load frontend config
    auto frontend_default_config = extract_frontend_config();
    if (!frontend_default_config.has_value())
    {
        return tl::make_unexpected("Failed to load default frontend config: " + frontend_default_config.error());
    }
    m_frontend_default_config = frontend_default_config.value();

    // Load encoder and osd config
    auto encoder_default_config = extract_encoder_config();
    if (!encoder_default_config.has_value())
    {
        return tl::make_unexpected("Failed to load default encoder config: " + encoder_default_config.error());
    }
    m_encoder_osd_default_config = encoder_default_config.value();
    return {};
}

tl::expected<nlohmann::json, std::string> ConfigResourceMedialib::extract_encoder_config()
{
    nlohmann::json encoder_config;
    try
    {
        // NOTE: waiting for encoder api from mosko
        //  encoder_config_t encoder_config = m_current_profile.m_encoders[STREAM_4K];
        auto encoder_config_struct = m_current_profile.to_encoded_output_stream_config_map()[STREAM_4K];
        // updated from the real struct)
        ConfigManager config_manager = ConfigManager(CONFIG_SCHEMA_ENCODER_AND_BLENDING);
        std::string encoder_config_str =
            config_manager.config_struct_to_string<config_encoded_output_stream_t>(encoder_config_struct);
        encoder_config = nlohmann::json::parse(encoder_config_str);
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_ERROR("Failed to extract encoder config: {}", e.what());
        return tl::make_unexpected(std::string(e.what()));
    }
    return encoder_config;
}

tl::expected<nlohmann::json, std::string> ConfigResourceMedialib::extract_frontend_config()
{
    nlohmann::json frontend_config;
    try
    {
        auto frontend_config_struct = m_current_profile.to_frontend_config();
        ConfigManager config_manager = ConfigManager(CONFIG_SCHEMA_FRONTEND);
        std::string frontend_config_str =
            config_manager.config_struct_to_string<frontend_config_t>(frontend_config_struct);
        frontend_config = nlohmann::json::parse(frontend_config_str);
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_ERROR("Failed to extract frontend config: {}", e.what());
        return tl::make_unexpected(std::string(e.what()));
    }
    return frontend_config;
}

tl::expected<nlohmann::json, std::string> ConfigResourceMedialib::get_profile(const nlohmann::json &profile_name)
{
    for (auto profile : m_medialib_config["profiles"])
    {
        if (profile["name"] == profile_name)
        {
            return load_config_from_file(profile["config_file"]);
        }
    }
    return tl::make_unexpected("Profile not found");
}

tl::expected<nlohmann::json, std::string> ConfigResourceMedialib::enable_gyro_if_exist(nlohmann::json profile)
{
    try
    {

        // Check if stabilizer_settings exists in the profile
        if (!profile.contains("stabilizer_settings"))
        {
            WEBSERVER_LOG_INFO("Stabilizer settings not found in profile, skipping gyro initialization");
            return profile;
        }

        nlohmann::json stabilizer_settings;

        // Check if stabilizer_settings is a file path (new format) or inline object (old format)
        if (profile["stabilizer_settings"].is_string())
        {
            // New format: stabilizer_settings contains a file path
            std::string stabilizer_config_path = profile["stabilizer_settings"];
            auto loaded_config = load_config_from_file(stabilizer_config_path);
            if (!loaded_config.has_value())
            {
                WEBSERVER_LOG_ERROR("Failed to load stabilizer settings from file: {}", loaded_config.error());
                return tl::make_unexpected("Failed to load stabilizer settings: " + loaded_config.error());
            }
            stabilizer_settings = loaded_config.value();
        }
        else
        {
            // Old format: stabilizer_settings contains inline config
            stabilizer_settings = profile["stabilizer_settings"];
        }

        // Check if gyro configuration exists in the loaded stabilizer settings
        if (!stabilizer_settings.contains("gyro") || !stabilizer_settings["gyro"].contains("sensor_name") ||
            !stabilizer_settings["gyro"].contains("sensor_frequency") || !stabilizer_settings["gyro"].contains("scale"))
        {
            WEBSERVER_LOG_INFO("Gyro settings not found in stabilizer settings, skipping gyro initialization");
            return profile;
        }

        auto sensor_name = stabilizer_settings["gyro"]["sensor_name"];
        auto sensor_frequency = stabilizer_settings["gyro"]["sensor_frequency"];
        auto gyro_scale = stabilizer_settings["gyro"]["scale"];
        auto gyro_dev = std::make_unique<GyroDevice>(sensor_name, sensor_frequency, gyro_scale);
        if (gyro_dev->exists() == GYRO_STATUS_SUCCESS)
        {
            // For new format, we need to update the file and reload it
            if (profile["stabilizer_settings"].is_string())
            {
                stabilizer_settings["gyro"]["enabled"] = true;
                // Note: In a production system, you might want to save this back to the file
                // For now, we'll update the in-memory copy
                gyro_exist = true;
            }
            else
            {
                // Old format: update inline
                profile["stabilizer_settings"]["gyro"]["enabled"] = true;
                gyro_exist = true;
            }
        }
        gyro_dev = nullptr;
        return profile;
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_ERROR("Failed to enable gyro: {}", e.what());
        return tl::make_unexpected(std::string(e.what()));
    }
}

tl::expected<void, std::string> ConfigResourceMedialib::switch_profile(const std::string &profile_name)
{
    auto result = extract_profile_data(profile_name);
    if (!result.has_value())
    {
        return tl::make_unexpected("Failed to switch profile: " + result.error());
    }
    on_resource_change(EventType::SWITCH_PROFILE, std::make_shared<ProfileNameState>(ProfileNameState(profile_name)));
    return {};
}

tl::expected<nlohmann::json, std::string> ConfigResourceMedialib::load_config_from_file(const std::string &file_path)
{
    std::ifstream configFile(file_path);
    if (!configFile.is_open())
    {
        std::string error_msg = "Failed to open config file: " + file_path;
        WEBSERVER_LOG_ERROR("{}", error_msg);
        return tl::make_unexpected(error_msg);
    }

    nlohmann::json configJson;
    try
    {
        configFile >> configJson;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        std::string error_msg = "JSON parse error in file " + file_path + ": " + e.what();
        WEBSERVER_LOG_ERROR("{}", error_msg);
        configFile.close();
        return tl::make_unexpected(error_msg);
    }
    catch (const std::exception &e)
    {
        std::string error_msg = "Error reading config file " + file_path + ": " + e.what();
        WEBSERVER_LOG_ERROR("{}", error_msg);
        configFile.close();
        return tl::make_unexpected(error_msg);
    }

    configFile.close();
    return configJson;
}

void ConfigResourceMedialib::update_profile()
{
    WEBSERVER_LOG_INFO("Updating profile");
    on_resource_change(EventType::PROFILE_UPDATE_REQUEST, std::make_shared<EmptyState>());
}

void ConfigResourceMedialib::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Post("/reset_all", [this](const nlohmann::json &req) {
        WEBSERVER_LOG_INFO("POST /reset_all called");
        try
        {
            on_resource_change(EventType::RESET_CONFIG, std::make_shared<EmptyState>());
        }
        catch (const std::exception &e)
        {
            WEBSERVER_LOG_ERROR("Failed to reset all: {}", e.what());
        }
        WEBSERVER_LOG_INFO("POST /reset_all completed");
    });

    srv->Get("/medialib_config", std::function<nlohmann::json()>([this]() {
                 WEBSERVER_LOG_INFO("GET /medialib_config called");
                 nlohmann::json j;
                 j["medialib_config"] = m_medialib_config;
                 return j;
                 WEBSERVER_LOG_INFO("GET /medialib_config completed");
             }));

    srv->Put("/switch_profile", [this](const nlohmann::json &j_body) {
        //{ "profile_name": "profile_name" }
        WEBSERVER_LOG_INFO("PUT /switch_profile called");
        if (!j_body.contains("profile_name"))
        {
            WEBSERVER_LOG_ERROR("Profile name not found in request body");
            throw std::runtime_error("Profile name not found in request body");
        }
        auto profile_name = j_body["profile_name"].get<std::string>();
        auto result = switch_profile(profile_name);
        m_current_profile_name = profile_name;
        if (!result.has_value())
        {
            WEBSERVER_LOG_ERROR("Failed to switch profile: {}", result.error());
            throw std::runtime_error("Failed to switch profile: " + result.error());
        }
        WEBSERVER_LOG_INFO("PUT /switch_profile completed");
        return nlohmann::json();
    });

    srv->Put("/framerate", [this](const nlohmann::json &j_body) {
        //{ "framerate": 30 }
        WEBSERVER_LOG_INFO("PUT /framerate called");
        if (!j_body.contains("framerate"))
        {
            WEBSERVER_LOG_ERROR("Framerate not found in request body");
            throw std::runtime_error("Framerate not found in request body");
        }
        auto framerate = j_body["framerate"].get<int>();
        on_resource_change(EventType::CHANGE_FRAMERATE, std::make_shared<ProfileFPSState>(ProfileFPSState(framerate)));
        WEBSERVER_LOG_INFO("PUT /framerate completed");
        return nlohmann::json();
    });

    // add endpoint for change resolution and then make the pipeline send the change event
    srv->Put("/resolution", [this](const nlohmann::json &j_body) {
        //{ "resolution": 4K/FHD/HD }
        WEBSERVER_LOG_INFO("PUT /resolution called");
        if (!j_body.contains("resolution"))
        {
            WEBSERVER_LOG_ERROR("Resolution not found in request body");
            throw std::runtime_error("Resolution not found in request body");
        }
        auto resolution = j_body["resolution"].get<std::string>();
        on_resource_change(EventType::CHANGE_RESOLUTION,
                           std::make_shared<ProfileResolutionState>(ProfileResolutionState(resolution)));
        WEBSERVER_LOG_INFO("PUT /resolution completed");
        return nlohmann::json();
    });
    srv->Put("/flip", [this](const nlohmann::json &j_body) {
        //{ "flip": "FLIP_DIRECTION_NONE/FLIP_DIRECTION_HORIZONTAL/FLIP_DIRECTION_VERTICAL/FLIP_DIRECTION_BOTH" }
        WEBSERVER_LOG_INFO("PUT /flip called");
        if (!j_body.contains("flip"))
        {
            WEBSERVER_LOG_ERROR("Flip not found in request body");
            throw std::runtime_error("Flip not found in request body");
        }
        auto flip = j_body["flip"].get<std::string>();
        on_resource_change(EventType::CHANGE_FLIP, std::make_shared<ProfileFlipState>(ProfileFlipState(flip)));
        WEBSERVER_LOG_INFO("PUT /flip completed");
        return nlohmann::json();
    });

    srv->Put("/rotation", [this](const nlohmann::json &j_body) {
        //{ "rotation": "ROTATION_ANGLE_0/ROTATION_ANGLE_90/ROTATION_ANGLE_180/ROTATION_ANGLE_270" }
        WEBSERVER_LOG_INFO("PUT /rotation called");
        if (!j_body.contains("rotation"))
        {
            WEBSERVER_LOG_ERROR("Rotation not found in request body");
            throw std::runtime_error("Rotation not found in request body");
        }
        auto rotation = j_body["rotation"].get<std::string>();
        on_resource_change(EventType::CHANGE_ROTATION,
                           std::make_shared<ProfileRotationState>(ProfileRotationState(rotation)));
        WEBSERVER_LOG_INFO("PUT /rotation completed");
        return nlohmann::json();
    });

    srv->Put("/dewarp", [this](const nlohmann::json &j_body) {
        //{ "dewarp": "true/false" }
        WEBSERVER_LOG_INFO("PUT /dewarp called");
        if (!j_body.contains("dewarp"))
        {
            WEBSERVER_LOG_ERROR("Dewarp not found in request body");
            throw std::runtime_error("Dewarp not found in request body");
        }
        auto dewarp = j_body["dewarp"].get<bool>();
        on_resource_change(EventType::CHANGE_DEWARP, std::make_shared<ProfileDewarpState>(ProfileDewarpState(dewarp)));
        WEBSERVER_LOG_INFO("PUT /dewarp completed");
        return nlohmann::json();
    });

    srv->Put("/freeze", [this](const nlohmann::json &j_body) {
        //{ "freeze": "true/false" }
        WEBSERVER_LOG_INFO("PUT /freeze called");
        if (!j_body.contains("freeze"))
        {
            WEBSERVER_LOG_ERROR("Freeze not found in request body");
            throw std::runtime_error("Freeze not found in request body");
        }
        auto freeze = j_body["freeze"].get<bool>();
        on_resource_change(EventType::CHANGE_FREEZE, std::make_shared<ProfileFreezeState>(ProfileFreezeState(freeze)));
        WEBSERVER_LOG_INFO("PUT /freeze completed");
        return nlohmann::json();
    });

    srv->Put("/digital_image_stabilization", [this](const nlohmann::json &j_body) {
        WEBSERVER_LOG_INFO("PUT /digital_image_stabilization called");
        if (!j_body.contains("digital_image_stabilization"))
        {
            WEBSERVER_LOG_ERROR("Digital Image stabilization not found in request body");
            throw std::runtime_error("Digital Image stabilization not found in request body");
        }
        auto image_stabilization = j_body["digital_image_stabilization"]["active"].get<bool>();
        on_resource_change(EventType::CHANGE_DIS,
                           std::make_shared<ProfileDisState>(ProfileDisState(image_stabilization)));
        WEBSERVER_LOG_INFO("PUT /digital_image_stabilization completed");
        return nlohmann::json();
    });

    srv->Put("/electronic_image_stabilization", [this](const nlohmann::json &j_body) {
        WEBSERVER_LOG_INFO("PUT /electronic_image_stabilization called");
        if (!j_body.contains("electronic_image_stabilization"))
        {
            WEBSERVER_LOG_ERROR("Image stabilization not found in request body");
            throw std::runtime_error("Image stabilization not found in request body");
        }
        if (!gyro_exist)
        {
            WEBSERVER_LOG_ERROR("Gyro not exist, cannot set electronic image stabilization");
            throw std::runtime_error("Gyro not exist, cannot set electronic image stabilization");
        }
        auto image_stabilization = j_body["electronic_image_stabilization"]["active"].get<bool>();
        on_resource_change(EventType::CHANGE_EIS,
                           std::make_shared<ProfileEisState>(ProfileEisState(image_stabilization)));

        WEBSERVER_LOG_INFO("PUT /electronic_image_stabilization completed");
        return nlohmann::json();
    });

    srv->Put("/digital_zoom", [this](const nlohmann::json &j_body) {
        //{"mode":"DIGITAL_ZOOM_MODE_MAGNIFICATION", "magnification":1, "x":0,"y":0,"width":100,"height":100}
        WEBSERVER_LOG_INFO("PUT /digital_zoom_roi called");
        if (!j_body.contains("digital_zoom"))
        {
            WEBSERVER_LOG_ERROR("Digital zoom mode not found in request body");
            throw std::runtime_error("Digital zoom mode not found in request body");
        }
        auto mode = string_to_digital_zoom(j_body["digital_zoom"]["mode"].get<std::string>());
        if (j_body["digital_zoom"].contains("mode") && mode == digital_zoom_mode_t::DIGITAL_ZOOM_MODE_MAGNIFICATION &&
            !j_body["digital_zoom"].contains("magnification"))
        {
            WEBSERVER_LOG_ERROR("Digital zoom not found in request body");
            throw std::runtime_error("Digital zoom not found in request body");
        }
        else if (j_body["digital_zoom"].contains("mode") && mode == digital_zoom_mode_t::DIGITAL_ZOOM_MODE_ROI &&
                 (!j_body["digital_zoom"]["digital_zoom_roi"].contains("x") ||
                  !j_body["digital_zoom"]["digital_zoom_roi"].contains("y") ||
                  !j_body["digital_zoom"]["digital_zoom_roi"].contains("width") ||
                  !j_body["digital_zoom"]["digital_zoom_roi"].contains("height")))
        {
            WEBSERVER_LOG_ERROR("Digital zoom roi not found in request body");
            throw std::runtime_error("Digital zoom roi not found in request body");
        }
        auto magnification = j_body["digital_zoom"]["magnification"].get<int>();
        auto enable = j_body["digital_zoom"]["enabled"].get<bool>();
        if (mode == digital_zoom_mode_t::DIGITAL_ZOOM_MODE_MAGNIFICATION)
        {
            on_resource_change(EventType::CHANGE_DIGITAL_ZOOM, std::make_shared<ProfileDigitalZoomState>(
                                                                   ProfileDigitalZoomState(enable, magnification)));
            return nlohmann::json();
        }
        auto x = j_body["digital_zoom"]["digital_zoom_roi"]["x"].get<double>();
        auto y = j_body["digital_zoom"]["digital_zoom_roi"]["y"].get<double>();
        auto width = j_body["digital_zoom"]["digital_zoom_roi"]["width"].get<double>();
        auto height = j_body["digital_zoom"]["digital_zoom_roi"]["height"].get<double>();
        on_resource_change(EventType::CHANGE_DIGITAL_ZOOM_ROI,
                           std::make_shared<ProfileDigitalZoomRoiState>(
                               ProfileDigitalZoomRoiState(enable, magnification, x, y, width, height)));
        WEBSERVER_LOG_INFO("PUT /digital_zoom_roi completed");
        return nlohmann::json();
    });

    srv->Put("/grayscale", [this](const nlohmann::json &j_body) {
        //{ "grayscale": "true/false" }
        WEBSERVER_LOG_INFO("PUT /grayscale called");
        if (!j_body.contains("grayscale"))
        {
            WEBSERVER_LOG_ERROR("Grayscale not found in request body");
            throw std::runtime_error("Grayscale not found in request body");
        }
        auto grayscale = j_body["grayscale"].get<bool>();
        on_resource_change(EventType::CHANGE_GRAYSCALE,
                           std::make_shared<ProfileGrayscaleState>(ProfileGrayscaleState(grayscale)));
        WEBSERVER_LOG_INFO("PUT /grayscale completed");
        return nlohmann::json();
    });

    srv->Put("/detection", [this](const nlohmann::json &j_body) {
        //{ "detection": "true/false" }
        WEBSERVER_LOG_INFO("PUT /detection called");
        if (!j_body.contains("detection"))
        {
            WEBSERVER_LOG_ERROR("Detection not found in request body");
            throw std::runtime_error("Detection not found in request body");
        }
        auto detection = j_body["detection"].get<bool>();
        on_resource_change(EventType::CHANGE_DETECTION, std::make_shared<DetectionState>(DetectionState(detection)));
        WEBSERVER_LOG_INFO("PUT /detection completed");
        return nlohmann::json();
    });

    srv->Get("/digital_image_stabilization", std::function<nlohmann::json()>([this]() {
                 WEBSERVER_LOG_INFO("GET /image_stabilization called");
                 update_profile();
                 nlohmann::json j;
                 j["digital_image_stabilization"]["active"] = m_current_profile.stabilizer_settings.dis.enabled;
                 WEBSERVER_LOG_INFO("GET /digital_image_stabilization completed");
                 return j;
             }));

    srv->Get("/electronic_image_stabilization", std::function<nlohmann::json()>([this]() {
                 WEBSERVER_LOG_INFO("GET /image_stabilization called");
                 update_profile();
                 nlohmann::json j;
                 j["electronic_image_stabilization"]["gyro_exist"] = gyro_exist;
                 j["electronic_image_stabilization"]["active"] =
                     gyro_exist && m_current_profile.stabilizer_settings.eis.enabled;
                 WEBSERVER_LOG_INFO("GET /electronic_image_stabilization completed");
                 return j;
             }));

    srv->Get("/architecture", std::function<nlohmann::json()>([this]() {
                 WEBSERVER_LOG_INFO("GET /architecture called");
                 nlohmann::json j;
                 j["architecture"] = get_hailo_architecture();
                 WEBSERVER_LOG_INFO("GET /architecture completed");
                 return j;
             }));
}
