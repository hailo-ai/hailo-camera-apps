#include "cpp_pipeline.hpp"

#define FRONTEND_STAGE "frontend_stage"
#define ENCODER_NAME(str) (std::string(str) + "_encoder")
#define UDP_NAME(str) (std::string(str) + "_udp")
#define HOST_IP "10.0.0.2"
#define HOST_PORT "5000"
#define TEE_STAGE "vision_tee"

using namespace webserver::pipeline;
using namespace webserver::resources;
// TODO create bigger pipeline that include more then one stream
// TODO why all the ai example is hpp it dameges compilation
CppPipeline::CppPipeline(WebserverResourceRepository resources)
    : IPipeline(resources), m_app_resources(std::make_shared<AppResources>())
{
    WEBSERVER_LOG_INFO("Initializing CppPipeline");
    auto config = std::static_pointer_cast<ConfigResourceMedialib>(m_resources->get(RESOURCE_CONFIG_MANAGER));
    std::string medialib_config_string = config->get_current_medialib_config().dump();
    m_app_resources->media_library = std::make_shared<MediaLibrary>();
    if (m_app_resources->media_library->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return;
    }
    m_resources->m_event_bus->subscribe(SWITCH_PROFILE, EventPriority::MEDIUM, std::bind(&CppPipeline::callback_handle_profile_switch, this, std::placeholders::_1));
    WEBSERVER_LOG_INFO("CppPipeline initialized successfully");
}

std::shared_ptr<CppPipeline> CppPipeline::create(std::shared_ptr<HTTPServer> svr, std::string config_path)
{
    auto resources = ResourceRepository::create(svr, config_path);
    return std::make_shared<CppPipeline>(resources);
}

void CppPipeline::start()
{
    WEBSERVER_LOG_INFO("Starting CppPipeline");
    // Create pipeline
    m_app_resources->pipeline = PipelineBuilder()
                                    .addStage<FrontendStage>("frontend", configure_frontend(), StageType::SOURCE)
                                    .addStage<EncoderStage>("encoder", configure_encoder_and_osd(STREAM_4K), StageType::SINK)
                                    .addStage<TeeStage>("tee", std::make_shared<TeeStage>(TEE_STAGE, 2, false, false))
                                    .addStage<UdpStage>("udp", configure_udp(STREAM_4K), StageType::SINK)
                                    .addStage<WebrtcStage>("webrtc", configure_webrtc_callback(), StageType::SINK)
                                    .connectFrontend("frontend", STREAM_4K, "encoder")
                                    .connect("encoder", "tee")
                                    .connect("tee", "udp")
                                    .connect("tee", "webrtc")
                                    .build();

    // encoder changes all the time so the encoder resource get a pointer to pull the encoder config when its wants
    m_app_resources->pipeline->start_pipeline();
    sleep(1);
    auto encoder_resource = std::static_pointer_cast<EncoderResource>(m_resources->get(RESOURCE_ENCODER));
    encoder_resource->set_encoder_query([this]()
                                        { return this->get_encoder_config(); });

    WEBSERVER_LOG_INFO("CppPipeline started successfully");
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
    m_app_resources->encoders[enc_name] = std::make_shared<EncoderStage>(enc_name);
    AppStatus enc_config_status = m_app_resources->encoders[enc_name]->configure(m_app_resources->media_library->m_encoders[STREAM_4K]);
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
    std::shared_ptr<UdpStage> udp_stage = std::make_shared<UdpStage>(UDP_NAME(udp_name));
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

    std::shared_ptr<WebrtcStage> rtp_converter = std::make_shared<WebrtcStage>("rtp_converter", webrtc_resource, 5, true, false);
    rtp_converter->configure(EncodingType::H264);
    return rtp_converter;
}
