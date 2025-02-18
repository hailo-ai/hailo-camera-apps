#include "pipeline/pipeline.hpp"
#include "common/common.hpp"
#include "osd.hpp"
#include <gst/gst.h>

using namespace webserver::pipeline;
using namespace webserver::resources;
using namespace privacy_mask_types;

#define DETECTION_HEF_PATH "/home/root/apps/webserver/resources/yolov5m_wo_spp_60p_nv12_640.hef"

std::shared_ptr<Pipeline> Pipeline::create(std::shared_ptr<HTTPServer> svr)
{
    auto resources = ResourceRepository::create(svr);
    return std::make_shared<Pipeline>(resources);
}

nlohmann::json create_encoder_osd_config(WebserverResourceRepository resources)
{
    auto osd_resource = std::static_pointer_cast<OsdResource>(resources->get(RESOURCE_OSD));
    nlohmann::json encoder_osd_config;
    encoder_osd_config["osd"] = osd_resource->get_encoder_osd_config();
    encoder_osd_config["encoding"] = resources->get(RESOURCE_ENCODER)->get();
    return encoder_osd_config;
}

std::string Pipeline::create_gst_pipeline_string()
{
    auto enabled_apps = std::static_pointer_cast<AiResource>(m_resources->get(RESOURCE_AI))->get_enabled_applications();
    auto detection_pass_through = std::find(enabled_apps.begin(), enabled_apps.end(), AiResource::AI_APPLICATION_DETECTION) != enabled_apps.end() ? "false" : "true";
    auto fe_resource = std::static_pointer_cast<FrontendResource>(m_resources->get(RESOURCE_FRONTEND));

    auto fe_config = fe_resource->get_frontend_config();
    const int encoder_fps = fe_config["output_video"]["resolutions"][0]["framerate"];

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

Pipeline::Pipeline(WebserverResourceRepository resources)
    : IPipeline(resources)
{
    WEBSERVER_LOG_DEBUG("Subscribing to events");
    using CallbackFunction = void (Pipeline::*)(ResourceStateChangeNotification);

    std::map<EventType, CallbackFunction> event_callback_map = {
        {CHANGED_RESOURCE_FRONTEND, &Pipeline::callback_handle_frontend},
        {STREAM_CONFIG, &Pipeline::callback_handle_stream_config},
        {CHANGED_RESOURCE_OSD, &Pipeline::callback_handle_osd},
        {CHANGED_RESOURCE_ENCODER, &Pipeline::callback_handle_encoder},
        {CHANGED_RESOURCE_AI, &Pipeline::callback_handle_ai},
        {CHANGED_RESOURCE_PRIVACY_MASK, &Pipeline::callback_handle_privacy_mask},
        {CHANGED_RESOURCE_ISP, &Pipeline::callback_handle_isp},
        {RESTART_FRONTEND, &Pipeline::callback_handle_restart_frontend},
        {CODEC_CHANGE, &Pipeline::callback_handle_simple_restart},
        {RESET_CONFIG, &Pipeline::callback_handle_simple_restart},//think if really need restart
    };

    for (const auto &event_callback : event_callback_map)
    {
        m_resources->m_event_bus->subscribe(event_callback.first, EventPriority::MEDIUM, std::bind(event_callback.second, this, std::placeholders::_1));
    }
}

hailo_encoder_config_t IPipeline::get_encoder_config()
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

