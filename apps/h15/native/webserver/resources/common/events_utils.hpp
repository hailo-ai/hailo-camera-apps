#pragma once
#include <nlohmann/json.hpp>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <stdexcept>
#include "media_library/media_library_types.hpp"

namespace webserver
{
    namespace resources
    {
        enum EventPriority
        {
            HIGH = 0,
            MEDIUM = 1,
            LOW = 2
        };
        enum EventType
        {
            CHANGED_RESOURCE_WEBPAGE,
            CHANGED_RESOURCE_CONFIG_MANAGER,
            CHANGED_RESOURCE_FRONTEND,
            CHANGED_RESOURCE_ENCODER,
            CHANGED_RESOURCE_OSD,
            CHANGED_RESOURCE_AI,
            CHANGED_RESOURCE_ISP,
            CHANGED_RESOURCE_PRIVACY_MASK,
            CHANGED_RESOURCE_WEBRTC,
            STREAM_CONFIG,
            RESTART_FRONTEND,
            CODEC_CHANGE,
            RESET_CONFIG,
            SWITCH_PROFILE,
        };

        NLOHMANN_JSON_SERIALIZE_ENUM(EventType, {{CHANGED_RESOURCE_WEBPAGE, "webpage"},
                                                    {CHANGED_RESOURCE_FRONTEND, "frontend"},
                                                    {CHANGED_RESOURCE_ENCODER, "encoder"},
                                                    {CHANGED_RESOURCE_OSD, "osd"},
                                                    {CHANGED_RESOURCE_AI, "ai"},
                                                    {CHANGED_RESOURCE_ISP, "isp"},
                                                    {CHANGED_RESOURCE_PRIVACY_MASK, "privacy_mask"},
                                                    {CHANGED_RESOURCE_CONFIG_MANAGER, "config"},
                                                    {CHANGED_RESOURCE_WEBRTC, "webrtc"},
                                                    {STREAM_CONFIG, "stream_config"},
                                                    {RESTART_FRONTEND, "restart_frontend"},
                                                    {CODEC_CHANGE, "codec_change"},
                                                    {RESET_CONFIG, "reset_config"},
                                                    {SWITCH_PROFILE, "switch_profile"}});

        class ResourceState
        {
        public:
            virtual ~ResourceState() = default;
        };

        template <typename T>
        class ValueResourceState : public ResourceState
        {
        public:
            std::string name;
            T value;
            ValueResourceState(std::string name, T value) : name(std::move(name)), value(std::move(value)) {}
        };

        class ResourceStateChangeNotification
        {
        public:
            EventType event_type;
            std::shared_ptr<ResourceState> resource_state;
        };

        class ConfigResourceState : public ResourceState
        {
        public:
            std::string config;
            explicit ConfigResourceState(std::string config) : config(std::move(config)) {}
        };

        class StreamConfigResourceState : public ResourceState
        {
        public:
            struct Resolution
            {
                uint32_t width;
                uint32_t height;
                uint32_t framerate;
                bool framerate_changed;
                bool stream_size_changed;
            };

            bool flip_state_changed;
            flip_direction_t flip;
            bool flip_enabled;
            bool rotate_state_changed;
            rotation_angle_t rotation;
            bool rotate_enabled;
            bool dewarp_state_changed;
            bool dewarp_enabled;
            std::vector<Resolution> resolutions;

            static flip_direction_t flip_string_to_enum(const std::string &flip)
            {
                if (flip == "FLIP_DIRECTION_NONE")
                {
                    return FLIP_DIRECTION_NONE;
                }
                else if (flip == "FLIP_DIRECTION_HORIZONTAL")
                {
                    return FLIP_DIRECTION_HORIZONTAL;
                }
                else if (flip == "FLIP_DIRECTION_VERTICAL")
                {
                    return FLIP_DIRECTION_VERTICAL;
                }
                else if (flip == "FLIP_DIRECTION_BOTH")
                {
                    return FLIP_DIRECTION_BOTH;
                }
                else
                {
                    throw std::invalid_argument("Invalid flip direction: " + flip);
                }
            }

            static rotation_angle_t rotation_string_to_enum(const std::string &rotation)
            {
                if (rotation == "ROTATION_ANGLE_0")
                {
                    return ROTATION_ANGLE_0;
                }
                else if (rotation == "ROTATION_ANGLE_90")
                {
                    return ROTATION_ANGLE_90;
                }
                else if (rotation == "ROTATION_ANGLE_180")
                {
                    return ROTATION_ANGLE_180;
                }
                else if (rotation == "ROTATION_ANGLE_270")
                {
                    return ROTATION_ANGLE_270;
                }
                else
                {
                    throw std::invalid_argument("Invalid rotation angle: " + rotation);
                }
            }

            StreamConfigResourceState() = default;

            StreamConfigResourceState(
                std::vector<Resolution> resolutions,
                bool flip_state_changed, const std::string &flip, bool flip_enabled,
                bool rotate_state_changed, const std::string &rotation, bool rotate_enabled,
                bool dewarp_state_changed, bool dewarp_enabled)
                : 
                  flip_state_changed(flip_state_changed),
                  flip(flip_string_to_enum(flip)),
                  flip_enabled(flip_enabled),
                  rotate_state_changed(rotate_state_changed),
                  rotation(rotation_string_to_enum(rotation)),
                  rotate_enabled(rotate_enabled),
                  dewarp_state_changed(dewarp_state_changed),
                  dewarp_enabled(dewarp_enabled),
                  resolutions(std::move(resolutions))
            { }

        };

        using ResourceChangeCallback = std::function<void(ResourceStateChangeNotification)>;

    }
}
