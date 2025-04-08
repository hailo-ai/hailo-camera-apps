#pragma once
#include "common/resources.hpp"
#include "configs.hpp"

namespace webserver
{
    namespace resources
    {
        class AiResource : public Resource
        {
        public:
            enum AiApplications
            {
                AI_APPLICATION_DETECTION,
                AI_APPLICATION_DENOISE,
            };

            class AiResourceState : public ResourceState
            {
            public:
                std::vector<AiApplications> enabled;
                std::vector<AiApplications> disabled;
            };

            AiResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<webserver::resources::ConfigResourceBase> configs);
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "ai"; }
            ResourceType get_type() override { return ResourceType::RESOURCE_AI; }
            nlohmann::json get_ai_config(AiApplications app);
            std::vector<AiApplications> get_enabled_applications();
            void set_detection_enabled(bool enabled);

        private:
            void reset_config() override;
            void http_patch(nlohmann::json body);
            std::shared_ptr<AiResourceState> parse_state(std::vector<AiApplications> current_enabled, std::vector<AiApplications> prev_enabled);
            nlohmann::json m_denoise_config;
            std::mutex m_mutex;
        };
    }
}