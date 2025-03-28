#pragma once
#include "common/resources.hpp"
#include "osd.hpp"
#include "configs.hpp"

namespace webserver
{
    namespace resources
    {
        class OsdResource : public Resource
        {
        public:
            template <typename T>
            class OsdResourceConfig
            {
                static_assert(std::is_base_of<osd::Overlay, T>::value, 
                  "T must be derived from BaseTextOverlay");
                private:
                    bool enabled;
                    T overlay;
                public:
                    OsdResourceConfig(bool enabled, nlohmann::json params) : enabled(enabled){
                        osd::from_json(params, overlay);
                    }
                    T get_overlay() { return overlay; }
                    bool get_enabled() { return enabled; }
            };

            class OsdResourceState : public ResourceState
            {
                public:
                    std::vector<std::string> overlays_to_delete;
                    std::vector<OsdResourceConfig<osd::TextOverlay>> text_overlays;
                    std::vector<OsdResourceConfig<osd::ImageOverlay>> image_overlays;
                    std::vector<OsdResourceConfig<osd::DateTimeOverlay>> datetime_overlays;
                    OsdResourceState(nlohmann::json config, std::vector<std::string> overlays_ids);
            };
            OsdResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<ConfigResource> configs);
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "osd"; }
            ResourceType get_type() override { return ResourceType::RESOURCE_OSD; }
            nlohmann::json get_encoder_osd_config();
            std::vector<std::string> get_overlays_to_delete(nlohmann::json previouse_config, nlohmann::json new_config);
            nlohmann::json from_medialib_config_to_osd_config(nlohmann::json config);
        private:
            void reset_config() override;
            nlohmann::json map_paths(nlohmann::json config);
            nlohmann::json unmap_paths(nlohmann::json config);

        };
    }
}