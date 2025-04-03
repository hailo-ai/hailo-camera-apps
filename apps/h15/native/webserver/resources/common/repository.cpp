#include "repository.hpp"

using namespace webserver::resources;

WebserverResourceRepository ResourceRepository::create(std::shared_ptr<HTTPServer> svr, std::string config_path)
{
    auto event_bus = std::make_shared<EventBus>();

    std::shared_ptr<ConfigResourceBase> config_resource = std::make_shared<ConfigResource>(event_bus);
    if (use_cpp_api()){
        config_resource = std::make_shared<ConfigResourceMedialib>(event_bus, config_path);
    }
    auto osd_resource = std::make_shared<OsdResource>(event_bus, config_resource);
    auto ai_resource = std::make_shared<AiResource>(event_bus, config_resource);
    auto isp_resource = std::make_shared<IspResource>(event_bus, ai_resource, config_resource);
    auto frontend_resource = std::make_shared<FrontendResource>(event_bus, ai_resource, isp_resource, config_resource);
    auto encoder_resource = std::make_shared<EncoderResource>(event_bus, config_resource);
    auto privacy_mask_resource = std::make_shared<PrivacyMaskResource>(event_bus, config_resource);
    auto webpage_resource = std::make_shared<WebpageResource>(event_bus);
    auto webrtc_resource = std::make_shared<WebRtcResource>(event_bus, config_resource);

    std::vector<WebserverResource> resources_vec{};
    resources_vec.push_back(config_resource);
    resources_vec.push_back(ai_resource);
    resources_vec.push_back(isp_resource);
    resources_vec.push_back(frontend_resource);
    resources_vec.push_back(osd_resource);
    resources_vec.push_back(encoder_resource);
    resources_vec.push_back(privacy_mask_resource);
    resources_vec.push_back(webpage_resource);
    resources_vec.push_back(webrtc_resource);

    return std::make_shared<ResourceRepository>(resources_vec, event_bus, svr);
}

WebserverResourceRepository ResourceRepository::create(std::shared_ptr<HTTPServer> svr)
{
    return create(svr, "");
}

ResourceRepository::ResourceRepository(std::vector<WebserverResource> resources, std::shared_ptr<EventBus> event_bus, std::shared_ptr<HTTPServer> srv) : m_resources(std::move(resources)), m_event_bus(event_bus)
{
    register_resources(srv);
}

void ResourceRepository::register_resources(std::shared_ptr<HTTPServer> svr)
{
    for (auto resource : m_resources)
    {
        resource->http_register(svr);
    }
}
