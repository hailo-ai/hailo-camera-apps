#pragma once
#include "common/resources.hpp"
#include "ai.hpp"
#include "osd_res.hpp"
#include "configs.hpp"
#include "common/isp/common.hpp"
#include <atomic>

namespace webserver
{
    namespace resources
    {
        class IspResource : public Resource
        {
        private:
            std::mutex m_mutex;
            std::shared_ptr<AiResource> m_ai_resource;
            common::stream_isp_params_t m_baseline_stream_params;
            int16_t m_baseline_wdr_params;
            common::backlight_filter_t m_baseline_backlight_params;
            nlohmann::json m_hdr_config;
            std::atomic<bool> m_isp_converge;
            common::auto_exposure_t get_auto_exposure();
            nlohmann::json set_auto_exposure(const nlohmann::json &req);
            bool set_auto_exposure(common::auto_exposure_t &ae);
            void on_ai_state_change(std::shared_ptr<AiResource::AiResourceState> state);
            void set_tuning_profile(webserver::common::tuning_profile_t);
            void reset_config() override;
            bool get_isp_converge();
            void wait_isp_converge(int polling_interval, int delay_after_polling);
            void wait_safe_to_pull();

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
