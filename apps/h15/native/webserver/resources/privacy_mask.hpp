#pragma once
#include "common/resources.hpp"
#include "media_library/privacy_mask_types.hpp"
#include "media_library/privacy_mask.hpp"
#include "configs.hpp"

using namespace privacy_mask_types;

namespace webserver
{
    namespace resources
    {
        class PrivacyMaskResource : public Resource
        {
        public:
            class PrivacyMaskResourceState : public ResourceState
            {
            public:
                std::map<std::string, polygon> masks;
                std::vector<std::string> changed_to_enabled;
                std::vector<std::string> changed_to_disabled;
                std::vector<std::string> polygon_to_update;
                std::vector<std::string> polygon_to_delete;
                PrivacyMaskResourceState(std::map<std::string, polygon> masks, std::vector<std::string> changed_to_enabled, std::vector<std::string> changed_to_disabled, std::vector<std::string> polygon_to_update, std::vector<std::string> polygon_to_delete)
                    : masks(masks), changed_to_enabled(changed_to_enabled), changed_to_disabled(changed_to_disabled), polygon_to_update(polygon_to_update), polygon_to_delete(polygon_to_delete) {}
                PrivacyMaskResourceState(std::map<std::string, polygon> masks)
                    : masks(masks), changed_to_enabled({}), changed_to_disabled({}), polygon_to_update({}), polygon_to_delete({}) {}
            };

        private:
            std::map<std::string, polygon> m_privacy_masks;
            std::map<std::string, polygon> m_original_privacy_masks;
            StreamConfigResourceState::rotation_t m_rotation;
            bool m_global_enable;
            std::shared_ptr<PrivacyMaskResourceState> parse_state(std::vector<std::string> current_enabled, std::vector<std::string> prev_enabled, nlohmann::json diff);
            std::shared_ptr<PrivacyMaskResourceState> update_all_vertices_state();
            std::vector<std::string> get_enabled_masks();
            void parse_polygon(nlohmann::json j);
            std::shared_ptr<webserver::resources::PrivacyMaskResource::PrivacyMaskResourceState> delete_masks_from_config(nlohmann::json config);
            void reset_config() override;
            vertex rotate_point(const vertex &p, uint32_t new_width, uint32_t new_height, int angle_deg);

        public:
            PrivacyMaskResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<webserver::resources::ConfigResource> configs);
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "privacy_mask"; }
            ResourceType get_type() override { return ResourceType::RESOURCE_PRIVACY_MASK; }
            std::map<std::string, polygon> get_privacy_masks() { return m_privacy_masks; }
            void renable_masks();
        };
    }
}