#include "cpp_pipeline.hpp"


using namespace webserver::pipeline;
using namespace webserver::resources;

void CppPipeline::callback_handle_profile_switch(ResourceStateChangeNotification notif){
    WEBSERVER_LOG_DEBUG("Pipeline: Handling switch profile event");
    auto state = std::static_pointer_cast<ConfigResourceMedialib::ProfileResourceState>(notif.resource_state);
    std::string profile_name = state->profile_name;
    auto res = m_app_resources->media_library->set_profile(profile_name);
    if (res != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to switch profile: {} error: {}", profile_name, res);
        throw std::runtime_error("Failed to switch profile: " + profile_name);
    }
    WEBSERVER_LOG_DEBUG("Pipeline: Switch profile event handled");
}

hailo_encoder_config_t CppPipeline::get_encoder_config(){
    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    ProfileConfig current_profile = expected_profile.value();

    // encoder_config_t encoder_config = current_profile.m_encoders[STREAM_4K];
    encoder_config_t encoder_config = m_app_resources->media_library->m_encoders[STREAM_4K]->get_config();//TODO get the config form the profile(mosko need to be updated from the real struct)
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder_config))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    return std::get<hailo_encoder_config_t>(encoder_config);
}

void CppPipeline::callback_handle_encoder(ResourceStateChangeNotification notif){
    WEBSERVER_LOG_DEBUG("Pipeline: Handling encoder resource state change");
    auto state = std::static_pointer_cast<EncoderResource::EncoderResourceState>(notif.resource_state);
    
    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    ProfileConfig current_profile = expected_profile.value();

    encoder_config_t encoder_config = m_app_resources->media_library->m_encoders[STREAM_4K]->get_config();
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder_config))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
    state->control.fill_encoder_element_config(hailo_encoder_config);

    current_profile.encoder_configs[STREAM_4K] = hailo_encoder_config;

    if(m_app_resources->media_library->set_override_profile(current_profile) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to set profile");
        throw std::runtime_error("Failed to set profile");
    }
    WEBSERVER_LOG_DEBUG("Pipeline: Encoder resource state change handled");
}

void CppPipeline::callback_handle_frontend(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling frontend resource state change");
    auto state = std::static_pointer_cast<FrontendResource::FrontendResourceState>(notif.resource_state);
    if (state->control.freeze_state_changed)
    {
        WEBSERVER_LOG_DEBUG("Pipeline: Frontend freeze state changed");
        m_app_resources->media_library->m_frontend->set_freeze(state->control.freeze);
    }

    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    ProfileConfig current_profile = expected_profile.value();
    update_profile_config_frontend(state->config, current_profile);

    if(m_app_resources->media_library->set_override_profile(current_profile) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to set profile");
        throw std::runtime_error("Failed to set profile");
    }

    WEBSERVER_LOG_DEBUG("Pipeline: Setting frontend config-string");
    WEBSERVER_LOG_DEBUG("Pipeline: Frontend resource state change handled");
}

void CppPipeline::update_profile_config_frontend(const std::string &frontend_conf, ProfileConfig &profile_config){
    ConfigManager frontend_config_schema(ConfigSchema::CONFIG_SCHEMA_FRONTEND);
    frontend_config_t frontend_config;
    frontend_config_schema.config_string_to_struct<frontend_config_t>(frontend_conf, frontend_config);

    profile_config.multi_resize_config = frontend_config.multi_resize_config;
    profile_config.ldc_config = frontend_config.ldc_config;
    profile_config.hailort_config = frontend_config.hailort_config;
    profile_config.isp_config = frontend_config.isp_config;
    profile_config.hdr_config = frontend_config.hdr_config;
    profile_config.denoise_config = frontend_config.denoise_config;
    profile_config.input_config = frontend_config.input_config;
}

void CppPipeline::callback_handle_stream_config(ResourceStateChangeNotification notif){
    WEBSERVER_LOG_DEBUG("Pipeline: Handling stream config resource state change");
    auto state = std::static_pointer_cast<StreamConfigResourceState>(notif.resource_state);
    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    ProfileConfig current_profile = expected_profile.value();
    if (state->resolutions[0].framerate_changed)
    {
        current_profile.multi_resize_config.application_input_streams_config.resolutions[0].framerate = state->resolutions[0].framerate;//TODO need to solve the magic number 0 when i will have more streams
    }

    if (state->resolutions[0].stream_size_changed)
    {
        current_profile.multi_resize_config.application_input_streams_config.resolutions[0].dimensions.destination_width = state->resolutions[0].width;
        current_profile.multi_resize_config.application_input_streams_config.resolutions[0].dimensions.destination_height = state->resolutions[0].height;
    }

    if (state->rotate_enabled){
        current_profile.multi_resize_config.rotation_config.enabled = state->rotate_enabled;
        current_profile.multi_resize_config.rotation_config.angle = state->rotation;
    }
    //TODO the next part break architecture, need to be fixed
    encoder_config_t encoder_config = m_app_resources->media_library->m_encoders[STREAM_4K]->get_config();
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder_config))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);

    auto encoder_resource = std::static_pointer_cast<EncoderResource>(m_resources->get(RESOURCE_ENCODER));
    encoder_resource->fill_encoder_element_config(hailo_encoder_config);

    current_profile.encoder_configs[STREAM_4K] = hailo_encoder_config;

    if(m_app_resources->media_library->set_override_profile(current_profile) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to set profile");
        throw std::runtime_error("Failed to set profile");
    }

    WEBSERVER_LOG_DEBUG("Pipeline: Stream config resource state change handled");
}

std::shared_ptr<osd::Blender> CppPipeline::get_osd_blender()
{
    return m_app_resources->media_library->m_encoders[STREAM_4K]->get_blender();
}

std::shared_ptr<PrivacyMaskBlender> CppPipeline::get_privacy_blender()
{
    return m_app_resources->media_library->m_frontend->get_privacy_mask_blender();
}
