#include "convert_rtp_module.hpp"
#include <sstream>

// Helper struct to wrap a Hailo media library buffer.
struct GstPtrWrapper
{
    HailoMediaLibraryBufferPtr ptr;
};


tl::expected<ConvertRtpModulePtr, AppStatus> ConvertRtpModule::create(std::string name, EncodingType type)
{
    AppStatus status = AppStatus::UNINITIALIZED;
    ConvertRtpModulePtr rtp_module = std::make_shared<ConvertRtpModule>(name, type, status);
    if (status != AppStatus::SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return rtp_module;
}

ConvertRtpModule::ConvertRtpModule(std::string name, EncodingType type, AppStatus &status)
    : OutputModule(name, type), m_appsink(nullptr)
{
    // Initialize gstreamer.
    gst_init(nullptr, nullptr);
    m_pipeline = gst_parse_launch(create_pipeline_string().c_str(), nullptr);
    if (!m_pipeline)
    {
        std::cerr << "Failed create RTP converter pipeline" << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed create RTP converter pipeline");
        status = AppStatus::CONFIGURATION_ERROR;
        return;
    }
    gst_bus_add_watch(gst_element_get_bus(m_pipeline), (GstBusFunc)bus_call, this);
    this->OutputModule::set_gst_callbacks(SRC_NAME);
    get_appsink();
    status = AppStatus::SUCCESS;
}


std::string ConvertRtpModule::create_pipeline_string()
{
    std::cout << "Creating RTP converter pipeline" << std::endl;
    std::string caps_type;
    std::string rtp_payloader;
    std::string encoding_name;
    std::string rtp_parser;
    if (m_type == EncodingType::H264)
    {
        caps_type = "video/x-h264";
        rtp_payloader = "rtph264pay";
        encoding_name = "H264";
        rtp_parser = "h264parse";
    }
    else
    {
        caps_type = "video/x-h265";
        rtp_payloader = "rtph265pay";
        encoding_name = "H265";
        rtp_parser = "h265parse";
    }

    std::ostringstream caps2;
    caps2 << caps_type << ",framerate=30/1,stream-format=byte-stream,alignment=au";

    std::ostringstream sink;
    sink << "appsink name=appsink emit-signals=false max-buffers=0 ";

    std::ostringstream pipelineStream;
    pipelineStream << "appsrc do-timestamp=true format=time block=true is-live=true max-bytes=0 "
                      "max-buffers=1 name="<< std::string(SRC_NAME) <<" ! "
                      "queue name=" << SRC_QUEUE_NAME << " leaky=downstream max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! "
                      << caps2.str() << " ! "
                      "tee name=tee0 "
                      "tee0. ! "
                          "queue leaky=no max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! "
                          << rtp_parser << " ! "
                          "queue leaky=no max-size-buffers=0 max-size-bytes=0 max-size-time=0 ! "
                          << rtp_payloader << " ! application/x-rtp, media=(string)video, encoding-name=(string)" << encoding_name << " ! "
                          "queue leaky=no max-size-buffers=0 max-size-bytes=0 max-size-time=0 ! "
                          << sink.str() <<
                      "tee0. ! "
                          "queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
                          "fpsdisplaysink fps-update-interval=2000 signal-fps-measurements=true name=fpsdisplaysink "
                          "text-overlay=false sync=true video-sink=fakesink ";

    std::string pipeline = pipelineStream.str();
    REFERENCE_CAMERA_LOG_INFO("Pipeline: {}", pipeline);
    return pipeline;
}


void ConvertRtpModule::get_appsink()
{
    // Retrieve and store the appsink element for frame extraction.
    GstElement *appsink = gst_bin_get_by_name(GST_BIN(m_pipeline), "appsink");
    if (appsink)
    {
        m_appsink = appsink;
        gst_object_unref(appsink);
    }
    else
    {
        m_appsink = nullptr;
    }
}


GstSample* ConvertRtpModule::get_frame()
{
    if (!m_appsink)
    {
        REFERENCE_CAMERA_LOG_ERROR("Appsink element not available");
        return nullptr;
    }
    // Pull a sample from the appsink (this call blocks until a sample is available).
    GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(m_appsink), 10 * GST_MSECOND);
    return sample;
}
