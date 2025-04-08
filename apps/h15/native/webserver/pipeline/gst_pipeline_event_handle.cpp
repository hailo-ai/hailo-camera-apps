#include "pipeline/gst_pipeline.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;
using namespace privacy_mask_types;

void GStreamerPipeline::callback_handle_frontend(ResourceStateChangeNotification notif){
    WEBSERVER_LOG_DEBUG("Pipeline: Handling frontend resource state change");
    GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
    auto state = std::static_pointer_cast<FrontendResource::FrontendResourceState>(notif.resource_state);
    if (state->control.freeze_state_changed){
        WEBSERVER_LOG_DEBUG("Pipeline: Frontend freeze state changed");
        g_object_set(frontend, "freeze", state->control.freeze, NULL);
    }

    WEBSERVER_LOG_DEBUG("Pipeline: Setting frontend config-string");
    g_object_set(frontend, "config-string", state->config.c_str(), NULL);

    gst_object_unref(frontend);
    WEBSERVER_LOG_DEBUG("Pipeline: Frontend resource state change handled");
}

void GStreamerPipeline::callback_handle_stream_config(ResourceStateChangeNotification notif){
    WEBSERVER_LOG_DEBUG("Pipeline: Handling stream config resource state change");
    auto state = std::static_pointer_cast<StreamConfigResourceState>(notif.resource_state);
    if (state->resolutions[0].framerate_changed){
        WEBSERVER_LOG_DEBUG("Pipeline: Stream framerate changed to {}", state->resolutions[0].framerate);
        return;
    }
    if (state->dewarp_state_changed){
        WEBSERVER_LOG_DEBUG("Pipeline: Stream dewarp state changed to {}", state->dewarp_enabled);
        return;
    }
    if (state->flip_state_changed){
        WEBSERVER_LOG_DEBUG("Pipeline: Stream flip state changed to {}", state->flip);
        return;
    }

    WEBSERVER_LOG_DEBUG("Pipeline: Restarting pipeline due to stream config change");
    stop();
    start();
    WEBSERVER_LOG_DEBUG("Pipeline: Stream config resource state change handled");
}


void GStreamerPipeline::callback_handle_encoder(ResourceStateChangeNotification notif){
    WEBSERVER_LOG_DEBUG("Pipeline: Handling encoder resource state change");
    auto state = std::static_pointer_cast<EncoderResource::EncoderResourceState>(notif.resource_state);

    GstElement *encoder_element = gst_bin_get_by_name(GST_BIN(m_pipeline), "enc");
    gpointer value = nullptr;
    g_object_get(G_OBJECT(encoder_element), "user-config", &value, NULL);
    if (!value)
    {
        WEBSERVER_LOG_ERROR("Encoder config is null");
    }
    encoder_config_t *config = reinterpret_cast<encoder_config_t *>(value);
    if (!std::holds_alternative<hailo_encoder_config_t>(*config))
    {
        WEBSERVER_LOG_ERROR("Encoder config does not hold hailo_encoder_config_t");
        gst_object_unref(encoder_element);
        return;
    }
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(*config);
    state->control.fill_encoder_element_config(hailo_encoder_config);
    WEBSERVER_LOG_INFO("Encoder updated with {}", state->control.to_string());
    g_object_set(G_OBJECT(encoder_element), "user-config", config, NULL);
    gst_object_unref(encoder_element);
    WEBSERVER_LOG_DEBUG("Pipeline: Encoder resource state change handled");
}

void GStreamerPipeline::callback_handle_ai(ResourceStateChangeNotification notif){
    WEBSERVER_LOG_DEBUG("Pipeline: Handling AI resource state change");
    auto state = std::static_pointer_cast<AiResource::AiResourceState>(notif.resource_state);

    if (state->enabled.empty() && state->disabled.empty())
    {
        WEBSERVER_LOG_DEBUG("Pipeline: No AI applications enabled or disabled");
        return;
    }

    GstElement *detection = gst_bin_get_by_name(GST_BIN(m_pipeline), "detection");
    if (std::find(state->disabled.begin(), state->disabled.end(), AiResource::AI_APPLICATION_DETECTION) != state->disabled.end()) // disable detection if it's in the disabled list
    {
        WEBSERVER_LOG_DEBUG("Pipeline: Disabling detection");
        g_object_set(detection, "pass-through", TRUE, NULL);
    }
    else if (std::find(state->enabled.begin(), state->enabled.end(), AiResource::AI_APPLICATION_DETECTION) != state->enabled.end()) // enable detection if it's in the enabled list
    {
        WEBSERVER_LOG_DEBUG("Pipeline: Enabling detection");
        g_object_set(detection, "pass-through", FALSE, NULL);
    }
    gst_object_unref(detection);
    WEBSERVER_LOG_DEBUG("Pipeline: AI resource state change handled");
}

void GStreamerPipeline::callback_handle_isp(ResourceStateChangeNotification notif){
    WEBSERVER_LOG_DEBUG("Pipeline: Handling ISP resource state change");
    m_resources->m_event_bus->notify(EventType::RESTART_FRONTEND, nullptr);
    WEBSERVER_LOG_DEBUG("Pipeline: ISP resource state change handled");
}

void GStreamerPipeline::callback_handle_restart_frontend(ResourceStateChangeNotification notif){
    WEBSERVER_LOG_INFO("Pipeline: ISP state changed, updating frontend config and restarting frontendsrcbin");
    GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");

    // stop frontend
    WEBSERVER_LOG_DEBUG("Pipeline: Stopping frontend");
    if (GstStateChangeReturn stop_status = gst_element_set_state(frontend, GST_STATE_NULL); stop_status != GST_STATE_CHANGE_SUCCESS)
    {
        GST_ERROR_OBJECT(m_pipeline, "Failed to stop frontend");
        WEBSERVER_LOG_ERROR("Pipeline: Failed to stop frontend");
    }

    // update frontend config
    WEBSERVER_LOG_DEBUG("Pipeline: Updating frontend config");
    auto fe_resource = std::static_pointer_cast<FrontendResource>(m_resources->get(RESOURCE_FRONTEND));//TODO need to send notification to update config
    g_object_set(frontend, "config-string", fe_resource->get_frontend_config().dump().c_str(), NULL);

    // start frontend
    WEBSERVER_LOG_DEBUG("Pipeline: Starting frontend");
    GstStateChangeReturn start_status = gst_element_set_state(frontend, GST_STATE_PLAYING);
    if (start_status != GST_STATE_CHANGE_SUCCESS)
    {
        GST_ERROR_OBJECT(m_pipeline, "Failed to start frontend");
        WEBSERVER_LOG_ERROR("Pipeline: Failed to start frontend");
    }

    gst_object_unref(frontend);
    WEBSERVER_LOG_DEBUG("Pipeline: Frontend restarted");
}

void GStreamerPipeline::callback_handle_simple_restart(ResourceStateChangeNotification notif){
    WEBSERVER_LOG_DEBUG("Pipeline: Handling simple restart");
    stop();
    start();
    WEBSERVER_LOG_DEBUG("Pipeline: Simple restart handled");
}

std::shared_ptr<osd::Blender> GStreamerPipeline::get_osd_blender()
{
    GstElement *enc = gst_bin_get_by_name(GST_BIN(m_pipeline), "enc");
    GValue val = G_VALUE_INIT;
    g_object_get_property(G_OBJECT(enc), "blender", &val);
    void *value_ptr = g_value_get_pointer(&val);
    auto osd_blender = reinterpret_cast<osd::Blender *>(value_ptr);
    gst_object_unref(enc);
    return std::shared_ptr<osd::Blender>(
        osd_blender,
        [](osd::Blender* ptr) {
        }
    );
}

std::shared_ptr<PrivacyMaskBlender> GStreamerPipeline::get_privacy_blender()
{
    GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
    GValue val = G_VALUE_INIT;
    g_object_get_property(G_OBJECT(frontend), "privacy-mask", &val);
    void *value_ptr = g_value_get_pointer(&val);
    auto privacy_blender = reinterpret_cast<PrivacyMaskBlender *>(value_ptr);
    gst_object_unref(frontend);
    return std::shared_ptr<PrivacyMaskBlender>(
        privacy_blender,
        [](PrivacyMaskBlender* ptr) {
        }
    );
}