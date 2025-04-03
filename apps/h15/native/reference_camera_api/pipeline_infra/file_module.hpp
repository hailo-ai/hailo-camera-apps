#pragma once

// General includes
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <tl/expected.hpp>
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

#define FILE_SOURCE "file_src"

class FileModule;
using FileModulePtr = std::shared_ptr<FileModule>;

class FileModule : public OutputModule
{
private:
    std::string m_filepath;
    EncodingType m_type;
    
public:
    static tl::expected<FileModulePtr, AppStatus> create(std::string name, std::string filepath, EncodingType type);
    ~FileModule() override = default;
    FileModule(std::string name, std::string filepath, EncodingType type, AppStatus &status);

private:
    std::string create_pipeline_string();
};

tl::expected<FileModulePtr, AppStatus> FileModule::create(std::string name, std::string filepath, EncodingType type)
{
    AppStatus status = AppStatus::UNINITIALIZED;
    FileModulePtr file_module = std::make_shared<FileModule>(name, filepath, type, status);
    if (status != AppStatus::SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return file_module;
}

FileModule::FileModule(std::string name, std::string filepath, EncodingType type, AppStatus &status)
    : OutputModule(name, type), m_filepath(filepath), m_type(type)
{
    // Initialize gstreamer
    gst_init(nullptr, nullptr);
    m_pipeline = gst_parse_launch(create_pipeline_string().c_str(), NULL);
    if (!m_pipeline)
    {
        std::cerr << "Failed create file pipeline" << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed create file pipeline");
        status = AppStatus::CONFIGURATION_ERROR;
        return;
    }
    gst_bus_add_watch(gst_element_get_bus(m_pipeline), (GstBusFunc)bus_call, this);
    this->OutputModule::set_gst_callbacks(FILE_SOURCE);

    status = AppStatus::SUCCESS;
}

std::string FileModule::create_pipeline_string()
{
    std::string pipeline = "";

    std::string caps_type;
    std::string encoder_parser;
    if (m_type == EncodingType::H264)
    {
        caps_type = "video/x-h264";
        encoder_parser = "h264parse";
    }
    else
    {
        caps_type = "video/x-h265";
        encoder_parser = "h265parse";
    }

    std::ostringstream caps2;
    caps2 << caps_type << ",framerate=30/1,stream-format=byte-stream,alignment=au";

    std::ostringstream file_sink;
    file_sink << "filesink location=" << m_filepath << " name=file_sink ";

    pipeline =
        "appsrc name=file_src do-timestamp=true format=time block=true is-live=true max-bytes=0 max-buffers=1 ! "
        "queue name=" +
        std::string(SRC_QUEUE_NAME) + " leaky=no max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! " +
        caps2.str() + " ! "
        "tee name=file_tee "
        "file_tee. ! "
            "queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
            "fpsdisplaysink name=fpsdisplaysink sync=false video-sink=fakesink "
        "file_tee. ! "
            "queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! " +
            encoder_parser + " ! " + file_sink.str() + " sync=false ";

    std::cout << "Pipeline: " << pipeline << std::endl;
    REFERENCE_CAMERA_LOG_INFO("Pipeline: {}", pipeline);

    return pipeline;
}
