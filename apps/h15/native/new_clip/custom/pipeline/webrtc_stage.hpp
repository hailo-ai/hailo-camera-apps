#pragma once

#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <iostream>
#include "media_library/encoder.hpp"
#include "hailo_common.hpp"
#include "stage.hpp"
#include "buffer.hpp"
#include "custom/converter/convert_rtp_module.hpp"
#include "custom/streaming/webrtc_streamer_ext.hpp"

#define WEBRTC_QUEUE_SIZE_DEFAULT 5

class WebrtcStage : public ConnectedStage
{
  public:
    // Constructor and Destructor
    WebrtcStage(std::string name, std::shared_ptr<WebRTCStreamerExt> webrtc_streamer,
                size_t queue_size = WEBRTC_QUEUE_SIZE_DEFAULT, bool leaky = false, bool print_fps = false)
        : ConnectedStage(name, queue_size, leaky, print_fps), m_rtp_converter(nullptr),
          m_webrtc_streamer(webrtc_streamer), m_running(false)
    {
    }

    ~WebrtcStage() override = default;

    // Public member functions
    AppStatus create(EncodingType type)
    {
        if (m_rtp_converter == nullptr)
        {
            auto rtp_converter_expected = ConvertRtpModule::create(m_stage_name, type, m_print_fps);
            if (!rtp_converter_expected.has_value())
            {
                std::cerr << "Failed to create rtp converter" << std::endl;
                return AppStatus::CONFIGURATION_ERROR;
            }
            m_rtp_converter = rtp_converter_expected.value();
            m_type = type;
        }
        return AppStatus::SUCCESS;
    }

    AppStatus init() override
    {
        if (m_rtp_converter == nullptr)
        {
            std::cerr << "rtp converter " << m_stage_name << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("rtp converter {} not configured. Call configure()", m_stage_name);
            return AppStatus::UNINITIALIZED;
        }
        m_rtp_converter->start();

        m_running.store(true);
        m_send_thread = std::thread(&WebrtcStage::callback_worker, this);

        return AppStatus::SUCCESS;
    }

    AppStatus deinit() override
    {
        m_running.store(false);
        if (m_send_thread.joinable())
        {
            m_send_thread.join();
        }
        if (m_rtp_converter != nullptr)
        {
            m_rtp_converter->stop();
        }
        m_webrtc_streamer->close_connection();
        return AppStatus::SUCCESS;
    }

    AppStatus configure(EncodingType type)
    {
        deinit();
        m_rtp_converter = nullptr;
        return create(type);
    }

    AppStatus process(BufferPtr data)
    {
        if (m_rtp_converter == nullptr)
        {
            std::cerr << "rtp converter " << m_stage_name << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("rtp converter {} not configured. Call configure()", m_stage_name);
            return AppStatus::UNINITIALIZED;
        }

        auto metadata = data->get_metadata_of_type(MetadataType::SIZE);
        if (metadata.empty())
        {
            std::cerr << "rtp converter " << m_stage_name << " got buffer of unknown size, add SizeMeta" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("rtp converter {} got buffer of unknown size, add SizeMeta", m_stage_name);
            return AppStatus::PIPELINE_ERROR;
        }

        auto size_metadata = std::dynamic_pointer_cast<SizeMetadata>(metadata[0]);
        size_t size = size_metadata->get_size();

        m_rtp_converter->add_buffer(data->get_buffer(), size);

        return AppStatus::SUCCESS;
    }

  private:
    // Worker function for processing frames
    void callback_worker()
    {
        while (m_running.load())
        {
            GstSample *sample = m_rtp_converter->get_frame();
            if (sample != nullptr)
            {
                m_webrtc_streamer->send_rtp_packet(sample);
            }
        }
    }

    // Member variables
    EncodingType m_type;
    ConvertRtpModulePtr m_rtp_converter;
    std::shared_ptr<WebRTCStreamerExt> m_webrtc_streamer;

    std::thread m_send_thread;
    std::atomic<bool> m_running;
};

class WebrtcStageBuild : public WebrtcStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        std::shared_ptr<WebRTCStreamerExt> m_webrtc_streamer = nullptr;
        size_t m_queue_size = WEBRTC_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_print_fps = false;

      public:
        Builder &set_stage_name(std::string name)
        {
            m_stage_name = name;
            return *this;
        }
        Builder &set_webrtc_streamer(std::shared_ptr<WebRTCStreamerExt> streamer)
        {
            m_webrtc_streamer = streamer;
            return *this;
        }
        Builder &set_queue_size(size_t size)
        {
            m_queue_size = size;
            return *this;
        }
        Builder &set_leaky_opt(bool activate)
        {
            m_leaky = activate;
            return *this;
        }
        Builder &set_printfps_opt(bool activate)
        {
            m_print_fps = activate;
            return *this;
        }

        std::shared_ptr<WebrtcStage> buildptr() const
        {
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
            THROW_IF_MISSING(m_webrtc_streamer != nullptr, "set_webrtc_streamer");

            return std::make_shared<WebrtcStage>(m_stage_name.value(), m_webrtc_streamer, m_queue_size, m_leaky,
                                                 m_print_fps);
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
