#pragma once

#include <gst/gst.h>
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
    static tl::expected<ConvertRtpModulePtr, AppStatus> create(std::string name, EncodingType type, bool print_fps);

    ~ConvertRtpModule() override = default;

    ConvertRtpModule(std::string name, EncodingType type, AppStatus &status, bool print_fps);

    // Function to get a frame from the appsink.
    GstSample *get_frame();

  private:
    std::string create_pipeline_string();
    void get_appsink();
    GstElement *m_appsink; // Stores the appsink element.
};
