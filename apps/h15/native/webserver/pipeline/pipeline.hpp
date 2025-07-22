#pragma once
#include "base_pipeline.hpp"
// medialibrary includes
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/media_library.hpp"
#include "media_library/config_manager.hpp"
#include "media_library/media_library_api_types.hpp"

// infra includes
#include "pipeline.hpp"
#include "encoder_stage.hpp"
#include "frontend_stage.hpp"
#include "udp_stage.hpp"
#include "pipeline_builder.hpp"
#include "postprocess_stage.hpp"
#include "ai_stage.hpp"
#include "aggregator_stage.hpp"
#include "overlay_stage.hpp"
#include "valve_stage.hpp"
#include "freeze_stage.hpp"
#include "stages/webrtc_stage.hpp"

#define STREAM_4K "sink0"
#define STREAM_640_640 "sink1"

namespace webserver
{
namespace pipeline
{
class CppPipeline : public IPipeline
{
  public:
    CppPipeline(WebserverResourceRepository resources, Architecture platform = Architecture::Hailo15H);
    static std::shared_ptr<CppPipeline> create(std::shared_ptr<HTTPServer> svr, std::string config_path,
                                               Architecture platform = Architecture::Hailo15H);
    void start() override;
    void stop() override;

  private:
    struct AppResources
    {
        std::shared_ptr<MediaLibrary> media_library;
        std::shared_ptr<FrontendStage> frontend;
        std::shared_ptr<ValveStage> valve_stage;
        std::shared_ptr<FreezeStage> freeze_stage;
        std::shared_ptr<OverlayStage> overlay_stage;
        std::map<output_stream_id_t, std::shared_ptr<EncoderStage>> encoders;
        PipelinePtr pipeline;
        Architecture platform;

        void clear()
        {
            frontend = nullptr;
            encoders.clear();
            pipeline = nullptr;
            valve_stage = nullptr;
            freeze_stage = nullptr;
            overlay_stage = nullptr;
            media_library = nullptr;
        }

        ~AppResources()
        {
            clear();
        }
    };
    std::shared_ptr<AppResources> m_app_resources;
    bool m_rotate_done_in_dewarp;
    std::string get_frontend_config();
    std::string get_encoder_and_osd_config(const std::string &encoder_name);
    std::shared_ptr<FrontendStage> configure_frontend();
    std::shared_ptr<EncoderStage> configure_encoder_and_osd(const std::string &stream_name);
    std::shared_ptr<UdpStage> configure_udp(const std::string &stream_name);
    std::shared_ptr<WebrtcStage> configure_webrtc_callback();
    std::string read_string_from_file(const char *file_path);
    void update_profile_config_frontend(const std::string &frontend_conf, ProfileConfig &profile_config);

    void callback_handle_profile_switch(ResourceStateChangeNotification notif);

    hailo_encoder_config_t get_encoder_config() override;
    std::shared_ptr<osd::Blender> get_osd_blender() override;
    std::shared_ptr<PrivacyMaskBlender> get_privacy_blender() override;
    void callback_handle_encoder(ResourceStateChangeNotification notif) override;
    void callback_handle_update_profile(ResourceStateChangeNotification notif);
    void update_fps(uint32_t fps, ProfileConfig &profile_config);
    void update_resolution(const std::string &resolution, ProfileConfig &profile_config);
    void update_flip(const std::string &flip, ProfileConfig &profile_config);
    void update_rotation(const std::string &rotation, ProfileConfig &profile_config);
    void update_zoom(std::shared_ptr<ProfileDigitalZoomState> state, ProfileConfig &profile_config);
    void update_zoom_roi(std::shared_ptr<ProfileDigitalZoomRoiState> state, ProfileConfig &profile_config);
    int relative_to_absolut(float position, uint32_t resolution_axis_size);
    float absolut_to_relative(int position, uint32_t resolution_axis_size);
    int scale(int position, int old_size, int new_size);
    void subscribe_callbacks();
    void register_endpoints();
    void build_pipeline();
};
} // namespace pipeline
} // namespace webserver
