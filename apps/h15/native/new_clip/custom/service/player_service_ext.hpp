#pragma once

#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>

#include "custom/streaming/mkv_streamer.hpp"
#include "custom/streaming/webrtc_streamer_ext.hpp"
#include "reference_camera_app_constructor.hpp"

class VideoStreamingServiceExt : public CameraAppExtension
{
  public:
    // Factory method to create an instance
    static std::shared_ptr<VideoStreamingServiceExt> create(const std::string &webrtc_session_id = "");

    // Destructor
    ~VideoStreamingServiceExt();

    // Main streaming control
    bool start_streaming(const std::vector<VideoFile> &video_files);
    void stop_streaming();
    bool is_streaming() const;

    // WebRTC access for web service
    std::shared_ptr<WebRTCStreamerExt> get_webrtc_streamer() const;

  private:
    // Private constructor (use factory method)
    VideoStreamingServiceExt(const std::string &session_id);

    // Initialization methods
    bool initialize();
    void cleanup();

    // Components
    std::unique_ptr<MKVStreamer> m_mkv_streamer;
    std::shared_ptr<WebRTCStreamerExt> m_webrtc_streamer;

    // Threading synchronization
    mutable std::mutex m_status_mutex;
    mutable std::mutex m_component_mutex;

    // Current streaming state
    std::atomic<bool> m_is_streaming;
    mutable std::mutex m_status_data_mutex;

    // Session management
    std::string m_session_id;

    // MKVStreamer callbacks
    void on_frame(const RtpPacketData &frame);
    void on_end_of_stream();
    void on_error(const ErrorInfo &error);
};

/* Implementation */

std::shared_ptr<VideoStreamingServiceExt> VideoStreamingServiceExt::create(const std::string &webrtc_session_id)
{
    // Use shared_ptr with custom deleter to access private constructor
    auto service = std::shared_ptr<VideoStreamingServiceExt>(new VideoStreamingServiceExt(webrtc_session_id));

    if (!service->initialize())
    {
        return nullptr;
    }

    return service;
}

VideoStreamingServiceExt::VideoStreamingServiceExt(const std::string &webrtc_session_id)
    : m_is_streaming(false), m_session_id(webrtc_session_id)
{
}

VideoStreamingServiceExt::~VideoStreamingServiceExt()
{
    stop_streaming();
    cleanup();
}

bool VideoStreamingServiceExt::initialize()
{
    std::lock_guard<std::mutex> lock(m_component_mutex);

    try
    {
        // Initialize MKVStreamer
        m_mkv_streamer = std::make_unique<MKVStreamer>();

        // Set up MKVStreamer callbacks
        m_mkv_streamer->set_rtp_packet_callback([this](const RtpPacketData &frame) { this->on_frame(frame); });

        m_mkv_streamer->set_end_of_stream_callback([this]() { this->on_end_of_stream(); });

        m_mkv_streamer->set_error_callback([this](const ErrorInfo &error) { this->on_error(error); });

        // Initialize WebRTC streamer
        m_webrtc_streamer = std::make_shared<WebRTCStreamerExt>(m_session_id);

        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to initialize VideoStreamingServiceExt: " << e.what() << std::endl;
        return false;
    }
}

void VideoStreamingServiceExt::cleanup()
{
    std::lock_guard<std::mutex> lock(m_component_mutex);

    // Cleanup WebRTC
    if (m_webrtc_streamer)
    {
        m_webrtc_streamer->close_connection();
        m_webrtc_streamer.reset();
    }

    // Cleanup MKVStreamer
    m_mkv_streamer.reset();
}

bool VideoStreamingServiceExt::start_streaming(const std::vector<VideoFile> &video_files)
{
    if (video_files.empty())
    {
        std::cerr << "No video files provided" << std::endl;
        return false;
    }

    // Stop any existing streaming
    stop_streaming();

    std::lock_guard<std::mutex> lock(m_status_mutex);

    // Start MKV streaming
    if (!m_mkv_streamer->start_streaming(video_files))
    {
        std::cerr << "Failed to start MKV streaming" << std::endl;
        return false;
    }

    // Can start streaming
    m_is_streaming = true;

    std::cout << "Video streaming started with " << video_files.size() << " files" << std::endl;
    return true;
}

void VideoStreamingServiceExt::stop_streaming()
{
    std::lock_guard<std::mutex> lock(m_status_mutex);

    if (!m_is_streaming)
    {
        return;
    }

    std::cout << "Stopping video streaming..." << std::endl;

    // Stop MKV streaming
    if (m_mkv_streamer)
    {
        m_mkv_streamer->stop_streaming();
    }

    m_is_streaming = false;

    std::cout << "Video streaming stopped" << std::endl;
}

bool VideoStreamingServiceExt::is_streaming() const
{
    return m_is_streaming;
}

std::shared_ptr<WebRTCStreamerExt> VideoStreamingServiceExt::get_webrtc_streamer() const
{
    return m_webrtc_streamer;
}

void VideoStreamingServiceExt::on_frame(const RtpPacketData &frame)
{
    if (frame.sample != nullptr)
    {
        if (m_is_streaming && m_webrtc_streamer && m_webrtc_streamer->has_active_client())
        {
            m_webrtc_streamer->send_rtp_packet(frame.sample);
        }
        else
        {
            // Must free the sample if not being used
            gst_sample_unref(frame.sample);
        }
    }
}

void VideoStreamingServiceExt::on_end_of_stream()
{
    std::cout << "End of stream reached" << std::endl;

    // Auto-stop streaming when all files are processed
    stop_streaming();
}

void VideoStreamingServiceExt::on_error(const ErrorInfo &error)
{
    std::cerr << "MKVStreamer error: " << error.message << std::endl;

    // Stop streaming on error
    stop_streaming();
}
