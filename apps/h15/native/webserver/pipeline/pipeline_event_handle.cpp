#include "pipeline.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;

void CppPipeline::callback_handle_update_profile(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling update profile event");
    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();
    WEBSERVER_LOG_INFO("Pipeline: got event named {}", nlohmann::json(notif.event_type).dump());
    std::visit(
        [&](auto &&state) {
            using T = std::decay_t<decltype(state)>;
            if constexpr (std::is_same_v<T, std::shared_ptr<ProfileFPSState>>)
            {
                update_fps(state->value, current_profile);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileResolutionState>>)
            {
                update_resolution(state->value, current_profile);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileFlipState>>)
            {
                update_flip(state->value, current_profile);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileRotationState>>)
            {
                update_rotation(state->value, current_profile);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileDewarpState>>)
            {
                WEBSERVER_LOG_INFO("Updating dewarp to {}", state->value);
                current_profile.iq_settings.dewarp.enabled = state->value;
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileFreezeState>>)
            {
                WEBSERVER_LOG_INFO("Updating freeze to {}", state->value);
                m_app_resources->freeze_stage->set_freeze(state->value);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileValveState>>)
            {
                WEBSERVER_LOG_INFO("Updating valve to {}", state->value);
                m_app_resources->valve_stage->set_valve(state->value);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileDisState>>)
            {
                if (current_profile.stabilizer_settings.eis.enabled)
                {
                    WEBSERVER_LOG_ERROR("Cannot set DIS when EIS is enabled");
                    throw std::runtime_error("Cannot set DIS when EIS is enabled");
                }
                WEBSERVER_LOG_INFO("Updating dis to {}", state->value);
                current_profile.stabilizer_settings.dis.enabled = state->value;
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileEisState>>)
            {
                if (current_profile.stabilizer_settings.dis.enabled)
                {
                    WEBSERVER_LOG_ERROR("Cannot set EIS when DIS is enabled");
                    throw std::runtime_error("Cannot set EIS when DIS is enabled");
                }
                WEBSERVER_LOG_INFO("Updating eis to {}", state->value);
                current_profile.stabilizer_settings.eis.enabled = state->value;
                current_profile.stabilizer_settings.gyro.enabled = state->value;
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileDigitalZoomState>>)
            {
                update_zoom(state, current_profile);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileDigitalZoomRoiState>>)
            {
                update_zoom_roi(state, current_profile);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<ProfileGrayscaleState>>)
            {
                current_profile.iq_settings.grayscale.enabled = state->value;
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<DetectionState>>)
            {
                m_app_resources->overlay_stage->set_skip(!state->value);
            }
            else
            {
                WEBSERVER_LOG_ERROR("Unknown state type");
                throw std::runtime_error("Unknown state type");
            }
        },
        notif.resource_state);

    if (m_app_resources->media_library->set_override_parameters(current_profile) !=
        media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to set profile");
        throw std::runtime_error("Failed to set profile");
    }
}

void CppPipeline::update_zoom(std::shared_ptr<ProfileDigitalZoomState> state, config_profile_t &profile_config)
{
    WEBSERVER_LOG_INFO("Updating zoom to {}", state->getMagnification());
    profile_config.application_settings.digital_zoom.enabled = state->getEnable();
    profile_config.application_settings.digital_zoom.mode = digital_zoom_mode_t::DIGITAL_ZOOM_MODE_MAGNIFICATION;
    profile_config.application_settings.digital_zoom.magnification = state->getMagnification();
}

void CppPipeline::update_zoom_roi(std::shared_ptr<ProfileDigitalZoomRoiState> state, config_profile_t &profile_config)
{
    WEBSERVER_LOG_INFO("Updating relative zoom roi to x: {}, y: {}, width: {}, height: {}", state->getX(),
                       state->getY(), state->getWidth(), state->getHeight());
    // NOTE: user have to know the internal resolution that get into dewarp
    auto width = profile_config.sensor_config.input_video.resolution.width;
    auto height = profile_config.sensor_config.input_video.resolution.height;

    rotation_angle_t angle = profile_config.application_settings.rotation.angle;
    if (isPortrait(angle))
    {
        std::swap(width, height);
    }
    profile_config.application_settings.digital_zoom.enabled = state->getEnable();
    profile_config.application_settings.digital_zoom.mode = digital_zoom_mode_t::DIGITAL_ZOOM_MODE_ROI;
    profile_config.application_settings.digital_zoom.magnification = state->getMagnification();
    profile_config.application_settings.digital_zoom.roi.x = relative_to_absolut(state->getX(), width);
    profile_config.application_settings.digital_zoom.roi.y = relative_to_absolut(state->getY(), height);
    profile_config.application_settings.digital_zoom.roi.width = relative_to_absolut(state->getWidth(), width);
    profile_config.application_settings.digital_zoom.roi.height = relative_to_absolut(state->getHeight(), height);
    WEBSERVER_LOG_INFO("Updating absolute values zoom roi to x: {}, y: {}, width: {}, height: {}",
                       profile_config.application_settings.digital_zoom.roi.x,
                       profile_config.application_settings.digital_zoom.roi.y,
                       profile_config.application_settings.digital_zoom.roi.width,
                       profile_config.application_settings.digital_zoom.roi.height);
}

int CppPipeline::relative_to_absolut(float position, uint32_t resolution_axis_size)
{
    if (position > 1 || position < 0)
    {
        WEBSERVER_LOG_ERROR("position {} not between 0 and 1", position);
        throw std::runtime_error("position " + std::to_string(position) + " not between 0 and 1");
    }
    return static_cast<int>(static_cast<float>(position) * static_cast<float>(resolution_axis_size));
}

float CppPipeline::absolut_to_relative(int position, uint32_t resolution_axis_size)
{
    if (position > static_cast<int>(resolution_axis_size) || position < 0)
    {
        WEBSERVER_LOG_ERROR("position {} not between 0 and {}", position, resolution_axis_size);
        throw std::runtime_error("position " + std::to_string(position) + " not between 0 and " +
                                 std::to_string(resolution_axis_size));
    }
    return static_cast<float>(position) / static_cast<float>(resolution_axis_size);
}

int CppPipeline::scale(int position, int old_size, int new_size)
{
    if (position > old_size || position < 0)
    {
        WEBSERVER_LOG_ERROR("position {} not between 0 and {}", position, old_size);
        throw std::runtime_error("position " + std::to_string(position) + " not between 0 and " +
                                 std::to_string(old_size));
    }
    return relative_to_absolut(absolut_to_relative(position, old_size), new_size);
}

// TODO make sure FPS is not reseting stream
void CppPipeline::update_fps(uint32_t fps, config_profile_t &profile_config)
{
    if (fps < 1 || fps > 30)
    {
        WEBSERVER_LOG_ERROR("Framerate out of range: {}", fps);
        throw std::runtime_error("Framerate out of range");
    }
    for (auto &resolution : profile_config.application_settings.application_input_streams.resolutions)
    {
        resolution.framerate = fps;
    }
    // NOTE: waiting for encoder api from mosko
    encoder_config_t encoder = m_app_resources->media_library->m_encoders[STREAM_4K]->get_config();
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder);
    hailo_encoder_config.input_stream.framerate = fps;
}

void CppPipeline::update_resolution(const std::string &resolution, config_profile_t &profile_config)
{
    WEBSERVER_LOG_INFO("Updating resolution to {}", resolution);
    // NOTE: waiting for encoder api from mosko
    encoder_config_t encoder = profile_config.to_encoded_output_stream_config_map()[STREAM_4K].encoding;
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    uint32_t width = webserver::common::resolution_map.at(string_to_resolution(resolution)).first;
    uint32_t height = webserver::common::resolution_map.at(string_to_resolution(resolution)).second;

    rotation_angle_t angle = profile_config.application_settings.rotation.angle;
    if (isPortrait(angle))
    {
        std::swap(width, height);
    }
    // update profile
    profile_config.application_settings.application_input_streams.resolutions[0].dimensions.destination_width = width;
    profile_config.application_settings.application_input_streams.resolutions[0].dimensions.destination_height = height;
    // clip zoom if needed
    auto &zoom_roi = profile_config.application_settings.digital_zoom.roi;
    if (zoom_roi.x > width)
    {
        WEBSERVER_LOG_INFO("Zoom ROI x {} is greater than width {}, clipping to width - 1", zoom_roi.x, width);
        zoom_roi.x = width - 1;
    }
    if (zoom_roi.y > height)
    {
        WEBSERVER_LOG_INFO("Zoom ROI y {} is greater than height {}, clipping to height - 1", zoom_roi.y, height);
        zoom_roi.y = height - 1;
    }
    if (zoom_roi.width + zoom_roi.x > width)
    {
        WEBSERVER_LOG_INFO("Zoom ROI width {} + x {} is greater than width {}, clipping to width - x", zoom_roi.width,
                           zoom_roi.x, width);
        zoom_roi.width = width - zoom_roi.x;
    }
    if (zoom_roi.height + zoom_roi.y > height)
    {
        WEBSERVER_LOG_INFO("Zoom ROI height {} + y {} is greater than height {}, clipping to height - y",
                           zoom_roi.height, zoom_roi.y, height);
        zoom_roi.height = height - zoom_roi.y;
    }

    // update encoder
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder);
    hailo_encoder_config.input_stream.width = width;
    hailo_encoder_config.input_stream.height = height;
    // NOTE: waiting for encoder api from mosko
    profile_config.encoded_output_streams[0].encoding = hailo_encoder_config;
}

void CppPipeline::update_flip(const std::string &flip, config_profile_t &profile_config)
{
    WEBSERVER_LOG_INFO("Updating flip to {}", flip);
    if (flip_string_map.find(flip) == flip_string_map.end())
    {
        WEBSERVER_LOG_ERROR("Invalid flip direction: {}", flip);
        throw std::runtime_error("Invalid flip direction: " + flip);
    }
    profile_config.application_settings.flip.enabled =
        (flip_string_map.at(flip) != flip_direction_t::FLIP_DIRECTION_NONE);
    profile_config.application_settings.flip.direction = flip_string_map.at(flip);
}

void CppPipeline::update_rotation(const std::string &rotation, config_profile_t &profile_config)
{
    WEBSERVER_LOG_INFO("Updating rotation to {}", rotation);
    if (rotation_string_map.find(rotation) == rotation_string_map.end())
    {
        WEBSERVER_LOG_ERROR("Invalid rotation angle: {}", rotation);
        throw std::runtime_error("Invalid rotation angle: " + rotation);
    }
    rotation_angle_t angle = profile_config.application_settings.rotation.angle;
    if (angle != rotation_string_map.at(rotation))
    {
        WEBSERVER_LOG_DEBUG("Rotation angle changed from {} to {}", angle, rotation);
    }
    // handle detection
    if (rotation_string_map.at(rotation) != rotation_angle_t::ROTATION_ANGLE_0)
    {
        m_app_resources->overlay_stage->set_skip(true);
    }
    // CONFIGURE ENCODER
    // NOTE: waiting for encoder api from mosko
    //  encoder_config_t& encoder = profile_config.encoder_configs[STREAM_4K];
    encoder_config_t encoder = m_app_resources->media_library->m_encoders[STREAM_4K]->get_config();
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder);
    auto currentAngle = rotation_string_map.at(rotation);
    auto profileAngle = angle;

    if (isPortrait(currentAngle) != isPortrait(profileAngle))
    {
        std::swap(hailo_encoder_config.input_stream.width, hailo_encoder_config.input_stream.height);
    }
    // NOTE: waiting for encoder api from mosko
    profile_config.encoded_output_streams[0].encoding = hailo_encoder_config;

    // CONFIGURE FRONTEND
    // TODO getting encoder from public encoder and putting it to profile is need to be fixed somehow by mosko
    bool enable = (rotation_string_map.at(rotation) != rotation_angle_t::ROTATION_ANGLE_0);
    profile_config.application_settings.rotation.enabled = enable;
    profile_config.application_settings.rotation.angle = rotation_string_map.at(rotation);
}

void CppPipeline::callback_handle_profile_switch(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_INFO("Pipeline: Handling switch profile event");
    if (!std::holds_alternative<std::shared_ptr<ProfileNameState>>(notif.resource_state))
    {
        WEBSERVER_LOG_ERROR("Failed to cast resource state to ProfileNameState");
        throw std::runtime_error("Failed to cast resource state to ProfileNameState");
    }
    auto &state = std::get<std::shared_ptr<ProfileNameState>>(notif.resource_state);
    std::string profile_name = state->value;
    auto res = m_app_resources->media_library->set_profile(profile_name);
    if (res != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to switch profile: {} error: {}", profile_name, res);
        throw std::runtime_error("Failed to switch profile: " + profile_name);
    }
    WEBSERVER_LOG_DEBUG("Pipeline: Switch profile event handled");
}

hailo_encoder_config_t CppPipeline::get_encoder_config()
{
    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();

    // NOTE: waiting for encoder api from mosko
    //  encoder_config_t encoder_config = current_profile.m_encoders[STREAM_4K];
    encoder_config_t encoder_config =
        m_app_resources->media_library->m_encoders[STREAM_4K]
            ->get_config(); // TODO get the config form the profile(mosko need to be updated from the real struct)
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder_config))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    return std::get<hailo_encoder_config_t>(encoder_config);
}

void CppPipeline::callback_handle_encoder(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling encoder resource state change");
    auto state = notif.getResourceStateFromBase<EncoderResource::EncoderResourceState>();

    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();

    encoder_config_t encoder_config = m_app_resources->media_library->m_encoders[STREAM_4K]->get_config();
    if (std::holds_alternative<jpeg_encoder_config_t>(encoder_config))
    {
        WEBSERVER_LOG_CRITICAL("JPEG encoder config is not supported in webserver");
        throw std::runtime_error("JPEG encoder config is not supported in webserver");
    }
    hailo_encoder_config_t &hailo_encoder_config = std::get<hailo_encoder_config_t>(encoder_config);
    state->value.fill_encoder_element_config(hailo_encoder_config);

    current_profile.encoded_output_streams[0].encoding = hailo_encoder_config;

    if (m_app_resources->media_library->set_override_parameters(current_profile) !=
        media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to set profile");
        throw std::runtime_error("Failed to set profile");
    }
    WEBSERVER_LOG_DEBUG("Pipeline: Encoder resource state change handled");
}

std::shared_ptr<osd::Blender> CppPipeline::get_osd_blender()
{
    return m_app_resources->media_library->m_encoders[STREAM_4K]->get_osd_blender();
}

std::shared_ptr<PrivacyMaskBlender> CppPipeline::get_privacy_blender()
{
    return m_app_resources->media_library->m_encoders[STREAM_4K]->get_privacy_mask_blender();
}

void CppPipeline::callback_handle_privacy_mask(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_DEBUG("Pipeline: Handling privacy mask resource state change");
    auto state = notif.getResourceStateFromBase<PrivacyMaskResource::PrivacyMaskResourceState>();
    auto masks = state->masks;
    // construct prcvacy type
    std::vector<std::string> active_masks = state->changed_to_enabled;
    active_masks.insert(active_masks.end(), state->polygon_to_update.begin(), state->polygon_to_update.end());
    std::vector<polygon> active_polygons;
    for (const auto &id : active_masks)
    {
        if (masks.find(id) != masks.end())
        {
            active_polygons.push_back(masks[id]);
        }
    }

    static_privacy_mask_config_t privacy_mask_config;
    privacy_mask_config.enabled = !active_polygons.empty();
    privacy_mask_config.masks = active_polygons;

    auto expected_profile = m_app_resources->media_library->get_current_profile();
    if (!expected_profile.has_value())
    {
        WEBSERVER_LOG_ERROR("Failed to get current profile");
        throw std::runtime_error("Failed to get current profile");
    }
    config_profile_t current_profile = expected_profile.value();

    current_profile.encoded_output_streams[0].masking.static_privacy_mask_config = privacy_mask_config;
    if (m_app_resources->media_library->set_override_parameters(current_profile) !=
        media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        WEBSERVER_LOG_ERROR("Failed to set profile");
        throw std::runtime_error("Failed to set profile");
    }
    WEBSERVER_LOG_DEBUG("Pipeline: Privacy mask resource state change handled");
}
