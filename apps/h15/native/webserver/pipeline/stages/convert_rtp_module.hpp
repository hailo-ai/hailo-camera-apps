#pragma once

// General includes
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>  // Needed for appsink functions
#include <gst/video/video.h>
#include <tl/expected.hpp>
#include <functional>
#include <iostream>
#include <queue>
#include <thread>
#include <vector>
#include <memory>
#include <string>
#include <sstream>

// Tappas includes
#include "hailo_objects.hpp"
#include "hailo_common.hpp"

// Media library includes
#include "gsthailobuffermeta.hpp"
#include "media_library/buffer_pool.hpp"
#include "media_library/media_library_types.hpp"

// Infra includes
#include "buffer.hpp"
#include "queue.hpp"
#include "stage.hpp"
#include "output_module.hpp"

// Defines
#define SRC_QUEUE_NAME "appsrc_q"
#define SRC_NAME "src0"


class ConvertRtpModule;
using ConvertRtpModulePtr = std::shared_ptr<ConvertRtpModule>;

class ConvertRtpModule : public OutputModule
{
public:
    // Factory method to create an instance.
    static tl::expected<ConvertRtpModulePtr, AppStatus> create(std::string name, EncodingType type);

    ~ConvertRtpModule() override = default;;
    ConvertRtpModule(std::string name, EncodingType type, AppStatus &status);

    // Function to get a frame from the appsink.
    GstSample* get_frame();

private:
    std::string create_pipeline_string();
    void get_appsink();
    GstElement *m_appsink; // Stores the appsink element.
};
