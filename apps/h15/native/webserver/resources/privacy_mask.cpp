#include "privacy_mask.hpp"
#include <iostream>

using namespace webserver::resources;

PrivacyMaskResource::PrivacyMaskResource(std::shared_ptr<EventBus> event_bus) : Resource(event_bus)
{
    m_privacy_masks = {};
    m_config = nlohmann::json::parse("{}");
    m_rotation = StreamConfigResourceState::ROTATION_0; // TODO think what to do when the stream start rotated. mybie take this from config resource
    WEBSERVER_LOG_INFO("PrivacyMaskResource initialized with default values");

    subscribe_callback(STREAM_CONFIG, [this](ResourceStateChangeNotification notification)
                        {
        WEBSERVER_LOG_INFO("Received STREAM_CONFIG notification");
        auto state = std::static_pointer_cast<StreamConfigResourceState>(notification.resource_state);
        if (!state->rotate_enabled)
        {
            WEBSERVER_LOG_INFO("Rotation not enabled, returning");
            return;
        }
        auto old_rotation = m_rotation;
        m_rotation = state->rotation;
        WEBSERVER_LOG_INFO("Rotation changed from {} to {}", old_rotation, m_rotation);
        for (auto& [key, poly] : m_privacy_masks)
        {
            for (auto& point : poly.vertices)
            {
                point = point_rotation(point, state->resolutions[0].width, state->resolutions[0].height, old_rotation, m_rotation);
            }
        }
        for (auto& mask : m_config["masks"]) {
            for (size_t i = 0; i < mask["Polygon"].size(); ++i) {
                mask["Polygon"][i]["x"] = m_privacy_masks[mask["id"].get<std::string>()].vertices[i].x;
                mask["Polygon"][i]["y"] = m_privacy_masks[mask["id"].get<std::string>()].vertices[i].y;
            }
        }
        on_resource_change(EventType::CHANGED_RESOURCE_PRIVACY_MASK, update_all_vertices_state());
    });

    subscribe_callback(EventType::STREAM_CONFIG, EventPriority::LOW, [this](ResourceStateChangeNotification notification)
                        {
        WEBSERVER_LOG_INFO("Received STREAM_CONFIG notification with LOW priority");
        auto state = std::static_pointer_cast<StreamConfigResourceState>(notification.resource_state);
        if (state->rotate_enabled || state->resolutions[0].stream_size_changed)
        {
            renable_masks();
        }
        
    });

    subscribe_callback(EventType::CODEC_CHANGE, EventPriority::LOW, [this](ResourceStateChangeNotification notification)
                        {
        WEBSERVER_LOG_INFO("Received CODEC_CHANGE notification with LOW priority");
        renable_masks();
    });
}

void PrivacyMaskResource::reset_config(){
    WEBSERVER_LOG_INFO("Resetting config");
    renable_masks();
}

vertex PrivacyMaskResource::point_rotation(vertex &point, uint32_t width, uint32_t height, StreamConfigResourceState::rotation_t from_rotation, StreamConfigResourceState::rotation_t to_rotation){
    int angle_diff = to_rotation - from_rotation;
    WEBSERVER_LOG_INFO("Rotating point ");
    if (angle_diff == 0){
        return point;
    }
    privacy_mask_types::vertex rotated_point(0, 0);
    switch (angle_diff)
    {
    case 90:
    case -270:
        rotated_point.x = height - point.y; // TODO check if need to switch width and high
        rotated_point.y = point.x;
        break;
    case 180:
    case -180:
        rotated_point.x = width - point.x;
        rotated_point.y = height - point.y;
        break;  
    case 270:
    case -90:
        rotated_point.x = point.y;
        rotated_point.y = width - point.x;
        break;
    default:
        WEBSERVER_LOG_ERROR("Invalid rotation angle");
        break;
    }
    return rotated_point;
}

std::shared_ptr<PrivacyMaskResource::PrivacyMaskResourceState> PrivacyMaskResource::update_all_vertices_state(){
    WEBSERVER_LOG_INFO("Updating all vertices state");
    return this->parse_state(this->get_enabled_masks(), this->get_enabled_masks(), nlohmann::json::parse("{}"));
}

void PrivacyMaskResource::renable_masks(){
    WEBSERVER_LOG_INFO("Re-enabling masks");
    auto state = std::make_shared<PrivacyMaskResource::PrivacyMaskResourceState>(m_privacy_masks, this->get_enabled_masks(), std::vector<std::string>{}, std::vector<std::string>{}, std::vector<std::string>{});
    on_resource_change(EventType::CHANGED_RESOURCE_PRIVACY_MASK, state);
}

std::shared_ptr<PrivacyMaskResource::PrivacyMaskResourceState> PrivacyMaskResource::parse_state(std::vector<std::string> current_enabled, std::vector<std::string> prev_enabled, nlohmann::json diff)
{
    WEBSERVER_LOG_INFO("Parsing state with current enabled masks: {} and previous enabled masks: {}", current_enabled.size(), prev_enabled.size());
    auto state = std::make_shared<PrivacyMaskResource::PrivacyMaskResourceState>(m_privacy_masks);
    for (auto &mask : m_privacy_masks)
    {
        if (std::find(current_enabled.begin(), current_enabled.end(), mask.first) != current_enabled.end() &&
            std::find(prev_enabled.begin(), prev_enabled.end(), mask.first) == prev_enabled.end())
        {
            state->changed_to_enabled.push_back(mask.first);
        }
        if (std::find(current_enabled.begin(), current_enabled.end(), mask.first) == current_enabled.end() &&
            std::find(prev_enabled.begin(), prev_enabled.end(), mask.first) != prev_enabled.end())
        {
            state->changed_to_disabled.push_back(mask.first);
        }
        if (std::find(current_enabled.begin(), current_enabled.end(), mask.first) != current_enabled.end() &&
            std::find(prev_enabled.begin(), prev_enabled.end(), mask.first) != prev_enabled.end())
        {
            // update all the polyagons that are still on the screen
            state->polygon_to_update.push_back(mask.first);
        }
    }
    for (auto &entry : diff)
    {
        if (entry["op"] == "remove")
        {
            state->polygon_to_delete.push_back(entry["path"].get<std::string>());
        }
    }
    return state;
}

std::vector<std::string> PrivacyMaskResource::get_enabled_masks()
{
    WEBSERVER_LOG_INFO("Getting enabled masks");
    std::vector<std::string> enabled;
    for (const auto& mask : m_config["masks"]) {
        if(mask["status"].get<bool>() && m_config["global_enable"].get<bool>()){
            enabled.push_back(mask["id"].get<std::string>());
        }
    }
    return enabled;
}

void PrivacyMaskResource::parse_polygon(nlohmann::json j){
    try
    {
        WEBSERVER_LOG_INFO("Parsing polygon from JSON");
        for (const auto& mask : j["masks"]) {
            polygon poly;
            poly.id = mask["id"].get<std::string>();
            if (mask["Polygon"].empty()){
                WEBSERVER_LOG_WARNING("Got polygon without points in privacy mask, skipping");
                continue;
            }
            for (const auto& point : mask["Polygon"]) {
                poly.vertices.emplace_back(point["x"].get<uint>(), point["y"].get<uint>());
            }

            m_privacy_masks[poly.id] = poly;
        }
    }
    catch (const std::exception& e)
    {
        WEBSERVER_LOG_ERROR("Failed to parse json body for privacy mask, no change have been made: {}", e.what());
    }
}

std::shared_ptr<PrivacyMaskResource::PrivacyMaskResourceState> PrivacyMaskResource::delete_masks_from_config(nlohmann::json config){
    WEBSERVER_LOG_INFO("Deleting masks from config");
    std::shared_ptr<PrivacyMaskResource::PrivacyMaskResourceState> state = std::make_shared<PrivacyMaskResource::PrivacyMaskResourceState>(m_privacy_masks);
    for (const auto& mask_to_del : config["masks"]) {
        std::string id = mask_to_del["id"].get<std::string>();
        if (m_config.contains("masks")) {
            state->polygon_to_delete.push_back(id);
            m_config["masks"].erase(std::remove_if(m_config["masks"].begin(), m_config["masks"].end(), [id](const nlohmann::json& mask){
                return mask["id"].get<std::string>() == id;
            }), m_config["masks"].end());
        }
    }
    return state;
}

void PrivacyMaskResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    WEBSERVER_LOG_INFO("Registering HTTP endpoints for PrivacyMaskResource");

    srv->Get("/privacy_mask", std::function<nlohmann::json()>([this]()
                                                              { 
                                                                  WEBSERVER_LOG_INFO("GET /privacy_mask called");
                                                                  return this->m_config; }));

    srv->Patch("/privacy_mask", [this](const nlohmann::json &partial_config)
            {
                WEBSERVER_LOG_INFO("PATCH /privacy_mask called");
                auto prev_enabled = this->get_enabled_masks();
                nlohmann::json diff = nlohmann::json::diff(m_config, partial_config);
                m_config.merge_patch(partial_config);
                this->parse_polygon(m_config);
                on_resource_change(EventType::CHANGED_RESOURCE_PRIVACY_MASK, this->parse_state(this->get_enabled_masks(), prev_enabled, diff));
                return this->m_config; });

    srv->Put("/privacy_mask", [this](const nlohmann::json &config)
             {
                WEBSERVER_LOG_INFO("PUT /privacy_mask called");
                auto prev_enabled = this->get_enabled_masks();
                auto partial_config = nlohmann::json::diff(m_config, config);
                m_config = m_config.patch(partial_config);
                this->parse_polygon(m_config);
                on_resource_change(EventType::CHANGED_RESOURCE_PRIVACY_MASK, this->parse_state(this->get_enabled_masks(), prev_enabled, partial_config));
                return this->m_config; });

    srv->Delete("/privacy_mask", [this](const nlohmann::json &config){
        WEBSERVER_LOG_INFO("DELETE /privacy_mask called");
        auto state = delete_masks_from_config(config);
        on_resource_change(EventType::CHANGED_RESOURCE_PRIVACY_MASK, state);
        return this->m_config;
    });
}