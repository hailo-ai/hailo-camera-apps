#include "encoder.hpp"
using namespace webserver::resources;

void EncoderResource::encoder_control_t::from_encoder_element_config(hailo_encoder_config_t encoder_config)
{
    width = encoder_config.input_stream.width;
    height = encoder_config.input_stream.height;
    framerate = encoder_config.input_stream.framerate;
    intra_pic_rate = encoder_config.rate_control.intra_pic_rate;
    codec = hailo_codec_to_codec.at(encoder_config.output_stream.codec);
    quantization.rc_mode = hailo_rc_mode_to_rc_mode.at(encoder_config.rate_control.rc_mode);
    if(!encoder_config.rate_control.quantization.intra_qp_delta.has_value()
        || !encoder_config.rate_control.quantization.fixed_intra_qp.has_value()
        || !encoder_config.rate_control.quantization.qp_max.has_value()
        || !encoder_config.rate_control.quantization.qp_min.has_value())
    {
        WEBSERVER_LOG_ERROR("Encoder config does not have intra_qp_delta or fixed_intra_qp");
    }
    if (quantization.rc_mode == bitrate_control_t::CQP)
    {
        quantization.set_rate_control_off(encoder_config.rate_control.quantization.intra_qp_delta.value(),
                                            encoder_config.rate_control.quantization.fixed_intra_qp.value(),
                                            encoder_config.rate_control.quantization.qp_hdr);
    }
    else
    {
        quantization.set_rate_control_on(quantization.rc_mode, encoder_config.rate_control.bitrate.target_bitrate,
                                            encoder_config.rate_control.quantization.qp_min.value(),
                                            encoder_config.rate_control.quantization.qp_max.value(),
                                            encoder_config.rate_control.quantization.intra_qp_delta.value(),
                                            encoder_config.rate_control.quantization.fixed_intra_qp.value(),
                                            encoder_config.rate_control.quantization.qp_hdr);
    }
    b_frames.num_b_frames = encoder_config.gop.gop_size;
    b_frames.b_frame_qp_delta = encoder_config.gop.b_frame_qp_delta;
}
std::string EncoderResource::encoder_control_t::to_string()
{
    std::string str = "Encoder control: \n";
    std::string width = this->width.has_value() ? std::to_string(this->width.value()) : "N/A";
    str += "Width: " + width + "\n";
    std::string height = this->height.has_value() ? std::to_string(this->height.value()) : "N/A";
    str += "Height: " + height + "\n";
    std::string framerate = this->framerate.has_value() ? std::to_string(this->framerate.value()) : "N/A";
    str += "Framerate: " + framerate + "\n";
    str += "Intra pic rate: " + std::to_string(intra_pic_rate) + "\n";
    str += "Rate control mode: " + rc_mode_to_str.at(quantization.rc_mode) + "\n";
    std::string bitrate = quantization.rc_mode == bitrate_control_t::CQP ? "N/A" : std::to_string(quantization.bitrate.value());
    str += "Bitrate: " + bitrate + "\n";
    std::string qp_min = quantization.rc_mode == bitrate_control_t::CQP ? "N/A" : std::to_string(quantization.qp_min.value());
    str += "QP min: " + qp_min + "\n";
    std::string qp_max = quantization.rc_mode == bitrate_control_t::CQP ? "N/A" : std::to_string(quantization.qp_max.value());
    str += "QP max: " + qp_max + "\n";
    str += "Intra QP delta: " + std::to_string(quantization.intra_qp_delta) + "\n";
    str += "Fixed intra QP: " + std::to_string(quantization.fixed_intra_qp) + "\n";
    str += "QP HDR: " + std::to_string(quantization.qp_hdr) + "\n";
    str += "Picture RC: " + std::to_string(quantization.picture_rc) + "\n";
    str += "B frames: " + std::to_string(b_frames.num_b_frames) + "\n";
    str += "B frame QP delta: " + std::to_string(b_frames.b_frame_qp_delta) + "\n";
    return str;
}
void EncoderResource::encoder_control_t::fill_encoder_element_config(hailo_encoder_config_t& encoder_config)
{
    if (!width.has_value() || !height.has_value() || !framerate.has_value())
    {
        throw std::invalid_argument("Encoder config does not have width, height or framerate");
    }
    encoder_config.input_stream.width = width.value();
    encoder_config.input_stream.height = height.value();
    encoder_config.input_stream.framerate = framerate.value();
    encoder_config.rate_control.intra_pic_rate = intra_pic_rate;
    encoder_config.rate_control.rc_mode = rc_mode_to_hailo_rc_mode.at(quantization.rc_mode);
    encoder_config.rate_control.quantization.intra_qp_delta = quantization.intra_qp_delta;
    encoder_config.rate_control.quantization.fixed_intra_qp = quantization.fixed_intra_qp;
    encoder_config.rate_control.quantization.qp_hdr = quantization.qp_hdr;
    if (quantization.rc_mode != bitrate_control_t::CQP && (!quantization.bitrate.has_value() || !quantization.qp_min.has_value() || !quantization.qp_max.has_value()))
    {
        throw std::invalid_argument("Encoder config does not have bitrate, qp_min or qp_max while rc_mode is not CQP");
    }
    if (quantization.rc_mode != bitrate_control_t::CQP && !quantization.picture_rc)
    {
        throw std::invalid_argument("Encoder config does not have picture_rc set to true while rc_mode is not CQP");
    }
    if (quantization.rc_mode != bitrate_control_t::CQP)
    {
        encoder_config.rate_control.bitrate.target_bitrate = quantization.bitrate.value();
        encoder_config.rate_control.quantization.qp_min = quantization.qp_min.value();
        encoder_config.rate_control.quantization.qp_max = quantization.qp_max.value();
    }
    if (quantization.rc_mode == bitrate_control_t::CQP && quantization.picture_rc){
        throw std::invalid_argument("Encoder config have bitrate, qp_min, qp_max or picture_rc while rc_mode is CQP");
    }
    encoder_config.rate_control.picture_rc = quantization.picture_rc;
    encoder_config.gop.gop_size = b_frames.num_b_frames;
    encoder_config.gop.b_frame_qp_delta = b_frames.b_frame_qp_delta;
}