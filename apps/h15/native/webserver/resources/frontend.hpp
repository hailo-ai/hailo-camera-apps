#pragma once
#include "common/resources.hpp"
#include "isp.hpp"
#include "ai.hpp"
#include "configs.hpp"

namespace webserver
{
    namespace resources
    {
        class FrontendResource : public Resource
        {
        public:
            struct Resolution
            {
                uint32_t width;
                uint32_t height;
                uint32_t framerate;
            };  
            struct frontend_config_t
            {
                bool freeze;
                bool freeze_state_changed;
                bool flip_enabled;
                bool rotate_enabled;
                std::string flip;
                std::string rotate;
                bool dewarp_enabled;
                std::vector<Resolution> resolutions;
            };

            class FrontendResourceState : public ResourceState
            {
            public:
                std::string config;
                frontend_config_t control;
                FrontendResourceState(std::string config, frontend_config_t control) : config(config), control(control) {}
            };

            FrontendResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<webserver::resources::AiResource> ai_res, std::shared_ptr<webserver::resources::IspResource> isp_res, std::shared_ptr<webserver::resources::ConfigResourceBase> configs);
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "frontend"; }
            ResourceType get_type() override { return ResourceType::RESOURCE_FRONTEND; }
            nlohmann::json get_frontend_config();
            void update_json_config();

        private:
            void reset_config() override;
            void update_frontend_config();
            std::shared_ptr<webserver::resources::AiResource> m_ai_resource;
            std::shared_ptr<webserver::resources::IspResource> m_isp_resource;
            frontend_config_t m_frontend_config;
        };
    }
}