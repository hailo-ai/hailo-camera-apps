#pragma once
#include "resources/common/resources.hpp"
#include "resources/common/repository.hpp"
#include "media_library/encoder_config.hpp"

namespace webserver
{
    namespace pipeline
    {
        class IPipeline
        {
        protected:
            WebserverResourceRepository m_resources;
            virtual void subscribe_to_events();
            virtual void callback_handle_frontend(ResourceStateChangeNotification notif) = 0;
            virtual void callback_handle_stream_config(ResourceStateChangeNotification notif) = 0;
            virtual void callback_handle_encoder(ResourceStateChangeNotification notif) = 0;
            virtual void callback_handle_ai(ResourceStateChangeNotification notif) = 0;
            virtual void callback_handle_isp(ResourceStateChangeNotification notif) = 0;
            virtual void callback_handle_restart_frontend(ResourceStateChangeNotification notif) = 0;
            virtual void callback_handle_simple_restart(ResourceStateChangeNotification notif) = 0;
            void callback_handle_osd(ResourceStateChangeNotification notif);
            void callback_handle_privacy_mask(ResourceStateChangeNotification notif);
            virtual std::shared_ptr<osd::Blender> get_osd_blender() = 0;
            virtual std::shared_ptr<PrivacyMaskBlender> get_privacy_blender() = 0;
            virtual hailo_encoder_config_t get_encoder_config() = 0;

        public:
            IPipeline(WebserverResourceRepository resources);
            virtual ~IPipeline() = default;
            virtual void start() = 0;
            virtual void stop() = 0;
            virtual WebserverResourceRepository get_resources() { return m_resources; };
        };

    }
}

typedef std::shared_ptr<webserver::pipeline::IPipeline> WebServerPipeline;
