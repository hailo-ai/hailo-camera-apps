#pragma once
#include "resources/common/resources.hpp"
#include "resources/common/repository.hpp"
#include "media_library/encoder_config.hpp"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include "rtc/rtc.hpp"

namespace webserver
{
    namespace pipeline
    {
        class IPipeline
        {
        protected:
            WebserverResourceRepository m_resources;
            GstElement *m_pipeline;
            virtual std::string create_gst_pipeline_string() = 0;
            void set_gst_callbacks();
            static void new_sample(GstAppSink *appsink, gpointer user_data);
            static void fps_measurement(GstElement *fpssink, gdouble fps, gdouble droprate, gdouble avgfps, gpointer user_data);
            hailo_encoder_config_t get_encoder_config();

        public:
            IPipeline(WebserverResourceRepository resources);
            virtual ~IPipeline() = default;
            virtual void start();
            virtual void stop();
            virtual WebserverResourceRepository get_resources() { return m_resources; };

            static std::shared_ptr<IPipeline> create(std::shared_ptr<HTTPServer> svr);
        };

        class Pipeline : public IPipeline
        {
        public:
            Pipeline(WebserverResourceRepository resources);
            static std::shared_ptr<Pipeline> create(std::shared_ptr<HTTPServer> svr);

        protected:
            std::string create_gst_pipeline_string() override;

        private:
            void callback_handle_frontend(ResourceStateChangeNotification notif);
            void callback_handle_stream_config(ResourceStateChangeNotification notif);
            void callback_handle_osd(ResourceStateChangeNotification notif);
            void callback_handle_encoder(ResourceStateChangeNotification notif);
            void callback_handle_ai(ResourceStateChangeNotification notif);
            void callback_handle_privacy_mask(ResourceStateChangeNotification notif);
            void callback_handle_isp(ResourceStateChangeNotification notif);
            void callback_handle_restart_frontend(ResourceStateChangeNotification notif);
            void callback_handle_simple_restart(ResourceStateChangeNotification notif);
        };

    }
}

typedef std::shared_ptr<webserver::pipeline::IPipeline> WebServerPipeline;
