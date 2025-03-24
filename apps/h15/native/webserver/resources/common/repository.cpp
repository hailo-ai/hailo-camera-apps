#include "repository.hpp"

WebserverResourceRepository webserver::resources::ResourceRepository::create(std::shared_ptr<HTTPServer> svr)
{
    auto event_bus = std::make_shared<EventBus>();
    std::vector<WebserverResource> resources_vec{};
    auto config_resource = std::make_shared<webserver::resources::ConfigResource>(event_bus);
    auto osd_resource = std::make_shared<webserver::resources::OsdResource>(event_bus, config_resource);
    auto ai_resource = std::make_shared<webserver::resources::AiResource>(event_bus, config_resource);
    auto isp_resource = std::make_shared<webserver::resources::IspResource>(event_bus, ai_resource, config_resource);
    auto frontend_resource = std::make_shared<webserver::resources::FrontendResource>(event_bus, ai_resource, isp_resource, config_resource);
    auto encoder_resource = std::make_shared<webserver::resources::EncoderResource>(event_bus, config_resource);
    auto privacy_mask_resource = std::make_shared<webserver::resources::PrivacyMaskResource>(event_bus, config_resource);
    auto webpage_resource = std::make_shared<webserver::resources::WebpageResource>(event_bus);
    auto webrtc_resource = std::make_shared<webserver::resources::WebRtcResource>(event_bus, config_resource);
    resources_vec.push_back(config_resource);
    resources_vec.push_back(ai_resource);
    resources_vec.push_back(isp_resource);
    resources_vec.push_back(frontend_resource);
    resources_vec.push_back(osd_resource);
    resources_vec.push_back(encoder_resource);
    resources_vec.push_back(privacy_mask_resource);
    resources_vec.push_back(webpage_resource);
    resources_vec.push_back(webrtc_resource);

    return std::make_shared<webserver::resources::ResourceRepository>(resources_vec, event_bus, svr);
}

webserver::resources::ResourceRepository::ResourceRepository(std::vector<WebserverResource> resources, std::shared_ptr<EventBus> event_bus, std::shared_ptr<HTTPServer> srv) : m_resources(std::move(resources)), m_event_bus(event_bus)
{
    register_resources(srv);
}

void webserver::resources::ResourceRepository::register_resources(std::shared_ptr<HTTPServer> svr)
{
    for (auto resource : m_resources)
    {
        resource->http_register(svr);
    }
}
