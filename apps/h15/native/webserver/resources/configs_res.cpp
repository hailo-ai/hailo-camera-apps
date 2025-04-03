#include "configs.hpp"

#define DEFAULT_CONFIGS_PATH "/home/root/apps/webserver/resources/configs/"
#define DEFAULT_FRONTEND_CONFIG_PATH "/home/root/apps/webserver/resources/configs/frontend_config.json"
#define DEFAULT_ENCODER_OSD_CONFIG_PATH "/home/root/apps/webserver/resources/configs/encoder_config.json"

webserver::resources::ConfigResource::ConfigResource(std::shared_ptr<EventBus> event_bus) : ConfigResourceBase(event_bus)
{
    // Load default frontend config
    std::ifstream defaultFrontendConfigFile(DEFAULT_FRONTEND_CONFIG_PATH);
    if (defaultFrontendConfigFile.is_open())
    {
        nlohmann::json defaultFrontendConfigJson;
        defaultFrontendConfigFile >> defaultFrontendConfigJson;
        m_frontend_default_config = defaultFrontendConfigJson;
        auto sensor_name = m_frontend_default_config["gyro"]["sensor_name"];
        auto sensor_frequency = m_frontend_default_config["gyro"]["sensor_frequency"];
        auto gyro_scale = m_frontend_default_config["gyro"]["scale"];
        auto gyro_dev = std::make_unique<GyroDevice>(sensor_name, sensor_frequency, gyro_scale);
        if (gyro_dev->exists() == GYRO_STATUS_SUCCESS)
        {
            m_frontend_default_config["gyro"]["enabled"] = true;
        }
        gyro_dev = nullptr;
        defaultFrontendConfigFile.close();
    }
    else
    {
        throw std::runtime_error("Failed to open default frontend config file");
    }

    // Load default encoder config
    std::ifstream defaultEncoderConfigFile(DEFAULT_ENCODER_OSD_CONFIG_PATH);
    if (defaultEncoderConfigFile.is_open())
    {
        nlohmann::json defaultEncoderConfigJson;
        defaultEncoderConfigFile >> defaultEncoderConfigJson;
        m_encoder_osd_default_config = defaultEncoderConfigJson;
        defaultEncoderConfigFile.close();
    }
    else
    {
        throw std::runtime_error("Failed to open default encoder config file");
    }
}


void webserver::resources::ConfigResource::http_register(std::shared_ptr<HTTPServer> srv)
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
                WEBSERVER_LOG_INFO("POST /reset_all completed");
                });
}
