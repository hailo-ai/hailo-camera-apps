#include "frontend.hpp"
#include <iostream>

webserver::resources::FrontendResource::FrontendResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<webserver::resources::AiResource> ai_res, std::shared_ptr<webserver::resources::IspResource> isp_res, std::shared_ptr<webserver::resources::ConfigResourceBase> configs) : Resource(event_bus)
{
    WEBSERVER_LOG_INFO("Initializing FrontendResource");
    m_default_config = configs->get_frontend_default_config().dump(4);
    m_ai_resource = ai_res;
    m_isp_resource = isp_res;
    reset_config();
    WEBSERVER_LOG_INFO("FrontendResource initialized successfully");
}

void webserver::resources::FrontendResource::reset_config(){
    m_config = nlohmann::json::parse(m_default_config);
    m_frontend_config.freeze = false;
    m_frontend_config.flip_enabled = m_config["flip"]["enabled"];
    m_frontend_config.rotate_enabled = m_config["rotation"]["enabled"];
    
    const auto& resolutions = m_config["application_input_streams"]["resolutions"];
    for (const auto& res : resolutions)
    {
        m_frontend_config.resolutions.push_back({res["width"], res["height"], res["framerate"]});
    }

    update_frontend_config();
}

void webserver::resources::FrontendResource::update_frontend_config()
{
    WEBSERVER_LOG_INFO("Updating frontend config");

    std::string old_flip = m_frontend_config.flip;
    bool flip_state_changed = m_frontend_config.flip_enabled != m_config["flip"]["enabled"] || (m_frontend_config.flip_enabled && (m_frontend_config.flip != m_config["flip"]["direction"]));
    m_frontend_config.flip_enabled = m_config["flip"]["enabled"];
    m_frontend_config.flip = m_config["flip"]["direction"];

    std::string old_rotate = m_frontend_config.rotate;
    bool rotate_state_changed = m_frontend_config.rotate_enabled != m_config["rotation"]["enabled"] || (m_frontend_config.rotate_enabled && (m_frontend_config.rotate != m_config["rotation"]["angle"]));
    m_frontend_config.rotate_enabled = m_config["rotation"]["enabled"];
    m_frontend_config.rotate = m_config["rotation"]["angle"];

    bool dewarp_state_changed = m_frontend_config.dewarp_enabled != m_config["dewarp"]["enabled"];
    m_frontend_config.dewarp_enabled = m_config["dewarp"]["enabled"];

    std::vector<webserver::resources::FrontendResource::Resolution> old_resolutions;
    for (const auto& res : m_frontend_config.resolutions)
    {
        old_resolutions.push_back(res);
    }
    m_frontend_config.resolutions.clear();
    for (const auto& res : m_config["application_input_streams"]["resolutions"])
    {
        m_frontend_config.resolutions.push_back({res["width"], res["height"], res["framerate"]});
    }
    bool at_least_one_stream_resolution_changed = false;
    bool at_least_one_stream_framerate_changed = false;
    std::vector<webserver::resources::StreamConfigResourceState::Resolution> resolutions;
    for (u_int32_t i = 0; i < m_frontend_config.resolutions.size(); i++)
    {
        bool framerate_changed = m_frontend_config.resolutions[i].framerate != old_resolutions[i].framerate;
        bool stream_size_changed = m_frontend_config.resolutions[i].width != old_resolutions[i].width || m_frontend_config.resolutions[i].height != old_resolutions[i].height;
        at_least_one_stream_framerate_changed = at_least_one_stream_framerate_changed || framerate_changed;
        at_least_one_stream_resolution_changed = at_least_one_stream_resolution_changed || stream_size_changed;
        resolutions.push_back({m_frontend_config.resolutions[i].width, m_frontend_config.resolutions[i].height, m_frontend_config.resolutions[i].framerate, framerate_changed, stream_size_changed});
    }
    if (at_least_one_stream_framerate_changed || flip_state_changed || rotate_state_changed || dewarp_state_changed || at_least_one_stream_resolution_changed){
        WEBSERVER_LOG_INFO("Stream configuration changed, notifying listeners");
        auto state = StreamConfigResourceState(resolutions,
            flip_state_changed, m_frontend_config.flip, m_frontend_config.flip_enabled,
            rotate_state_changed, m_frontend_config.rotate, m_frontend_config.rotate_enabled,
            dewarp_state_changed, m_frontend_config.dewarp_enabled);
        on_resource_change(EventType::STREAM_CONFIG, std::make_shared<webserver::resources::StreamConfigResourceState>(state));
    }
    WEBSERVER_LOG_INFO("Frontend config updated successfully");
}

void webserver::resources::FrontendResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    WEBSERVER_LOG_INFO("Registering HTTP endpoints for FrontendResource");
    srv->Get("/frontend", std::function<nlohmann::json()>([this]()
                                                          { 
                                                              WEBSERVER_LOG_INFO("GET /frontend called");
                                                              return this->get_frontend_config(); }));

    srv->Patch("/frontend", [this](const nlohmann::json &partial_config)
               {
                   WEBSERVER_LOG_INFO("PATCH /frontend called");
                   m_config.merge_patch(partial_config); 
                   update_json_config();
                   update_frontend_config();
                   auto result = this->m_config;
                   auto state = FrontendResourceState(this->to_string(), m_frontend_config);
                   on_resource_change(EventType::CHANGED_RESOURCE_FRONTEND, std::make_shared<webserver::resources::FrontendResource::FrontendResourceState>(state));
                   WEBSERVER_LOG_INFO("PATCH /frontend completed");
                   return result; });

    srv->Put("/frontend", [this](const nlohmann::json &config)
             {
                 WEBSERVER_LOG_INFO("PUT /frontend called");
                 auto partial_config = nlohmann::json::diff(m_config, config);
                 m_config = m_config.patch(partial_config);
                 update_json_config();
                 update_frontend_config();
                 auto result = this->m_config;
                 on_resource_change(EventType::CHANGED_RESOURCE_FRONTEND, std::make_shared<webserver::resources::FrontendResource::FrontendResourceState>(FrontendResourceState(this->to_string(), m_frontend_config)));
                 WEBSERVER_LOG_INFO("PUT /frontend completed");
                 return result; });

    srv->Post("/frontend/freeze", std::function<void(const nlohmann::json &)>([this](const nlohmann::json &j_body)
                                                                              {
                                                                                  WEBSERVER_LOG_INFO("POST /frontend/freeze called");
                                                                                  update_json_config();
                                                                                  m_frontend_config.freeze_state_changed = m_frontend_config.freeze != j_body["value"];
                                                                                  m_frontend_config.freeze = j_body["value"];
                                                                                  on_resource_change(EventType::CHANGED_RESOURCE_FRONTEND, std::make_shared<webserver::resources::FrontendResource::FrontendResourceState>(FrontendResourceState(this->to_string(), m_frontend_config)));
                                                                                  WEBSERVER_LOG_INFO("POST /frontend/freeze completed");
                                                                              }));
    WEBSERVER_LOG_INFO("HTTP endpoints registered successfully");
}

void webserver::resources::FrontendResource::update_json_config()
{
    WEBSERVER_LOG_INFO("Updating JSON config");
    m_config["denoise"] = m_ai_resource->get_ai_config(AiResource::AiApplications::AI_APPLICATION_DENOISE);
    m_config["hdr"] = m_isp_resource->get_hdr_config();
    WEBSERVER_LOG_INFO("JSON config updated successfully");
}

nlohmann::json webserver::resources::FrontendResource::get_frontend_config()
{
    WEBSERVER_LOG_INFO("Getting frontend config");
    update_json_config();
    return m_config;
}
