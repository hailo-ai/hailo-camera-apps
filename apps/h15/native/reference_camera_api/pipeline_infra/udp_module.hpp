#pragma once

// General includes
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <tl/expected.hpp>
#include <functional>
#include <iostream>
#include <queue>
#include <thread>
#include <vector>

// Tappas includes
#include "hailo_objects.hpp"
#include "hailo_common.hpp"

// Media library includes
#include "gsthailobuffermeta.hpp"
#include "media_library/buffer_pool.hpp"
#include "media_library/media_library_types.hpp"
#include "output_module.hpp"

// Infra includes
#include "buffer.hpp"
#include "queue.hpp"
#include "stage.hpp"
#include "output_module.hpp"

// Defines
#define SRC_QUEUE_NAME "appsrc_q"
#define UDP_SOURCE "udp_src"

class UdpModule;
using UdpModulePtr = std::shared_ptr<UdpModule>;

class UdpModule : public OutputModule
{
private:
    std::string m_host;
    std::string m_port;

public:
    static tl::expected<UdpModulePtr, AppStatus> create(std::string name, std::string host, std::string port, EncodingType type);
    ~UdpModule() override = default;
    UdpModule(std::string name, std::string host, std::string port, EncodingType type, AppStatus &status);

private:
    std::string create_pipeline_string();
};

inline tl::expected<UdpModulePtr, AppStatus>
UdpModule::create(std::string name, std::string host, std::string port, EncodingType type)
{
    AppStatus status = AppStatus::UNINITIALIZED;
    UdpModulePtr udp_module = std::make_shared<UdpModule>(name, host, port, type, status);
    if (status != AppStatus::SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return udp_module;
}

inline UdpModule::UdpModule(std::string name, std::string host, std::string port, EncodingType type, AppStatus &status)
    : OutputModule(name, type), m_host(host), m_port(port)
{
    // Initialize gstreamer
    gst_init(nullptr, nullptr);
    m_pipeline = gst_parse_launch(create_pipeline_string().c_str(), NULL);
    if (!m_pipeline)
    {
        std::cerr << "Failed create UDP pipeline" << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed create UDP pipeline");
        status = AppStatus::CONFIGURATION_ERROR;
        return;
    }
    gst_bus_add_watch(gst_element_get_bus(m_pipeline), (GstBusFunc)bus_call, this);
    this->OutputModule::set_gst_callbacks(UDP_SOURCE);

    status = AppStatus::SUCCESS;
}

/**
 * Create the gstreamer pipeline as string
 *
 * @return A string containing the gstreamer pipeline.
 */
inline std::string UdpModule::create_pipeline_string()
{
    std::string pipeline = "";

    std::string caps_type;
    std::string rtp_payloader;
    std::string encoding_name;
    if (m_type == EncodingType::H264)
    {
        caps_type = "video/x-h264";
        rtp_payloader = "rtph264pay";
        encoding_name = "H264";
    }
    else
    {
        caps_type = "video/x-h265";
        rtp_payloader = "rtph264pay";
        encoding_name = "H265";
    }

    std::ostringstream caps2;
    caps2 << caps_type << ",framerate=30/1,stream-format=byte-stream,alignment=au";

    std::ostringstream udp_sink;
    udp_sink << "udpsink host=" << m_host << " port=" << m_port;

    pipeline =
        "appsrc do-timestamp=true format=time block=true is-live=true max-bytes=0 "
        "max-buffers=1 name="+ std::string(UDP_SOURCE) + " ! "
        "queue name=" +
        std::string(SRC_QUEUE_NAME) + " leaky=no max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! " +
        caps2.str() + " ! " +
        "tee name=udp_tee "
        "udp_tee. ! "
            "queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! " +
            rtp_payloader + " ! application/x-rtp, media=video, encoding-name=" + encoding_name + " ! " +
            udp_sink.str() + " name=udp_sink sync=true "
        "udp_tee. ! "
            "queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
            "fpsdisplaysink fps-update-interval=2000 signal-fps-measurements=true name=fpsdisplaysink "
            "text-overlay=false sync=true video-sink=fakesink ";

    REFERENCE_CAMERA_LOG_INFO("Pipeline: {}", pipeline);

    return pipeline;
}