#pragma once
#include "common/resources.hpp"

namespace webserver
{
    namespace resources
    {
    class ConfigResource : public Resource
            {
            private:
                nlohmann::json m_frontend_default_config;
                nlohmann::json m_encoder_osd_default_config;

            public:
                ConfigResource(std::shared_ptr<EventBus> event_bus);
                ~ConfigResource() = default;
                std::string name() override { return "config"; }
                ResourceType get_type() override { return ResourceType::RESOURCE_CONFIG_MANAGER; }
                void http_register(std::shared_ptr<HTTPServer> srv) override;
                nlohmann::json get_frontend_default_config();
                nlohmann::json get_encoder_default_config();
                nlohmann::json get_osd_default_config();
                nlohmann::json get_osd_and_encoder_default_config();
                nlohmann::json get_hdr_default_config();
                nlohmann::json get_denoise_default_config();
            };
    }
}