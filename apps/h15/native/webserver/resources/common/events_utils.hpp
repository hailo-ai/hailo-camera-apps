
#pragma once
#include <nlohmann/json.hpp>
#include <memory>
#include <vector>
#include <string>
#include <functional>
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
                                                    {RESET_CONFIG, "reset_config"}});

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
            enum rotation_t
            {
                ROTATION_0 = 0,
                ROTATION_90 = 90,
                ROTATION_180 = 180,
                ROTATION_270 = 270
            };

            struct Resolution
            {
                uint32_t width;
                uint32_t height;
                uint32_t framerate;
                bool framerate_changed;
                bool stream_size_changed;
            };

            rotation_t rotation;
            bool rotate_enabled;
            std::vector<Resolution> resolutions;

            StreamConfigResourceState() = default;

            StreamConfigResourceState(std::vector<Resolution> resolutions, const std::string &rotation, bool rotate_enabled)
                : rotate_enabled(rotate_enabled), resolutions(std::move(resolutions))
            {
                if (rotation == "ROTATION_ANGLE_0")
                {
                    this->rotation = ROTATION_0;
                }
                else if (rotation == "ROTATION_ANGLE_90")
                {
                    this->rotation = ROTATION_90;
                }
                else if (rotation == "ROTATION_ANGLE_180")
                {
                    this->rotation = ROTATION_180;
                }
                else if (rotation == "ROTATION_ANGLE_270")
                {
                    this->rotation = ROTATION_270;
                }
                else
                {
                    throw std::invalid_argument("Invalid rotation angle: " + rotation);
                }
            }
        };

        using ResourceChangeCallback = std::function<void(ResourceStateChangeNotification)>;

    }
}
