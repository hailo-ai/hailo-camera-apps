#include "pipeline/gst_pipeline.hpp"
#include "osd.hpp"
#include "resources/webrtc.hpp"
#include "rtc/rtc.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;
using namespace privacy_mask_types;

#define DETECTION_HEF_PATH "/home/root/apps/webserver/resources/yolov5m_wo_spp_60p_nv12_640.hef"

std::shared_ptr<GStreamerPipeline> GStreamerPipeline::create(std::shared_ptr<HTTPServer> svr)
{
    auto resources = ResourceRepository::create(svr);
    return std::make_shared<GStreamerPipeline>(resources);
}

GStreamerPipeline::GStreamerPipeline(WebserverResourceRepository resources)
    : IPipeline(resources)
{
    gst_init(nullptr, nullptr);
}

nlohmann::json create_encoder_osd_config(WebserverResourceRepository resources)
{
    auto osd_resource = std::static_pointer_cast<OsdResource>(resources->get(RESOURCE_OSD));
    nlohmann::json encoder_osd_config;
    encoder_osd_config["osd"] = osd_resource->get_encoder_osd_config();
    encoder_osd_config["encoding"] = resources->get(RESOURCE_ENCODER)->get();
    return encoder_osd_config;
}

std::string GStreamerPipeline::create_gst_pipeline_string()
{
    auto enabled_apps = std::static_pointer_cast<AiResource>(m_resources->get(RESOURCE_AI))->get_enabled_applications();
    auto detection_pass_through = std::find(enabled_apps.begin(), enabled_apps.end(), AiResource::AI_APPLICATION_DETECTION) != enabled_apps.end() ? "false" : "true";
    auto fe_resource = std::static_pointer_cast<FrontendResource>(m_resources->get(RESOURCE_FRONTEND));

    auto fe_config = fe_resource->get_frontend_config();
    const int encoder_fps = fe_config["application_input_streams"]["resolutions"][0]["framerate"];

    auto encoder_config = create_encoder_osd_config(m_resources);
    std::string codec = "264";
    if (encoder_config["encoding"]["hailo_encoder"]["config"]["output_stream"]["codec"] == "CODEC_TYPE_HEVC")
    {
        codec = "265";
    }
    std::ostringstream pipeline;
    pipeline << "hailofrontendbinsrc name=frontend config-string='" << fe_config << "' ";
    pipeline << "hailomuxer name=mux ";
    pipeline << "frontend. ! ";
    pipeline << "queue name=q4 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "mux. ";
    pipeline << "frontend. ! ";
    pipeline << "queue name=q5 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "video/x-raw, width=640, height=640 ! ";
    pipeline << "hailonet name=detection batch-size=4 hef-path=" << DETECTION_HEF_PATH << " pass-through=" << detection_pass_through << " nms-iou-threshold=0.45 nms-score-threshold=0.3 scheduling-algorithm=1 scheduler-threshold=4 scheduler-timeout-ms=1000 vdevice-group-id=1 ! ";
    pipeline << "queue name=q6 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailofilter function-name=yolov5 config-path=/home/root/apps/detection/resources/configs/yolov5.json so-path=/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so qos=false ! ";
    pipeline << "queue name=q7 leaky=downstream max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "mux. ";
    pipeline << "mux. ! ";
    pipeline << "hailooverlay qos=false ! ";
    pipeline << "queue name=q8 leaky=downstream max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailoencodebin name=enc config-string='" << encoder_config.dump() << "' enforce-caps=false ! ";
    pipeline << "video/x-h" << codec << ",framerate=" << encoder_fps << "/1 ! ";
    pipeline << "queue name=q9 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "h" << codec << "parse ! ";
    pipeline << "queue name=q10 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "rtph" << codec << "pay ! ";
    pipeline << "tee name=t ! ";
    pipeline << "application/x-rtp, media=(string)video, encoding-name=(string)H"<< codec <<" ! ";
    pipeline << "queue name=q11 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "appsink name=webtrc_appsink emit-signals=true max-buffers=0 ";
    pipeline << "t. ! ";
    pipeline << "queue name=q12 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "udpsink host=10.0.0.2 sync=false port=5000 ";
    std::string pipeline_str = pipeline.str();
    std::cout << "Pipeline: \n"
              << pipeline_str << std::endl;
    return pipeline_str;
}


hailo_encoder_config_t GStreamerPipeline::get_encoder_config()
{
    GstElement *encoder_element = gst_bin_get_by_name(GST_BIN(m_pipeline), "enc");
    gpointer value = nullptr;
    g_object_get(G_OBJECT(encoder_element), "config", &value, NULL);
    if (!value)
    {
        WEBSERVER_LOG_ERROR("Encoder config is null");
        gst_object_unref(encoder_element);
        std::runtime_error("Encoder config is null");
    }
    encoder_config_t *config = reinterpret_cast<encoder_config_t *>(value);
    if (!std::holds_alternative<hailo_encoder_config_t>(*config))
    {
        WEBSERVER_LOG_ERROR("Encoder config does not hold hailo_encoder_config_t");
        gst_object_unref(encoder_element);
        std::terminate();
        std::runtime_error("Encoder config does not hold hailo_encoder_config_t");
    }
    gst_object_unref(encoder_element);
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(*config);
    return hailo_encoder_config;
}


void GStreamerPipeline::start()
{
    WEBSERVER_LOG_INFO("Starting pipeline");
    m_pipeline = gst_parse_launch(this->create_gst_pipeline_string().c_str(), NULL);
    if (!m_pipeline)
    {
        WEBSERVER_LOG_ERROR("Failed to create pipeline");
        throw std::runtime_error("Failed to create pipeline");
    }
    this->set_gst_callbacks();
    WEBSERVER_LOG_INFO("Setting pipeline to PLAYING");
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        WEBSERVER_LOG_ERROR("Failed to start pipeline");
        throw std::runtime_error("Failed to start pipeline");
    }
    WEBSERVER_LOG_INFO("Pipeline started");
    
    auto encoder_resource = std::static_pointer_cast<EncoderResource>(m_resources->get(RESOURCE_ENCODER));
    encoder_resource->set_encoder_query([this]() { return this->get_encoder_config(); });
}

void GStreamerPipeline::stop()
{
    WEBSERVER_LOG_INFO("Stopping pipeline");
    if (!m_pipeline)
    {
        WEBSERVER_LOG_INFO("Pipeline is not running");
        return;
    }
    gboolean ret = gst_element_send_event(m_pipeline, gst_event_new_eos());
    if (!ret)
    {
        WEBSERVER_LOG_ERROR("Failed to send EOS event");
        throw std::runtime_error("Failed to send EOS event");
    }

    WEBSERVER_LOG_INFO("Setting pipeline to NULL");
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    WEBSERVER_LOG_INFO("Pipeline stopped");
    gst_object_unref(m_pipeline);
}

void GStreamerPipeline::set_gst_callbacks()
{
    GstElement *appsink = gst_bin_get_by_name(GST_BIN(m_pipeline), "webtrc_appsink");
    if (!appsink)
    {
        WEBSERVER_LOG_ERROR("Failed to get appsink");
        throw std::runtime_error("Failed to get appsink");
    }
    WebserverResource webrtc_resource = std::static_pointer_cast<WebRtcResource>(m_resources->get(RESOURCE_WEBRTC));
    g_signal_connect(appsink, "new-sample", G_CALLBACK(new_sample), webrtc_resource.get());
    gst_object_unref(appsink);
}

void GStreamerPipeline::new_sample(GstAppSink *appsink, gpointer user_data)
{
    WebRtcResource *webRtcResource = static_cast<WebRtcResource *>(user_data);
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    webRtcResource->send_rtp_packet(sample);
    gst_sample_unref(sample);
}

