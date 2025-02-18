#include "pipeline/pipeline.hpp"
#include "common/common.hpp"
#include "resources/common/resources.hpp"
#include "resources/webrtc.hpp"
#include <gst/gst.h>

using namespace webserver::pipeline;
using namespace webserver::resources;

std::shared_ptr<IPipeline> IPipeline::create(std::shared_ptr<HTTPServer> svr)
{
    WEBSERVER_LOG_INFO("Creating pipeline");
    return Pipeline::create(svr);
}

IPipeline::IPipeline(WebserverResourceRepository resources)
{
    m_resources = resources;
    gst_init(nullptr, nullptr);
}

void IPipeline::start()
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

void IPipeline::stop()
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

void IPipeline::set_gst_callbacks()
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

void IPipeline::new_sample(GstAppSink *appsink, gpointer user_data)
{
    WebRtcResource *webRtcResource = static_cast<WebRtcResource *>(user_data);
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    webRtcResource->send_rtp_packet(sample);
    gst_sample_unref(sample);
}