#include "pipeline.hpp"
#include "common/common.hpp"

#define FRONTEND_STAGE "frontend_stage"
#define ENCODER_NAME(str) (std::string(str) + "_encoder")
#define UDP_NAME(str) (std::string(str) + "_udp")
#define HOST_IP "10.0.0.2"
#define HOST_PORT "5000"
#define TEE_STAGE "vision_tee"

// Detection AI Params
#define DETECTION_AI_STAGE "yolo_detection"

// Define platform-specific resources
// For Hailo15
#define HAILO15_YOLO_HEF_FILE "/home/root/apps/webserver/resources/yolov5m_wo_spp_60p_nv12_640.hef"
#define HAILO15_YOLO_FUNC_NAME "yolov5"
#define HAILO15_YOLO_POST_CONF "/home/root/apps/detection/resources/configs/yolov5.json"

// For Hailo15L
#define HAILO15L_YOLO_HEF_FILE "/home/root/apps/webserver/resources/yolov5s_personface_nv12.hef"
#define HAILO15L_YOLO_FUNC_NAME "yolov5s_personface"
#define HAILO15L_YOLO_POST_CONF "/home/root/apps/detection/resources/configs/yolov5_personface.json"

// Detection Postprocess Params
#define POST_STAGE "yolo_post"
#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so"
// Aggregator Params
#define AGGREGATOR_STAGE "aggregator"
#define WEBRTC_STAGE "webrtc_stage"
#define OVERLAY_STAGE "overlay"

using namespace webserver::pipeline;
using namespace webserver::resources;
CppPipeline::CppPipeline(WebserverResourceRepository resources, Architecture platform)
    : IPipeline(resources), m_app_resources(std::make_shared<AppResources>())
{
    WEBSERVER_LOG_INFO("initializing Pipeline with platform: {}", ARCHITECTURE_STRING(platform));
    m_app_resources->platform = platform;
    m_rotate_done_in_dewarp = is_env_variable_on(MEDIALIB_DEWARP_DSP_OPTIMIZATION_ENV_VAR);
    subscribe_callbacks();
    register_endpoints();
    WEBSERVER_LOG_INFO("initializing successfully");
}

void CppPipeline::build_pipeline()
{
    WEBSERVER_LOG_INFO("Building pipeline");
    auto config = std::static_pointer_cast<ConfigResourceMedialib>(m_resources->get(RESOURCE_CONFIG_MANAGER));
    std::string medialib_config_string = config->get_current_medialib_config().dump();
    m_app_resources->media_library = std::make_shared<MediaLibrary>();
    if (m_app_resources->media_library->initialize(medialib_config_string) !=
        media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return;
    }

    // Select the right HEF file and function name based on the platform
    std::string yolo_hef_file;
    std::string yolo_func_name;
    std::string yolo_post_conf;

    if (m_app_resources->platform == Architecture::Hailo15L)
    {
        yolo_hef_file = HAILO15L_YOLO_HEF_FILE;
        yolo_func_name = HAILO15L_YOLO_FUNC_NAME;
        yolo_post_conf = HAILO15L_YOLO_POST_CONF;
        WEBSERVER_LOG_INFO("Using Hailo15L resources: {}, {}", yolo_hef_file, yolo_func_name);
    }
    else
    {
        yolo_hef_file = HAILO15_YOLO_HEF_FILE;
        yolo_func_name = HAILO15_YOLO_FUNC_NAME;
        yolo_post_conf = HAILO15_YOLO_POST_CONF;
        WEBSERVER_LOG_INFO("Using Hailo15 resources: {}, {}", yolo_hef_file, yolo_func_name);
    }

    WEBSERVER_LOG_INFO("building pipeline");
    m_app_resources->valve_stage = std::make_shared<ValveStage>("valve", 1);
    m_app_resources->freeze_stage = std::make_shared<FreezeStage>("freeze", 1);
    m_app_resources->overlay_stage = std::make_shared<OverlayStage>(OVERLAY_STAGE, false);
    // Create pipeline
    m_app_resources->pipeline =
        PipelineBuilder()
            .add_stage<FrontendStage>("frontend", configure_frontend(), StageType::SOURCE)
            .add_stage<ValveStage>("valve", m_app_resources->valve_stage)
            .add_stage<FreezeStage>("freeze", m_app_resources->freeze_stage)
            .add_stage<AggregatorStage>("aggregator",
                                        std::make_shared<AggregatorStage>(AGGREGATOR_STAGE, true, 1, STREAM_4K, 3,
                                                                          false, POST_STAGE, 3, false, false, true))
            .add_stage<HailortAsyncStage>(
                "hailonet", std::make_shared<HailortAsyncStage>(DETECTION_AI_STAGE, yolo_hef_file, 5, 50, "device0", 1,
                                                                10, 1, false, std::chrono::milliseconds(50), false,
                                                                StagePoolMode::BLOCKING))
            .add_stage<PostprocessStage>("post process",
                                         std::make_shared<PostprocessStage>(POST_STAGE, YOLO_POST_SO, yolo_func_name,
                                                                            yolo_post_conf, 5, false, false))
            .add_stage<OverlayStage>("overlay", m_app_resources->overlay_stage)
            .add_stage<EncoderStage>("encoder", configure_encoder_and_osd(STREAM_4K), StageType::SINK)
            .add_stage<TeeStage>("tee", std::make_shared<TeeStage>(TEE_STAGE, 2, false, false))
            .add_stage<UdpStage>("udp", configure_udp(STREAM_4K), StageType::SINK)
            .add_stage<WebrtcStage>("webrtc", configure_webrtc_callback(), StageType::SINK)
            .connect_frontend("frontend", STREAM_4K, "aggregator")
            .connect_frontend("frontend", STREAM_640_640, "hailonet")
            .connect("hailonet", "post process")
            .connect("post process", "aggregator")
            .connect("aggregator", "overlay")
            .connect("overlay", "freeze")
            .connect("freeze", "valve")
            .connect("valve", "encoder")
            .connect("encoder", "tee")
            .connect("tee", "udp")
            .connect("tee", "webrtc")
            .build();
}

void CppPipeline::subscribe_callbacks()
{
    WEBSERVER_LOG_INFO("Subscribing callbacks");
    m_resources->m_event_bus->subscribe(
        EventType::SWITCH_PROFILE, EventPriority::EVENT_PRIORITY_MEDIUM,
        std::bind(&CppPipeline::callback_handle_profile_switch, this, std::placeholders::_1));
    m_resources->m_event_bus->subscribe(
        {EventType::CHANGE_FRAMERATE, EventType::CHANGE_RESOLUTION, EventType::CHANGE_FLIP, EventType::CHANGE_ROTATION,
         EventType::CHANGE_GRAYSCALE, EventType::CHANGE_DEWARP, EventType::CHANGE_FREEZE, EventType::CHANGE_VALVE,
         EventType::CHANGE_EIS, EventType::CHANGE_DIS, EventType::CHANGE_DIGITAL_ZOOM,
         EventType::CHANGE_DIGITAL_ZOOM_ROI, EventType::CHANGE_DETECTION},
        EventPriority::EVENT_PRIORITY_MEDIUM,
        std::bind(&CppPipeline::callback_handle_update_profile, this, std::placeholders::_1));

    m_resources->m_event_bus->subscribe(
        {EventType::CHANGE_RESOLUTION, EventType::CHANGE_ROTATION, EventType::SWITCH_PROFILE},
        EventPriority::EVENT_PRIORITY_VERY_HIGH, [this](ResourceStateChangeNotification notification) {
            WEBSERVER_LOG_INFO("handeling before reset notification");
            m_resources->m_event_bus->notify(EventType::CHANGE_VALVE,
                                             std::make_shared<ProfileValveState>(ProfileValveState(false)));
        });
    m_resources->m_event_bus->subscribe(
        {EventType::CHANGE_RESOLUTION, EventType::CHANGE_ROTATION, EventType::SWITCH_PROFILE},
        EventPriority::EVENT_PRIORITY_LOW, [this](ResourceStateChangeNotification notification) {
            WEBSERVER_LOG_INFO("handeling after reset notification");
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            m_resources->m_event_bus->notify(EventType::RESET_ISP, std::make_shared<EmptyState>(EmptyState()));
            m_resources->m_event_bus->notify(EventType::CHANGE_VALVE,
                                             std::make_shared<ProfileValveState>(ProfileValveState(true)));
        });
    m_resources->m_event_bus->subscribe(
        EventType::PROFILE_UPDATE_REQUEST, EventPriority::EVENT_PRIORITY_MEDIUM,
        [this](ResourceStateChangeNotification notification) {
            WEBSERVER_LOG_INFO("Received PROFILE_UPDATE_REQUEST notification");
            auto expected_profile = m_app_resources->media_library->get_current_profile();
            if (!expected_profile.has_value())
            {
                WEBSERVER_LOG_ERROR("Failed to get current profile");
                throw std::runtime_error("Failed to get current profile");
            }
            ProfileConfig current_profile = expected_profile.value();
            m_resources->m_event_bus->notify(EventType::PROFILE_UPDATE,
                                             std::make_shared<ProfileState>(ProfileState(current_profile)));
        });
}

std::shared_ptr<CppPipeline> CppPipeline::create(std::shared_ptr<HTTPServer> svr, std::string config_path,
                                                 Architecture platform)
{
    auto resources = ResourceRepository::create(svr, config_path);
    return std::make_shared<CppPipeline>(resources, platform);
}

void CppPipeline::start()
{
    WEBSERVER_LOG_INFO("Starting CppPipeline");
    build_pipeline();
    // encoder changes all the time so the encoder resource get a pointer to pull the encoder config when its wants
    m_app_resources->media_library->start_pipeline();
    m_app_resources->pipeline->start_pipeline();
    // Create pipeline
    sleep(1);
    auto encoder_resource = std::static_pointer_cast<EncoderResource>(m_resources->get(RESOURCE_ENCODER));
    encoder_resource->set_encoder_query([this]() { return this->get_encoder_config(); });

    WEBSERVER_LOG_INFO("CppPipeline started successfully");

    m_resources->m_event_bus->notify(EventType::RESET_ISP, std::make_shared<EmptyState>(EmptyState()));
}

void CppPipeline::stop()
{
    WEBSERVER_LOG_INFO("Stopping CppPipeline");
    std::cout << "Stopping Pipeline..." << std::endl;
    m_app_resources->pipeline->stop_pipeline();
    m_app_resources->media_library->stop_pipeline();
    m_app_resources->clear();
    WEBSERVER_LOG_INFO("CppPipeline stopped successfully");
}

std::shared_ptr<FrontendStage> CppPipeline::configure_frontend()
{
    WEBSERVER_LOG_INFO("Configuring frontend");
    m_app_resources->frontend = std::make_shared<FrontendStage>(FRONTEND_STAGE, 1, false, false);
    AppStatus frontend_config_status = m_app_resources->frontend->configure(m_app_resources->media_library->m_frontend);
    if (frontend_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure frontend " << FRONTEND_STAGE << std::endl;
        throw std::runtime_error("Failed to configure frontend");
    }
    WEBSERVER_LOG_INFO("Frontend configured successfully");
    return m_app_resources->frontend;
}

std::shared_ptr<EncoderStage> CppPipeline::configure_encoder_and_osd(const std::string &stream_name)
{
    WEBSERVER_LOG_INFO("Creating encoder and osd for stream {}", stream_name);
    std::string enc_name = ENCODER_NAME(stream_name);
    m_app_resources->encoders[enc_name] = std::make_shared<EncoderStage>(enc_name, 3, true);
    AppStatus enc_config_status =
        m_app_resources->encoders[enc_name]->configure(m_app_resources->media_library->m_encoders[STREAM_4K]);
    if (enc_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure encoder " << enc_name << std::endl;
        throw std::runtime_error("Failed to configure encoder");
    }
    WEBSERVER_LOG_INFO("Creating encoder {}", enc_name);
    return m_app_resources->encoders[enc_name];
}

std::shared_ptr<UdpStage> CppPipeline::configure_udp(const std::string &stream_name)
{
    std::string udp_name = UDP_NAME(stream_name);
    WEBSERVER_LOG_INFO("Creating udp {}", udp_name);
    std::shared_ptr<UdpStage> udp_stage = std::make_shared<UdpStage>(udp_name);
    AppStatus udp_config_status = udp_stage->configure(HOST_IP, HOST_PORT, EncodingType::H264);
    if (udp_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure udp " << udp_name << std::endl;
        throw std::runtime_error("Failed to configure udp");
    }
    WEBSERVER_LOG_INFO("Udp {} created successfully", udp_name);
    return udp_stage;
}

std::shared_ptr<WebrtcStage> CppPipeline::configure_webrtc_callback()
{
    std::shared_ptr<webserver::resources::WebRtcResource> webrtc_resource =
        std::static_pointer_cast<WebRtcResource>(m_resources->get(RESOURCE_WEBRTC));

    std::shared_ptr<WebrtcStage> rtp_converter =
        std::make_shared<WebrtcStage>("rtp_converter", webrtc_resource, 5, true, false);
    rtp_converter->configure(EncodingType::H264);
    return rtp_converter;
}
