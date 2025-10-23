#pragma once

#include "hailo_common.hpp"
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <thread>
#include <atomic>
#include <iostream>
#include <sstream>

enum class EncodingType {
    H264 = 0,
    H265,
};

class RtspModule {
public:
    RtspModule(const std::string &name,
               const std::string &mount_point,
               EncodingType encoding,
               bool print_fps,
               uint32_t width,
               uint32_t height,
               uint32_t fps);

    ~RtspModule();

    static tl::expected<std::shared_ptr<RtspModule>, AppStatus> create(
        const std::string &name,
        const std::string &mount_point,
        EncodingType encoding,
        bool print_fps,
        uint32_t width,
        uint32_t height,
        uint32_t fps);

    AppStatus add_buffer(HailoMediaLibraryBufferPtr ptr, size_t size);
    AppStatus start();
    AppStatus stop();

private:
    std::string m_name;
    std::string m_mount_point;
    EncodingType m_type;
    bool m_print_fps;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_fps;

    GstRTSPServer *m_server = nullptr;
    GstRTSPMediaFactory *m_factory = nullptr;
    GstAppSrc *m_appsrc = nullptr;

    GMainLoop *m_loop = nullptr;
    std::thread m_loop_thread;
    std::atomic<bool> m_running {false};

    static void media_configure(GstRTSPMediaFactory *factory,
                                GstRTSPMedia *media,
                                gpointer user_data);

    void loop();
    std::string create_launch_pipeline();
};

// --------------------------------------
// Implementation
// --------------------------------------

inline tl::expected<std::shared_ptr<RtspModule>, AppStatus> RtspModule::create(
    const std::string &name,
    const std::string &mount_point,
    EncodingType type,
    bool print_fps,
    uint32_t width,
    uint32_t height,
    uint32_t fps)
{
    auto module = std::make_shared<RtspModule>(name, mount_point, type, print_fps, width, height, fps);
    return module;
}

inline RtspModule::RtspModule(const std::string &name,
                              const std::string &mount_point,
                              EncodingType type,
                              bool print_fps,
                              uint32_t width,
                              uint32_t height,
                              uint32_t fps)
    : m_name(name), m_mount_point(mount_point), m_type(type), m_print_fps(print_fps), m_width(width), m_height(height), m_fps(fps)
{
    gst_init(nullptr, nullptr);
}

inline RtspModule::~RtspModule()
{
    stop();
    if (m_factory) g_object_unref(m_factory);
    if (m_server) g_object_unref(m_server);
    if (m_loop) g_main_loop_unref(m_loop);
}

inline std::string RtspModule::create_launch_pipeline()
{
    // pipeline: appsrc -> queue -> parser -> rtph26xpay (pay0)
    std::ostringstream pipeline;
    // Note: we'll set caps and do-timestamp on appsrc in media_configure
    pipeline << "appsrc name=rtsp_src is-live=true format=time ";
    // Use parser + payloader (payloader will set config-interval)
    if (m_type == EncodingType::H264) {
        pipeline << "caps=video/x-h264,stream-format=avc,alignment=au ! "
                 << "queue max-size-buffers=20 leaky=downstream ! h264parse ! rtph264pay name=pay0 pt=96 config-interval=1";
    } else {
        pipeline << "caps=video/x-h265,stream-format=hev1,alignment=au ! "
                 << "queue max-size-buffers=20 leaky=downstream ! h265parse ! rtph265pay name=pay0 pt=96 config-interval=1";
    }
    return pipeline.str();
}

inline void RtspModule::media_configure(GstRTSPMediaFactory *factory,
                                        GstRTSPMedia *media,
                                        gpointer user_data)
{
    RtspModule* self = static_cast<RtspModule*>(user_data);
    GstElement* element = gst_rtsp_media_get_element(media);

    // get appsrc by name
    GstElement* src = gst_bin_get_by_name_recurse_up(GST_BIN(element), "rtsp_src");
    if (!src) {
        g_print("Failed to find appsrc 'rtsp_src'\n");
        gst_object_unref(element);
        return;
    }

    // store typed pointer
    self->m_appsrc = GST_APP_SRC(src);

    // Set appsrc properties: make it timestamp buffers automatically if needed
    g_object_set(G_OBJECT(self->m_appsrc),
                 "is-live", TRUE,
                 "format", GST_FORMAT_TIME,
                 "do-timestamp", TRUE,
                 NULL);

    // Build caps including resolution/framerate if available
    GstCaps* caps = nullptr;
    if (self->m_type == EncodingType::H264) {
        // Use avc (container style) so that h264parse/rtph264pay can produce sprop-parameter-sets
        caps = gst_caps_new_simple("video/x-h264",
                                   "stream-format", G_TYPE_STRING, "avc",
                                   "alignment", G_TYPE_STRING, "au",
                                   "width", G_TYPE_INT, (gint)self->m_width,
                                   "height", G_TYPE_INT, (gint)self->m_height,
                                   "framerate", GST_TYPE_FRACTION, (gint)self->m_fps, 1,
                                   NULL);
    } else {
        caps = gst_caps_new_simple("video/x-h265",
                                   "stream-format", G_TYPE_STRING, "hev1",
                                   "alignment", G_TYPE_STRING, "au",
                                   "width", G_TYPE_INT, (gint)self->m_width,
                                   "height", G_TYPE_INT, (gint)self->m_height,
                                   "framerate", GST_TYPE_FRACTION, (gint)self->m_fps, 1,
                                   NULL);
    }

    // Set the caps on appsrc
    gst_app_src_set_caps(self->m_appsrc, caps);
    gst_caps_unref(caps);

    gst_object_unref(element);
}

inline void RtspModule::loop()
{
    m_loop = g_main_loop_new(nullptr, FALSE);
    m_running = true;
    g_main_loop_run(m_loop);
    m_running = false;
}

inline AppStatus RtspModule::start()
{
    if (m_server) return AppStatus::SUCCESS;

    m_server = gst_rtsp_server_new();
    if (!m_server) return AppStatus::CONFIGURATION_ERROR;

    m_factory = gst_rtsp_media_factory_new();
    if (!m_factory) return AppStatus::CONFIGURATION_ERROR;

    gst_rtsp_media_factory_set_launch(m_factory, create_launch_pipeline().c_str());
    gst_rtsp_media_factory_set_shared(m_factory, TRUE);
    g_signal_connect(m_factory, "media-configure", G_CALLBACK(media_configure), this);

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(m_server);
    gst_rtsp_mount_points_add_factory(mounts, m_mount_point.c_str(), m_factory);
    g_object_unref(mounts);

    if (!gst_rtsp_server_attach(m_server, nullptr)) return AppStatus::CONFIGURATION_ERROR;

    m_loop_thread = std::thread([this]() { loop(); });
    return AppStatus::SUCCESS;
}

inline AppStatus RtspModule::stop()
{
    if (m_running && m_loop) {
        g_main_loop_quit(m_loop);
        if (m_loop_thread.joinable())
            m_loop_thread.join();
    }

    if (m_appsrc) {
        gst_object_unref(m_appsrc);
        m_appsrc = nullptr;
    }

    if (m_factory) {
        g_object_unref(m_factory);
        m_factory = nullptr;
    }

    if (m_server) {
        g_object_unref(m_server);
        m_server = nullptr;
    }

    if (m_loop) {
        g_main_loop_unref(m_loop);
        m_loop = nullptr;
    }

    return AppStatus::SUCCESS;
}


inline AppStatus RtspModule::add_buffer(HailoMediaLibraryBufferPtr ptr, size_t size)
{
    if (!m_appsrc) return AppStatus::UNINITIALIZED;

    struct BufferWrapper { HailoMediaLibraryBufferPtr ptr; };
    auto wrapper = new BufferWrapper{ptr};

    GstBuffer* gst_buffer = gst_buffer_new_wrapped_full(
        GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS,
        ptr->get_plane_ptr(0),
        ptr->get_plane_size(0),
        0, size,
        wrapper,
        [](gpointer data){
            delete static_cast<BufferWrapper*>(data);
        }
    );

    // Set timestamp/duration: maintain a monotonic pts counter
    static GstClockTime pts = 0;
    // duration = 1 / fps in GST time units
    GstClockTime duration = gst_util_uint64_scale_int(1, GST_SECOND, (m_fps > 0 ? m_fps : 30));
    GST_BUFFER_PTS(gst_buffer) = pts;
    GST_BUFFER_DTS(gst_buffer) = pts;
    GST_BUFFER_DURATION(gst_buffer) = duration;
    pts += duration;

    GstFlowReturn ret = gst_app_src_push_buffer(m_appsrc, gst_buffer);
    if (ret != GST_FLOW_OK) {
        std::cerr << "Failed to push buffer: " << ret << std::endl;
        return AppStatus::PIPELINE_ERROR;
    }
    return AppStatus::SUCCESS;
}

