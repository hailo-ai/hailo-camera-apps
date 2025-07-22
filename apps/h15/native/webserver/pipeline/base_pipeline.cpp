#include "pipeline/base_pipeline.hpp"
#include "common/common.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;

IPipeline::IPipeline(WebserverResourceRepository resources)
{
    m_resources = resources;
    subscribe_to_events();
}

void IPipeline::subscribe_to_events()
{
    WEBSERVER_LOG_DEBUG("Subscribing to events");
    using CallbackFunction = void (IPipeline::*)(ResourceStateChangeNotification);

    std::map<EventType, CallbackFunction> event_callback_map = {
        {EventType::CHANGED_RESOURCE_OSD, &IPipeline::callback_handle_osd},
        {EventType::CHANGED_RESOURCE_ENCODER, &IPipeline::callback_handle_encoder},
        {EventType::CHANGED_RESOURCE_PRIVACY_MASK, &IPipeline::callback_handle_privacy_mask},
    };

    for (const auto &event_callback : event_callback_map)
    {
        m_resources->m_event_bus->subscribe(event_callback.first, EventPriority::EVENT_PRIORITY_MEDIUM,
                                            std::bind(event_callback.second, this, std::placeholders::_1));
    }
}

void IPipeline::callback_handle_osd(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling OSD resource state change");

    std::shared_ptr<osd::Blender> osd_blender = get_osd_blender();
    auto state = notif.getResourceStateFromBase<OsdResource::OsdResourceState>();

    for (auto &id : state->overlays_to_delete)
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
        WEBSERVER_LOG_INFO("Pipeline: Setting text overlay enabled state: {} to {}", text_overlay.get_overlay().id,
                           text_overlay.get_enabled());
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
        WEBSERVER_LOG_INFO("Pipeline: Setting image overlay enabled state: {} to {}", image_overlay.get_overlay().id,
                           image_overlay.get_enabled());
        osd_blender->set_overlay_enabled(image_overlay.get_overlay().id, image_overlay.get_enabled());
        if (image_overlay.get_enabled())
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
        WEBSERVER_LOG_INFO("Pipeline: Setting datetime overlay enabled state: {} to {}",
                           datetime_overlay.get_overlay().id, datetime_overlay.get_enabled());
        osd_blender->set_overlay_enabled(datetime_overlay.get_overlay().id, datetime_overlay.get_enabled());
        if (datetime_overlay.get_enabled())
        {
            WEBSERVER_LOG_INFO("Pipeline: Updating datetime overlay: {}", datetime_overlay.get_overlay().id);
            osd_blender->set_overlay_async(datetime_overlay.get_overlay());
        }
    }
    WEBSERVER_LOG_DEBUG("Pipeline: OSD resource state change handled");
}

void IPipeline::callback_handle_privacy_mask(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling privacy mask resource state change");
    auto state = notif.getResourceStateFromBase<PrivacyMaskResource::PrivacyMaskResourceState>();
    auto masks = state->masks;
    std::shared_ptr<PrivacyMaskBlender> privacy_blender = get_privacy_blender();

    if (state->color)
    {
        privacy_blender->set_color(state->color.value());
    }
    else if (state->pixelization_size)
    {
        privacy_blender->set_pixelization_size(state->pixelization_size.value());
    }

    for (std::string id : state->changed_to_enabled)
    {
        WEBSERVER_LOG_DEBUG("Pipeline: recived candidate mask to add: {} ", id);
        if (masks.find(id) != masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Adding privacy mask: {}", id);
            privacy_blender->add_static_privacy_mask(masks[id]);
        }
    }
    for (std::string id : state->changed_to_disabled)
    {
        WEBSERVER_LOG_DEBUG("Pipeline: recived candidate mask to disable: {} ", id);
        if (masks.find(id) != masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Removing privacy mask: {}", id);
            privacy_blender->remove_static_privacy_mask(id);
        }
    }
    for (std::string &mask : state->polygon_to_update)
    {
        WEBSERVER_LOG_DEBUG("Pipeline: recived candidate mask to update: {} ", mask);
        if (masks.find(mask) != masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Updating privacy mask: {}", mask);
            privacy_blender->set_static_privacy_mask(masks[mask]);
        }
    }
    for (std::string &mask : state->polygon_to_delete)
    {
        WEBSERVER_LOG_DEBUG("Pipeline: recived candidate mask to delete: {} ", mask);
        if (masks.find(mask) != masks.end())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Deleting privacy mask: {}", mask);
            privacy_blender->remove_static_privacy_mask(mask);
        }
    }
    WEBSERVER_LOG_DEBUG("Pipeline: Privacy mask resource state change handled");
}
