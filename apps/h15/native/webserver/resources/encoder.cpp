#include "encoder.hpp"
#include <iostream>

void webserver::resources::to_json(nlohmann::json &j, const webserver::resources::EncoderResource::encoder_control_t &b)
{
    WEBSERVER_LOG_INFO("Converting encoder_control_t to JSON");
    j = nlohmann::json{
            {"intra_pic_rate", b.intra_pic_rate},
            {"codec", b.codec_to_str.at(b.codec)},
            {"quantization", {
                {"rc_mode", b.rc_mode_to_str.at(b.quantization.rc_mode)},
                {"intra_qp_delta", b.quantization.intra_qp_delta},
                {"fixed_intra_qp", b.quantization.fixed_intra_qp},
                {"qp_hdr", b.quantization.qp_hdr},
            }},
            {"b_frames", {
                {"num_b_frames", b.b_frames.num_b_frames},
                {"b_frame_qp_delta", b.b_frames.b_frame_qp_delta}
            }}
        };
    if (!(b.quantization.rc_mode == webserver::resources::EncoderResource::bitrate_control_t::CQP))
    {
        if(!b.quantization.bitrate.has_value() || !b.quantization.qp_min.has_value() || !b.quantization.qp_max.has_value() || !b.quantization.picture_rc)
            WEBSERVER_LOG_ERROR("Encoder config does not have bitrate, qp_min, qp_max or picture_rc while rc_mode is not CQP");
        j["quantization"].merge_patch(nlohmann::json{
            {"bitrate", b.quantization.bitrate.value()},
            {"qp_min", b.quantization.qp_min.value()},
            {"qp_max", b.quantization.qp_max.value()}
        });
    }
    else if(b.quantization.bitrate.has_value() || b.quantization.qp_min.has_value() || b.quantization.qp_max.has_value() || b.quantization.picture_rc)
        WEBSERVER_LOG_ERROR("Encoder config have bitrate, qp_min, qp_max or picture_rc while rc_mode is not CQP");
}

void webserver::resources::from_json(const nlohmann::json &j, webserver::resources::EncoderResource::encoder_control_t &b)
{
    WEBSERVER_LOG_INFO("Converting JSON to encoder_control_t");
    b.intra_pic_rate = j.at("intra_pic_rate").get<uint32_t>();
    b.codec = b.str_to_codec.at(j.at("codec").get<std::string>());
    webserver::resources::EncoderResource::rate_control_t quantization;
    auto rc_mode = b.str_to_rc_mode.at(j.at("quantization").at("rc_mode").get<std::string>());
    if (rc_mode == webserver::resources::EncoderResource::bitrate_control_t::CQP)
        quantization.set_rate_control_off(j.at("quantization").at("intra_qp_delta").get<int32_t>(),
                                          j.at("quantization").at("fixed_intra_qp").get<uint32_t>(),
                                          j.at("quantization").at("qp_hdr").get<int32_t>());
    else
        quantization.set_rate_control_on(rc_mode,
                                         j.at("quantization").at("bitrate").get<uint32_t>(),
                                         j.at("quantization").at("qp_min").get<uint32_t>(),
                                         j.at("quantization").at("qp_max").get<uint32_t>(),
                                         j.at("quantization").at("intra_qp_delta").get<int32_t>(),
                                         j.at("quantization").at("fixed_intra_qp").get<uint32_t>(),
                                         j.at("quantization").at("qp_hdr").get<int32_t>());
    b.quantization = quantization;
    b.b_frames.num_b_frames = j.at("b_frames").at("num_b_frames").get<uint32_t>();
    b.b_frames.b_frame_qp_delta = j.at("b_frames").at("b_frame_qp_delta").get<uint32_t>();
    if (!b.quantization.intra_qp_delta == 0 && !b.quantization.fixed_intra_qp == 0)
    {
        WEBSERVER_LOG_ERROR("Encoder config: intra_qp_delta or fixed_intra_qp is 0");
    }
}

webserver::resources::EncoderResource::EncoderResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<webserver::resources::ConfigResourceBase> configs) : Resource(event_bus)
{
    WEBSERVER_LOG_INFO("Initializing EncoderResource");
    m_default_config = configs->get_encoder_default_config().dump(4);
    reset_config();
    subscribe_callback(STREAM_CONFIG, [this](ResourceStateChangeNotification notification)
                        {
        WEBSERVER_LOG_INFO("Received STREAM_CONFIG notification");
        auto state = std::static_pointer_cast<StreamConfigResourceState>(notification.resource_state);

        if (state->resolutions[0].framerate_changed){
            m_encoder_control.framerate = state->resolutions[0].framerate;
            m_config["input_stream"]["framerate"] = m_encoder_control.framerate.value();
            on_resource_change(EventType::CHANGED_RESOURCE_ENCODER,std::make_shared<webserver::resources::EncoderResource::EncoderResourceState>(EncoderResourceState(m_encoder_control)));
        }

        if ((state->rotate_enabled &&
            (state->rotation == ROTATION_ANGLE_90 || state->rotation == ROTATION_ANGLE_270)))
        {
            m_encoder_control.width = state->resolutions[0].height;
            m_encoder_control.height = state->resolutions[0].width;
            m_config["input_stream"]["width"] = m_encoder_control.width.value();
            m_config["input_stream"]["height"] = m_encoder_control.height.value();
        }
        else if (state->resolutions[0].stream_size_changed || (state->rotate_enabled &&
            (state->rotation == ROTATION_ANGLE_0 || state->rotation == ROTATION_ANGLE_180))
            || !state->rotate_enabled)
        {
            m_encoder_control.width = state->resolutions[0].width;
            m_encoder_control.height = state->resolutions[0].height;
            m_config["input_stream"]["width"] = m_encoder_control.width.value();
            m_config["input_stream"]["height"] = m_encoder_control.height.value();
        }

    });
}

void webserver::resources::EncoderResource::reset_config(){
    m_config = nlohmann::json::parse(m_default_config);
    m_encoder_control.quantization.bitrate = m_config["hailo_encoder"]["rate_control"]["bitrate"]["target_bitrate"];
    m_encoder_control.quantization.rc_mode = m_encoder_control.str_to_rc_mode.at(m_config["hailo_encoder"]["rate_control"]["rc_mode"]);
    m_encoder_control.quantization.qp_hdr = m_config["hailo_encoder"]["rate_control"]["quantization"]["qp_hdr"];
    m_encoder_control.quantization.picture_rc = m_config["hailo_encoder"]["rate_control"]["picture_rc"];
    m_encoder_control.codec = m_encoder_control.str_to_codec.at(m_config["hailo_encoder"]["config"]["output_stream"]["codec"]);
    m_encoder_control.intra_pic_rate = m_config["hailo_encoder"]["rate_control"]["intra_pic_rate"];
    m_encoder_control.width = m_config["input_stream"]["width"];
    m_encoder_control.height = m_config["input_stream"]["height"];
    m_encoder_control.framerate = m_config["input_stream"]["framerate"];
    m_encoder_control.b_frames.num_b_frames = m_config["hailo_encoder"]["gop_config"]["gop_size"];
    m_encoder_control.b_frames.b_frame_qp_delta = m_config["hailo_encoder"]["gop_config"]["b_frame_qp_delta"];
}

void webserver::resources::EncoderResource::set_encoder_control(webserver::resources::EncoderResource::encoder_control_t &encoder_control)
{
    WEBSERVER_LOG_INFO("Setting encoder control");
    if ((encoder_control.quantization.rc_mode == webserver::resources::EncoderResource::bitrate_control_t::CQP &&
            (encoder_control.quantization.bitrate.has_value() ||
             encoder_control.quantization.qp_min.has_value()  ||
             encoder_control.quantization.qp_max.has_value()  ||
             encoder_control.quantization.picture_rc
            )
        )
        ||
        (
            encoder_control.quantization.rc_mode != webserver::resources::EncoderResource::bitrate_control_t::CQP &&
            (!encoder_control.quantization.bitrate.has_value() ||
             !encoder_control.quantization.qp_min.has_value()  ||
             !encoder_control.quantization.qp_max.has_value()  ||
             !encoder_control.quantization.picture_rc
            )
        )
    )
    {
        WEBSERVER_LOG_ERROR("Encoder config does not have bitrate, qp_min or qp_max while rc_mode is not CQP");
    }
    if (encoder_control.width.has_value())
        m_config["input_stream"]["width"] = encoder_control.width.value();
    if (encoder_control.height.has_value())
        m_config["input_stream"]["height"] = encoder_control.height.value();
    if (encoder_control.framerate.has_value())
        m_config["input_stream"]["framerate"] = encoder_control.framerate.value();

    m_config["hailo_encoder"]["rate_control"]["intra_pic_rate"] = encoder_control.intra_pic_rate;
    m_config["hailo_encoder"]["config"]["output_stream"]["codec"] = encoder_control.codec_to_str.at(encoder_control.codec);
    m_config["hailo_encoder"]["rate_control"]["rc_mode"] = encoder_control.rc_mode_to_str.at(encoder_control.quantization.rc_mode);

    if (encoder_control.quantization.bitrate.has_value())
        m_config["hailo_encoder"]["rate_control"]["bitrate"]["target_bitrate"] = encoder_control.quantization.bitrate.value();

    m_config["hailo_encoder"]["rate_control"]["quantization"]["qp_hdr"] = encoder_control.quantization.qp_hdr;
    m_config["hailo_encoder"]["rate_control"]["picture_rc"] = encoder_control.quantization.picture_rc;
    m_config["hailo_encoder"]["gop_config"]["gop_size"] = encoder_control.b_frames.num_b_frames;
    m_config["hailo_encoder"]["gop_config"]["b_frame_qp_delta"] = encoder_control.b_frames.b_frame_qp_delta;
    auto old_codec = m_encoder_control.codec;
    m_encoder_control = encoder_control;
    if (old_codec != encoder_control.codec)
    {
        WEBSERVER_LOG_INFO("Codec changed, notifying resource change");
        on_resource_change(EventType::CODEC_CHANGE, std::make_shared<webserver::resources::EncoderResource::EncoderResourceState>(EncoderResourceState(m_encoder_control)));
    }
}

void webserver::resources::EncoderResource::pull_encoder_config()
{
    WEBSERVER_LOG_INFO("Pulling encoder config");
    webserver::resources::EncoderResource::encoder_control_t encoder_control;
    hailo_encoder_config_t encoder_config = m_get_encoder_config();
    encoder_control.from_encoder_element_config(encoder_config);
    set_encoder_control(encoder_control);
}

void webserver::resources::EncoderResource::set_encoder_query(std::function<hailo_encoder_config_t()> get_encoder_config)
{
    WEBSERVER_LOG_INFO("Setting encoder query");
    m_get_encoder_config = get_encoder_config;
    pull_encoder_config();
}

void webserver::resources::EncoderResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    WEBSERVER_LOG_INFO("Registering HTTP endpoints for EncoderResource");
    srv->Get("/encoder", std::function<nlohmann::json()>([this]()
                                                                         {
        WEBSERVER_LOG_INFO("HTTP GET /encoder called");
        nlohmann::json j;
        pull_encoder_config();
        to_json(j, m_encoder_control);
        return j; }));

    srv->Post("/encoder", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body)
                                                                                                {
        WEBSERVER_LOG_INFO("HTTP POST /encoder called");
        webserver::resources::EncoderResource::encoder_control_t encoder_control;
        try
        {
            encoder_control = j_body.get<webserver::resources::EncoderResource::encoder_control_t>();
        }
        catch (const std::exception &e)
        {
            WEBSERVER_LOG_ERROR("Failed to parse json body to encoder_control_t");
        }
        set_encoder_control(encoder_control);
        on_resource_change(EventType::CHANGED_RESOURCE_ENCODER, std::make_shared<webserver::resources::EncoderResource::EncoderResourceState>(EncoderResourceState(m_encoder_control)));
        nlohmann::json j_out;
        to_json(j_out, m_encoder_control);
        return j_out; }));
}