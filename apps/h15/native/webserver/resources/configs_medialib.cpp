#include "configs.hpp"

#define DEFAULT_CONFIGS_PATH "/home/root/apps/webserver/resources/configs/"
#define APPEND_CONFIG_PATH(path) DEFAULT_CONFIGS_PATH path
#define DEFAULT_MEDIALIB_CONFIG_PATH APPEND_CONFIG_PATH("medialib_config.json")

using namespace webserver::resources;

ConfigResourceMedialib::ConfigResourceMedialib(std::shared_ptr<EventBus> event_bus, std::string config_path) : ConfigResourceBase(event_bus)
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
        throw std::runtime_error("Failed to load default medialib config: " + medialib_config.error());
    }
    m_medialib_config = medialib_config.value();

    // Load default profile config
    m_default_profile_name = m_medialib_config["default_profile"];
    auto conf_succsess = extract_profile_data(m_default_profile_name);
    if (!conf_succsess.has_value())
    {
        throw std::runtime_error("Failed to extract profile data: " + conf_succsess.error());
    }
}

void ConfigResourceMedialib::reset_config()
{
    auto result = switch_profile(m_default_profile_name);
    if (!result.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to switch profile: {}", result.error());
        throw std::runtime_error("Failed to switch profile: " + result.error());
    }
    return;

    auto conf_succsess = extract_profile_data(m_default_profile_name);
    if (!conf_succsess.has_value())
    {
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
        if (m_profile["encoded_output_streams"].size() != 1)
        {
            return tl::make_unexpected("Profile should have only one encoder in webserver");
        }
        std::string encoder_config_path = m_profile["encoded_output_streams"][0]["config_path"];
        return load_config_from_file(encoder_config_path);
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
        std::vector<std::string> config_fields = {
            "input_video", "application_input_streams", "dewarp", "dis", "eis", "gyro",
            "gmv", "optical_zoom", "isp", "hdr", "digital_zoom", "flip",
            "motion_detection", "hailort", "denoise", "rotation"};

        for (const auto &field : config_fields)
        {
            if (m_profile.contains(field))
            {
                frontend_config[field] = m_profile[field];
            }
        }
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_ERROR("Failed to extract frontend config: {}", e.what());
        return tl::make_unexpected(std::string(e.what()));
    }
    return frontend_config;
}

tl::expected<nlohmann::json, std::string> ConfigResourceMedialib::get_profile(nlohmann::json profile_name)
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
        auto sensor_name = profile["gyro"]["sensor_name"];
        auto sensor_frequency = profile["gyro"]["sensor_frequency"];
        auto gyro_scale = profile["gyro"]["scale"];
        auto gyro_dev = std::make_unique<GyroDevice>(sensor_name, sensor_frequency, gyro_scale);
        if (gyro_dev->exists() == GYRO_STATUS_SUCCESS)
        {
            profile["gyro"]["enabled"] = true;
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
    on_resource_change(EventType::SWITCH_PROFILE, std::make_shared<ConfigResourceMedialib::ProfileResourceState>(ProfileResourceState(profile_name)));
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

void ConfigResourceMedialib::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Post("/reset_all", [this](const nlohmann::json &req)
              {
                WEBSERVER_LOG_INFO("POST /reset_all called");
                try
                {
                    on_resource_change(EventType::RESET_CONFIG, std::make_shared<ResourceState>());
                }
                catch (const std::exception &e)
                {
                    WEBSERVER_LOG_ERROR("Failed to reset all: {}", e.what());
                }
                WEBSERVER_LOG_INFO("POST /reset_all completed"); });

    srv->Get("/medialib_config", std::function<nlohmann::json()>([this]()
                                                                 {
                WEBSERVER_LOG_INFO("GET /medialib_config called");
                return m_medialib_config;
                WEBSERVER_LOG_INFO("GET /medialib_config completed"); }));

    srv->Put("/switch_profile", [this](const nlohmann::json &j_body)
             {
                //{ "profile_name": "profile_name" }
                WEBSERVER_LOG_INFO("PUT /switch_profile called");
                if(!j_body.contains("profile_name"))
                {
                    WEBSERVER_LOG_ERROR("Profile name not found in request body");
                    throw std::runtime_error("Profile name not found in request body");
                }
                auto profile_name = j_body["profile_name"].get<std::string>();
                auto result = switch_profile(profile_name);
                if (!result.has_value())
                {
                    WEBSERVER_LOG_ERROR("Failed to switch profile: {}", result.error());
                    throw std::runtime_error("Failed to switch profile: " + result.error());
                }
                WEBSERVER_LOG_INFO("PUT /switch_profile completed");
                return nlohmann::json(); });
}