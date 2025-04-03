#pragma once
#include "media_library/encoder_config.hpp"
#include "common/resources.hpp"
#include "configs.hpp"

namespace webserver
{
    namespace resources
    {
        class EncoderResource : public Resource
        {
        public:
            enum codec_mode_t
            {
                H264,
                H265,
            };
            

            enum bitrate_control_t
            {
                VBR,
                CVBR,
                CQP, //CQP so the encoder deside itself the bitrate and the quality
            };
            
            struct rate_control_t
            {
                bitrate_control_t rc_mode;
                std::optional<uint32_t> bitrate; //10k to 40,000k
                std::optional<uint32_t> qp_min; // 0 to 51
                std::optional<uint32_t> qp_max; // 0 to 51
                int32_t intra_qp_delta; // -12 to 12
                uint32_t fixed_intra_qp; // 0 to 51
                int32_t qp_hdr; // 0 to 51 TODO check if when in !CQP mode it will get -1
                bool picture_rc;

                void set_rate_control_off(int32_t intra_qp_delta, uint32_t fixed_intra_qp, int32_t qp_hdr)
                {
                    this->rc_mode = bitrate_control_t::CQP;
                    this->bitrate = std::nullopt;
                    this->qp_min = std::nullopt;
                    this->qp_max = std::nullopt;
                    this->intra_qp_delta = intra_qp_delta;
                    this->fixed_intra_qp = fixed_intra_qp;
                    if (qp_hdr < 0 || qp_hdr > 51)
                    {
                        throw std::invalid_argument("qp_hdr must be between 0 and 51");
                    }
                    this->qp_hdr = qp_hdr;
                    this->picture_rc = false;

                }
                void set_rate_control_on(bitrate_control_t rc_mode, int bitrate, uint32_t qp_min, uint32_t qp_max, int32_t intra_qp_delta, uint32_t fixed_intra_qp, int32_t qp_hdr)
                {
                    this->rc_mode = rc_mode;
                    this->bitrate = bitrate;
                    this->qp_min = qp_min;
                    this->qp_max = qp_max;
                    this->intra_qp_delta = intra_qp_delta;
                    this->fixed_intra_qp = fixed_intra_qp;
                    this->qp_hdr = qp_hdr;
                    this->picture_rc = true;
                }
            };

            struct b_frames_config_t
            {
                uint32_t num_b_frames; //gop size 
                uint32_t b_frame_qp_delta;
            };

            struct encoder_control_t
            {
                std::optional<uint32_t> width;
                std::optional<uint32_t> height;
                std::optional<uint32_t> framerate;

                uint32_t intra_pic_rate; //0 to 300
                codec_mode_t codec;

                rate_control_t quantization;
                b_frames_config_t b_frames;


                encoder_control_t() = default;
                encoder_control_t& operator=(const encoder_control_t &other)
                      {
                        intra_pic_rate = other.intra_pic_rate;
                        codec = other.codec;
                        quantization = other.quantization;
                        b_frames = other.b_frames;
                        if (other.width.has_value())
                            width = other.width.value();
                        if (other.height.has_value())
                            height = other.height.value();
                        if (other.framerate.has_value())
                            framerate = other.framerate.value();
                        return *this;
                      }
                
                void from_encoder_element_config(hailo_encoder_config_t encoder_config);
                void fill_encoder_element_config(hailo_encoder_config_t& encoder_config);
                std::string to_string();
                const std::unordered_map<codec_mode_t, codec_t> codec_to_hailo_codec{{H264, CODEC_TYPE_H264}, {H265, CODEC_TYPE_HEVC}};
                const std::unordered_map<codec_t, codec_mode_t> hailo_codec_to_codec{{CODEC_TYPE_H264, H264}, {CODEC_TYPE_HEVC, H265}};
                const std::unordered_map<codec_mode_t, std::string> codec_to_str{{H264, "CODEC_TYPE_H264"}, {H265, "CODEC_TYPE_HEVC"}};
                const std::unordered_map<std::string, codec_mode_t> str_to_codec{{"CODEC_TYPE_H264", H264}, {"CODEC_TYPE_HEVC", H265}};

                const std::unordered_map<bitrate_control_t, rc_mode_t> rc_mode_to_hailo_rc_mode{{VBR, rc_mode_t::VBR}, {CVBR, rc_mode_t::CVBR}, {CQP, rc_mode_t::CQP}};
                const std::unordered_map<rc_mode_t, bitrate_control_t> hailo_rc_mode_to_rc_mode{{rc_mode_t::VBR, VBR}, {rc_mode_t::CVBR, CVBR}, {rc_mode_t::CQP, CQP}};
                const std::unordered_map<bitrate_control_t, std::string> rc_mode_to_str{{VBR, "VBR"}, {CVBR, "CVBR"}, {CQP, "CQP"}};
                const std::unordered_map<std::string, bitrate_control_t> str_to_rc_mode{{"VBR", VBR}, {"CVBR", CVBR}, {"CQP", CQP}};

            };

            class EncoderResourceState : public ResourceState
            {
            public:
                encoder_control_t control;
                EncoderResourceState(encoder_control_t control) : control(control) {}
            };

            EncoderResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<webserver::resources::ConfigResourceBase> configs);
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "encoder"; }
            ResourceType get_type() override { return ResourceType::RESOURCE_ENCODER; }
            void set_encoder_query(std::function<hailo_encoder_config_t()> get_encoder_config);
            void fill_encoder_element_config(hailo_encoder_config_t &encoder_config);

        private:
            encoder_control_t m_encoder_control;
            void set_encoder_control(encoder_control_t &encoder_control);
            std::function<hailo_encoder_config_t()> m_get_encoder_config;
            void pull_encoder_config();
            void reset_config() override;
        };

        void to_json(nlohmann::json &j, const EncoderResource::encoder_control_t &b);
        void from_json(const nlohmann::json &j, EncoderResource::encoder_control_t &b);
    }
}