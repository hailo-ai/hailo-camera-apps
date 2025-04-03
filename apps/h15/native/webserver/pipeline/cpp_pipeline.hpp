#pragma once
#include "base_pipeline.hpp"
// medialibrary includes
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/media_library.hpp"
#include "media_library/config_manager.hpp"
#include "media_library/media_library_types.hpp"

// infra includes
#include "pipeline.hpp"
#include "encoder_stage.hpp"
#include "frontend_stage.hpp"
#include "udp_stage.hpp"
#include "stages/webrtc_stage.hpp"
#include "stages/pipeline_builder.hpp"

#define STREAM_4K "sink0"

namespace webserver
{
    namespace pipeline
    {
        class CppPipeline : public IPipeline
        {
        public:
            CppPipeline(WebserverResourceRepository resources);
            static std::shared_ptr<CppPipeline> create(std::shared_ptr<HTTPServer> svr, std::string config_path);
            void start() override;
            void stop() override;

        private:
            struct AppResources
                {
                    std::shared_ptr<MediaLibrary> media_library;
                    std::shared_ptr<FrontendStage> frontend;
                    std::map<output_stream_id_t, std::shared_ptr<EncoderStage>> encoders;
                    PipelinePtr pipeline;

                    void clear()
                    {
                        frontend = nullptr;
                        encoders.clear();
                        pipeline = nullptr;
                    }

                    ~AppResources()
                    {
                        clear();
                    }
                };
            std::shared_ptr<AppResources> m_app_resources;

            std::string get_frontend_config();
            std::string get_encoder_and_osd_config(const std::string &encoder_name);
            std::shared_ptr<FrontendStage> configure_frontend();
            std::shared_ptr<EncoderStage> configure_encoder_and_osd(const std::string& stream_name);
            std::shared_ptr<UdpStage> configure_udp(const std::string& stream_name);
            std::shared_ptr<WebrtcStage> configure_webrtc_callback();
            std::string read_string_from_file(const char *file_path);
            void update_profile_config_frontend(const std::string &frontend_conf, ProfileConfig &profile_config);

            void callback_handle_profile_switch(ResourceStateChangeNotification notif);

            hailo_encoder_config_t get_encoder_config() override;
            std::shared_ptr<osd::Blender> get_osd_blender() override;
            std::shared_ptr<PrivacyMaskBlender> get_privacy_blender() override;
            void callback_handle_frontend(ResourceStateChangeNotification notif) override;
            void callback_handle_encoder(ResourceStateChangeNotification notif) override;
            void callback_handle_stream_config(ResourceStateChangeNotification notif) override;
            void callback_handle_ai(ResourceStateChangeNotification notif) override {}
            void callback_handle_isp(ResourceStateChangeNotification notif) override {}
            void callback_handle_restart_frontend(ResourceStateChangeNotification notif) override {}
            void callback_handle_simple_restart(ResourceStateChangeNotification notif) override {}
        };
    }
}