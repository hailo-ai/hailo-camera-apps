#pragma once
#include "common/resources.hpp"

namespace webserver
{
    namespace resources
    {
    class ConfigResourceBase : public Resource
    {
    protected:
        nlohmann::json m_frontend_default_config;
        nlohmann::json m_encoder_osd_default_config;
    public:
        ConfigResourceBase(std::shared_ptr<EventBus> event_bus) : Resource(event_bus) {}
        virtual ~ConfigResourceBase() = default;
        std::string name() override { return "config"; }
        ResourceType get_type() override { return ResourceType::RESOURCE_CONFIG_MANAGER; }
        nlohmann::json get_frontend_default_config();
        nlohmann::json get_encoder_default_config();
        nlohmann::json get_osd_default_config();
        nlohmann::json get_osd_and_encoder_default_config();
        nlohmann::json get_hdr_default_config();
        nlohmann::json get_denoise_default_config();
    };
    class ConfigResource : public ConfigResourceBase
    {
    private:

    public:
        ConfigResource(std::shared_ptr<EventBus> event_bus);
        ~ConfigResource() = default;
        void http_register(std::shared_ptr<HTTPServer> srv) override;
    };
    

    class ConfigResourceMedialib : public ConfigResourceBase
            {
            private:
                std::string m_default_profile_name;
                std::string m_current_profile_name;
                nlohmann::json m_profile;
                nlohmann::json m_medialib_config;

                tl::expected<nlohmann::json, std::string> load_config_from_file(const std::string &file_path);
                tl::expected<nlohmann::json, std::string> enable_gyro_if_exist(nlohmann::json profile);
                tl::expected<nlohmann::json, std::string> get_profile(nlohmann::json profile_name);
                tl::expected<nlohmann::json, std::string> extract_frontend_config();
                tl::expected<nlohmann::json, std::string> extract_encoder_config();
                tl::expected<void, std::string> extract_profile_data(const std::string &profile_name);
                tl::expected<void, std::string> switch_profile(const std::string &profile_name);
                void reset_config() override;


            public:
                class ProfileResourceState : public ResourceState
                {
                public:
                    std::string profile_name;
                    ProfileResourceState(std::string profile_name) : profile_name(profile_name) {}
                };

                ConfigResourceMedialib(std::shared_ptr<EventBus> event_bus, std::string config_path);
                ~ConfigResourceMedialib() = default;
                std::string name() override { return "medialib config"; }
                void http_register(std::shared_ptr<HTTPServer> srv) override;
                nlohmann::json get_current_profile(){ return m_profile; }
                nlohmann::json get_current_medialib_config(){ return m_medialib_config; }
            };
    };
}
