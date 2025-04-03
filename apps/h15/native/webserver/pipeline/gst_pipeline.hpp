#pragma once
#include "base_pipeline.hpp"
#include "media_library/encoder_config.hpp"
#include "resources/common/resources.hpp"
#include <gst/app/gstappsink.h>
#include <gst/gst.h>

namespace webserver
{
    namespace pipeline
    {
        class GStreamerPipeline : public IPipeline
        {
        public:
            void start() override;
            void stop() override;
            GStreamerPipeline(WebserverResourceRepository resources);
            static std::shared_ptr<GStreamerPipeline> create(std::shared_ptr<HTTPServer> svr);

        private:
            GstElement *m_pipeline;
            void set_gst_callbacks();
            static void new_sample(GstAppSink *appsink, gpointer user_data);
            hailo_encoder_config_t get_encoder_config() override;
            std::string create_gst_pipeline_string();
            void callback_handle_frontend(ResourceStateChangeNotification notif) override;
            void callback_handle_stream_config(ResourceStateChangeNotification notif) override;
            void callback_handle_encoder(ResourceStateChangeNotification notif) override;
            void callback_handle_ai(ResourceStateChangeNotification notif) override;
            void callback_handle_isp(ResourceStateChangeNotification notif) override;
            void callback_handle_restart_frontend(ResourceStateChangeNotification notif) override;
            void callback_handle_simple_restart(ResourceStateChangeNotification notif) override;
            std::shared_ptr<osd::Blender> get_osd_blender() override;
            std::shared_ptr<PrivacyMaskBlender> get_privacy_blender() override;
        };

    }
}