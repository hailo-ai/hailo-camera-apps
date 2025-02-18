#include "pipeline/pipeline.hpp"
#include "common/common.hpp"
#include "resources/common/resources.hpp"
#include "media_library/encoder_config.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;
using namespace privacy_mask_types;

void Pipeline::callback_handle_frontend(ResourceStateChangeNotification notif){
    GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
    auto state = std::static_pointer_cast<FrontendResource::FrontendResourceState>(notif.resource_state);
    if (state->control.freeze_state_changed){
        WEBSERVER_LOG_DEBUG("Pipeline: Frontend freeze state changed");
        g_object_set(frontend, "freeze", state->control.freeze, NULL);
    }

    g_object_set(frontend, "config-string", state->config.c_str(), NULL);

    gst_object_unref(frontend);
}

void Pipeline::callback_handle_stream_config(ResourceStateChangeNotification notif){
    auto state = std::static_pointer_cast<StreamConfigResourceState>(notif.resource_state);
    if (state->resolutions[0].framerate_changed){
        WEBSERVER_LOG_DEBUG("Pipeline: Stream framerate changed to {}", state->resolutions[0].framerate);
        return;
    }
    stop();
    start();
}

void Pipeline::callback_handle_osd(ResourceStateChangeNotification notif){
    GstElement *enc = gst_bin_get_by_name(GST_BIN(m_pipeline), "enc");
    GValue val = G_VALUE_INIT;
    g_object_get_property(G_OBJECT(enc), "blender", &val);
    void *value_ptr = g_value_get_pointer(&val);
    auto osd_blender = reinterpret_cast<osd::Blender *>(value_ptr);

    auto state = std::static_pointer_cast<OsdResource::OsdResourceState>(notif.resource_state);

    for (auto& id: state->overlays_to_delete)
    {
        WEBSERVER_LOG_INFO("Pipeline: Removing OSD overlay: {}", id);
        osd_blender->remove_overlay(id);
    }

    for (OsdResource::OsdResourceConfig<osd::TextOverlay> &text_overlay : state->text_overlays)
    {
        if (!osd_blender->get_overlay(text_overlay.get_overlay().id))
        {
            WEBSERVER_LOG_INFO("Pipeline: Adding new text overlay: {}", text_overlay.get_overlay().id);
            osd_blender->add_overlay_async(text_overlay.get_overlay());
            continue;
        }
        WEBSERVER_LOG_INFO("Pipeline: Setting text overlay enabled state: {} to {}", text_overlay.get_overlay().id, text_overlay.get_enabled());
        osd_blender->set_overlay_enabled(text_overlay.get_overlay().id, text_overlay.get_enabled());
        if (text_overlay.get_enabled())
        {
            WEBSERVER_LOG_INFO("Pipeline: Updating text overlay: {}", text_overlay.get_overlay().id);
            osd_blender->set_overlay_async(text_overlay.get_overlay());
        }
    }
    for (OsdResource::OsdResourceConfig<osd::ImageOverlay> &image_overlay : state->image_overlays)
    {
        if (!osd_blender->get_overlay(image_overlay.get_overlay().id))
        {
            WEBSERVER_LOG_INFO("Pipeline: Adding new image overlay: {}", image_overlay.get_overlay().id);
            osd_blender->add_overlay_async(image_overlay.get_overlay());
            continue;
        }
        WEBSERVER_LOG_INFO("Pipeline: Setting image overlay enabled state: {} to {}", image_overlay.get_overlay().id, image_overlay.get_enabled());
        osd_blender->set_overlay_enabled(image_overlay.get_overlay().id, image_overlay.get_enabled());
        if(image_overlay.get_enabled())
        {
            WEBSERVER_LOG_INFO("Pipeline: Updating image overlay: {}", image_overlay.get_overlay().id);
            osd_blender->set_overlay_async(image_overlay.get_overlay());
        }
    }
    for (OsdResource::OsdResourceConfig<osd::DateTimeOverlay> &datetime_overlay : state->datetime_overlays)
    {
        if (!osd_blender->get_overlay(datetime_overlay.get_overlay().id))
        {
            WEBSERVER_LOG_INFO("Pipeline: Adding new datetime overlay: {}", datetime_overlay.get_overlay().id);
            osd_blender->add_overlay_async(datetime_overlay.get_overlay());
            continue;
        }
        WEBSERVER_LOG_INFO("Pipeline: Setting datetime overlay enabled state: {} to {}", datetime_overlay.get_overlay().id, datetime_overlay.get_enabled());
        osd_blender->set_overlay_enabled(datetime_overlay.get_overlay().id, datetime_overlay.get_enabled());
        if(datetime_overlay.get_enabled())
        {
            WEBSERVER_LOG_INFO("Pipeline: Updating datetime overlay: {}", datetime_overlay.get_overlay().id);
            osd_blender->set_overlay_async(datetime_overlay.get_overlay());
        }
    }
    gst_object_unref(enc);
}

void Pipeline::callback_handle_encoder(ResourceStateChangeNotification notif){
    WEBSERVER_LOG_DEBUG("Pipeline: Updating encoder config");
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
}

void Pipeline::callback_handle_ai(ResourceStateChangeNotification notif){
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
}

void Pipeline::callback_handle_privacy_mask(ResourceStateChangeNotification notif){
    auto state = std::static_pointer_cast<PrivacyMaskResource::PrivacyMaskResourceState>(notif.resource_state);
    if (state->changed_to_enabled.empty() && state->changed_to_disabled.empty() && state->polygon_to_update.empty() && state->polygon_to_delete.empty())
    {
        return;
    }
    auto masks = state->masks;

    GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
    GValue val = G_VALUE_INIT;
    g_object_get_property(G_OBJECT(frontend), "privacy-mask", &val);
    void *value_ptr = g_value_get_pointer(&val);
    auto privacy_blender = reinterpret_cast<PrivacyMaskBlender *>(value_ptr);

    for (std::string id : state->changed_to_enabled)
    {
        if (masks.find(id) != masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Adding privacy mask: {}", id);
            privacy_blender->add_privacy_mask(masks[id]);
        }
    }
    for (std::string id : state->changed_to_disabled)
    {
        if (masks.find(id) != masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Removing privacy mask: {}", id);
            privacy_blender->remove_privacy_mask(id);
        }
    }
    for (std::string &mask : state->polygon_to_update)
    {
        if (masks.find(mask) != masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Updating privacy mask: {}", mask);
            privacy_blender->set_privacy_mask(masks[mask]);
        }
    }
    for (std::string &mask : state->polygon_to_delete)
    {
        if (masks.find(mask) != masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Deleting privacy mask: {}", mask);
            privacy_blender->remove_privacy_mask(mask);
        }
    }
    gst_object_unref(frontend);
}

void Pipeline::callback_handle_isp(ResourceStateChangeNotification notif){
    m_resources->m_event_bus->notify(EventType::RESTART_FRONTEND, nullptr);
}

void Pipeline::callback_handle_restart_frontend(ResourceStateChangeNotification notif){
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
}

void Pipeline::callback_handle_simple_restart(ResourceStateChangeNotification notif){
    stop();
    start();
}
