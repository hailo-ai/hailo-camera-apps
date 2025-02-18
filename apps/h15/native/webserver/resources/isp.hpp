#pragma once
#include "common/resources.hpp"
#include "ai.hpp"
#include "osd_res.hpp"
#include "configs.hpp"
#include "media_library/v4l2_ctrl.hpp"
#include "common/isp/common.hpp"

using namespace webserver::common;

namespace webserver
{
    namespace resources
    {
        class IspResource : public Resource
        {
        private:
            std::mutex m_mutex;
            std::unique_ptr<isp_utils::ctrl::v4l2Control> m_v4l2;
            std::shared_ptr<AiResource> m_ai_resource;
            stream_isp_params_t m_baseline_stream_params;
            int16_t m_baseline_wdr_params;
            backlight_filter_t m_baseline_backlight_params;
            nlohmann::json m_hdr_config;
            auto_exposure_t get_auto_exposure();
            nlohmann::json set_auto_exposure(const nlohmann::json &req);
            bool set_auto_exposure(auto_exposure_t &ae);
            void on_ai_state_change(std::shared_ptr<AiResource::AiResourceState> state);
            void set_tuning_profile(webserver::common::tuning_profile_t);
            void reset_config() override;

        public:
            class IspResourceState : public ResourceState
            {
            public:
                bool isp_3aconfig_updated;
                IspResourceState(bool isp_3aconfig_updated) : isp_3aconfig_updated(isp_3aconfig_updated) {}
            };

            nlohmann::json get_hdr_config() { return m_hdr_config; }
            IspResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<AiResource> ai_res, std::shared_ptr<webserver::resources::ConfigResource> configs);
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "isp"; }
            ResourceType get_type() override { return ResourceType::RESOURCE_ISP; }
            void init(bool set_auto_wb = true);
        };
    }
}