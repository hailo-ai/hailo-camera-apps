#include "privacy_mask.hpp"
#include <iostream>
#include <cmath> // For sin, cos

using namespace webserver::resources;

PrivacyMaskResource::PrivacyMaskResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<webserver::resources::ConfigResourceBase> configs) 
    : Resource(event_bus),
      m_privacy_masks{},
      m_original_privacy_masks{}
{
    m_config = nlohmann::json::parse("{}");
    nlohmann::json frontend_conf = configs->get_frontend_default_config();
    m_flip = StreamConfigResourceState::flip_string_to_enum(frontend_conf["flip"]["direction"]);
    m_rotation = StreamConfigResourceState::rotation_string_to_enum(frontend_conf["rotation"]["angle"]);
    m_dewarp = frontend_conf["dewarp"]["enabled"];
    m_frame.width = frontend_conf["input_video"]["resolution"]["width"];
    m_frame.height = frontend_conf["input_video"]["resolution"]["height"];

    WEBSERVER_LOG_INFO("PrivacyMaskResource initialized with default values");

    subscribe_callback(EventType::STREAM_CONFIG, [this](ResourceStateChangeNotification notification) {
        WEBSERVER_LOG_INFO("Received STREAM_CONFIG notification");
        auto state = std::static_pointer_cast<StreamConfigResourceState>(notification.resource_state);

        m_flip = state->flip_enabled ? state->flip : FLIP_DIRECTION_NONE;
        m_rotation = state->rotate_enabled ? state->rotation : ROTATION_ANGLE_0;
        m_dewarp = state->dewarp_enabled;
        m_frame = state->resolutions[0];

        m_privacy_masks.clear();
        for (auto& [key, original_polygon] : m_original_privacy_masks)
        {
            polygon flip_rotated_polygon;
            flip_rotated_polygon.id = original_polygon.id;
            for (auto& vertex : original_polygon.vertices)
            {
                if (m_dewarp) {
                    // privacy mask is applied on a transformed (post-dewarp) frame
                    // so we need to adjust the polygon vertices accordingly
                    flip_rotated_polygon.vertices.push_back(flip_rotate_point(vertex));
                } else {
                    // otherwise, privacy mask is applied on the original frame
                    // and only after the frame is trasnformed in the multi resize operation
                    // so we don't need to adjust the polygon
                    flip_rotated_polygon.vertices.push_back(vertex);
                }
            }
            m_privacy_masks[key] = flip_rotated_polygon;
        }

        for (auto& mask : m_config["masks"]) {
            for (size_t i = 0; i < mask["Polygon"].size(); ++i) {
                mask["Polygon"][i]["x"] = m_privacy_masks[mask["id"].get<std::string>()].vertices[i].x;
                mask["Polygon"][i]["y"] = m_privacy_masks[mask["id"].get<std::string>()].vertices[i].y;
            }
        }

        on_resource_change(EventType::CHANGED_RESOURCE_PRIVACY_MASK, update_all_vertices_state());
    });

    subscribe_callback(EventType::STREAM_CONFIG, EventPriority::LOW, [this](ResourceStateChangeNotification notification) {
        WEBSERVER_LOG_INFO("Received STREAM_CONFIG notification with LOW priority");
        auto state = std::static_pointer_cast<StreamConfigResourceState>(notification.resource_state);
        if (state->rotate_state_changed || state->resolutions[0].stream_size_changed)
        {
            renable_masks();
        }
    });

    subscribe_callback(EventType::CODEC_CHANGE, EventPriority::LOW, [this](ResourceStateChangeNotification notification) {
        WEBSERVER_LOG_INFO("Received CODEC_CHANGE notification with LOW priority");
        renable_masks();
    });
}

void PrivacyMaskResource::reset_config(){
    WEBSERVER_LOG_INFO("Resetting config");
    renable_masks();
}

vertex PrivacyMaskResource::flip_rotate_point(const vertex &p)
{
    WEBSERVER_LOG_INFO("Rotating + Flipping point ");

    auto original_point = p;
    privacy_mask_types::vertex rotated_point(0, 0);

    switch(m_rotation)
    {
    case ROTATION_ANGLE_0:
        rotated_point.x = original_point.x;
        rotated_point.y = original_point.y;
        break;
    case ROTATION_ANGLE_90:
        rotated_point.x = m_frame.height - original_point.y;
        rotated_point.y = original_point.x; 
        break;
    case ROTATION_ANGLE_180:
        rotated_point.x = m_frame.width - original_point.x;
        rotated_point.y = m_frame.height - original_point.y;
        break;  
    case ROTATION_ANGLE_270:
        rotated_point.x = original_point.y;
        rotated_point.y = m_frame.width - original_point.x;
        break;
    default:
        WEBSERVER_LOG_ERROR("Invalid rotation angle");
        break;
    }

    auto rotated_frame = m_frame;
    if (m_rotation == ROTATION_ANGLE_90 || m_rotation == ROTATION_ANGLE_270)
    {
        std::swap(rotated_frame.width, rotated_frame.height);
    }

    privacy_mask_types::vertex flipped_point(0, 0);

    switch (m_flip)
    {
    case FLIP_DIRECTION_NONE:
        flipped_point.x = rotated_point.x;
        flipped_point.y = rotated_point.y;
        break;
    case FLIP_DIRECTION_HORIZONTAL:
        flipped_point.x = rotated_frame.width - rotated_point.x;
        flipped_point.y = rotated_point.y; 
        break;
    case FLIP_DIRECTION_VERTICAL:
        flipped_point.x = rotated_point.x;
        flipped_point.y = rotated_frame.height - rotated_point.y;
        break;  
    case FLIP_DIRECTION_BOTH:
        flipped_point.x = rotated_frame.width - rotated_point.x;
        flipped_point.y = rotated_frame.height - rotated_point.y;
        break;
    default:
        WEBSERVER_LOG_ERROR("Invalid flip direction");
        break;
    };

    return flipped_point;
}
vertex PrivacyMaskResource::reverse_flip_rotate_point(const vertex &p)
{
    WEBSERVER_LOG_INFO("Reverse Rotating + Flipping point ");

    auto rotated_frame = m_frame;
    if (m_rotation == ROTATION_ANGLE_90 || m_rotation == ROTATION_ANGLE_270)
    {
        std::swap(rotated_frame.width, rotated_frame.height);
    }

    auto original_point = p;
    privacy_mask_types::vertex flipped_point(0, 0);

    switch (m_flip)
    {
    case FLIP_DIRECTION_NONE:
        flipped_point.x = original_point.x;
        flipped_point.y = original_point.y;
        break;
    case FLIP_DIRECTION_HORIZONTAL:
        flipped_point.x = rotated_frame.width - original_point.x;
        flipped_point.y = original_point.y; 
        break;
    case FLIP_DIRECTION_VERTICAL:
        flipped_point.x = original_point.x;
        flipped_point.y = rotated_frame.height - original_point.y;
        break;  
    case FLIP_DIRECTION_BOTH:
        flipped_point.x = rotated_frame.width - original_point.x;
        flipped_point.y = rotated_frame.height - original_point.y;
        break;
    default:
        WEBSERVER_LOG_ERROR("Invalid flip direction");
        break;
    };

    privacy_mask_types::vertex rotated_point(0, 0);

    switch(m_rotation)
    {
    case ROTATION_ANGLE_0:
        rotated_point.x = flipped_point.x;
        rotated_point.y = flipped_point.y;
        break;
    case ROTATION_ANGLE_90:
        rotated_point.x = flipped_point.y;
        rotated_point.y = m_frame.width - flipped_point.x;
        break;
    case ROTATION_ANGLE_180:
        rotated_point.x = m_frame.width - flipped_point.x;
        rotated_point.y = m_frame.height - flipped_point.y;
        break;  
    case ROTATION_ANGLE_270:
        rotated_point.x = m_frame.height - flipped_point.y;
        rotated_point.y = flipped_point.x; 
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
            m_privacy_masks.erase(entry["path"].get<std::string>());
            m_original_privacy_masks.erase(entry["path"].get<std::string>());
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
            polygon flip_rotated_polygon, original_polygon;
            flip_rotated_polygon.id = mask["id"].get<std::string>();
            original_polygon.id = mask["id"].get<std::string>();
            if (mask["Polygon"].empty()){
                WEBSERVER_LOG_WARNING("Got polygon without points in privacy mask, skipping");
                continue;
            }
            for (const auto& point : mask["Polygon"]) {
                vertex v(point["x"].get<uint>(), point["y"].get<uint>());
                // the polygon coordinates we got are adjusted to the flipped/rotated frame
                // calculate the normalized polygon by reversing the frame transformation
                flip_rotated_polygon.vertices.push_back(v);
                original_polygon.vertices.push_back(reverse_flip_rotate_point(v));
            }
            m_original_privacy_masks[original_polygon.id] = original_polygon;
            if (!m_dewarp) {
                // privacy mask is applied on the original frame in the multi resize operation
                // so the transformed polygon is the same as the original polygon
                m_privacy_masks[flip_rotated_polygon.id] = original_polygon;
            } else {
                // privacy mask is applied on the dewarped frame
                // so the transformed polygon is the same as the polygon we got
                m_privacy_masks[flip_rotated_polygon.id] = flip_rotated_polygon;
            }
        }
    }
    catch (const std::exception& e)
    {
        WEBSERVER_LOG_ERROR("Failed to parse json body for privacy mask, no change has been made: {}", e.what());
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