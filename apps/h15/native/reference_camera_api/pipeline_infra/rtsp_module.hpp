#pragma once
#include "hailo_common.hpp"
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <thread>
#include <atomic>
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>

enum class EncodingType { H264 = 0, H265 };

class RtspModule {
public:
    RtspModule(const std::string &name,
               const std::string &mount_point,
               EncodingType encoding,
               uint32_t width,
               uint32_t height,
               uint32_t fps,
               const std::shared_ptr<GstRTSPServer> &server,
               const std::shared_ptr<GstRTSPMountPoints> &mounts);

    ~RtspModule();

    static tl::expected<std::shared_ptr<RtspModule>, AppStatus> create(
        const std::string &name,
        const std::string &mount_point,
        EncodingType encoding,
        uint32_t width,
        uint32_t height,
        uint32_t fps,
        const std::shared_ptr<GstRTSPServer> &server,
        const std::shared_ptr<GstRTSPMountPoints> &mounts);

    AppStatus start();
    AppStatus stop();
    AppStatus add_buffer(const uint8_t* data, size_t size);

private:
    std::string m_name;
    std::string m_mount_point;
    EncodingType m_type;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_fps;
    std::shared_ptr<GstRTSPServer> m_server;
    std::shared_ptr<GstRTSPMountPoints> m_mounts;

    GstRTSPMediaFactory* m_factory = nullptr;
    GstAppSrc* m_appsrc = nullptr;
    GMainLoop* m_loop = nullptr;

    std::thread m_loop_thread;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_client_count{0};
    GstClockTime m_pts{0};
    std::chrono::steady_clock::time_point m_prev_time;

    static void media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media, gpointer user_data);
    static void client_connected(GstRTSPServer* server, GstRTSPClient* client, gpointer user_data);
    static void client_disconnected(GstRTSPClient* client, gpointer user_data);

    void loop();
    std::string create_launch_pipeline() const;
    void set_gst_buffer_time(GstBuffer* buf);
};

// ---------------------------------------------------------------------------

inline tl::expected<std::shared_ptr<RtspModule>, AppStatus> RtspModule::create(
    const std::string &name,
    const std::string &mount_point,
    EncodingType type,
    uint32_t width,
    uint32_t height,
    uint32_t fps,
    const std::shared_ptr<GstRTSPServer> &server,
    const std::shared_ptr<GstRTSPMountPoints> &mounts)
{
    auto module = std::make_shared<RtspModule>(name, mount_point, type, width, height, fps, server, mounts);
    return module;
}

inline RtspModule::RtspModule(const std::string &name,
                              const std::string &mount_point,
                              EncodingType type,
                              uint32_t width,
                              uint32_t height,
                              uint32_t fps,
                              const std::shared_ptr<GstRTSPServer> &server,
                              const std::shared_ptr<GstRTSPMountPoints> &mounts)
    : m_name(name), m_mount_point(mount_point), m_type(type),
      m_width(width), m_height(height), m_fps(fps),
      m_server(server), m_mounts(mounts)
{
}

inline RtspModule::~RtspModule()
{
    stop();
}

inline std::string RtspModule::create_launch_pipeline() const
{
    std::ostringstream pipeline;
    pipeline << "( appsrc name=rtsp_src is-live=true format=time do-timestamp=true ! ";
    if (m_type == EncodingType::H264)
        pipeline << "h264parse ! rtph264pay name=pay0 pt=96 config-interval=1 )";
    else
        pipeline << "h265parse ! rtph265pay name=pay0 pt=96 config-interval=1 )";
    return pipeline.str();
}

inline void RtspModule::media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media, gpointer user_data)
{
    auto* self = static_cast<RtspModule*>(user_data);
    GstElement* pipeline = gst_rtsp_media_get_element(media);
    GstElement* src = gst_bin_get_by_name_recurse_up(GST_BIN(pipeline), "rtsp_src");
    if (!src) {
        std::cerr << "[RtspModule] Failed to find appsrc 'rtsp_src'\n";
        gst_object_unref(pipeline);
        return;
    }

    self->m_appsrc = GST_APP_SRC(src);

    // Set proper caps for byte-stream encoded data
    const char* codec = (self->m_type == EncodingType::H264) ? "video/x-h264" : "video/x-h265";
    GstCaps* caps = gst_caps_new_simple(codec,
                                        "stream-format", G_TYPE_STRING, "byte-stream",
                                        "alignment", G_TYPE_STRING, "au",
                                        "width", G_TYPE_INT, (gint)self->m_width,
                                        "height", G_TYPE_INT, (gint)self->m_height,
                                        "framerate", GST_TYPE_FRACTION, (gint)self->m_fps, 1,
                                        nullptr);
    gst_app_src_set_caps(self->m_appsrc, caps);
    gst_caps_unref(caps);

    // Set appsrc parameters
    g_object_set(self->m_appsrc,
                 "is-live", TRUE,
                 "format", GST_FORMAT_TIME,
                 "do-timestamp", TRUE,
                 nullptr);

    self->m_pts = 0;
    self->m_prev_time = std::chrono::steady_clock::now();

    gst_object_unref(pipeline);
}

inline void RtspModule::client_connected(GstRTSPServer*, GstRTSPClient* client, gpointer user_data)
{
    auto* self = static_cast<RtspModule*>(user_data);
    self->m_client_count++;
    g_signal_connect(client, "closed", G_CALLBACK(client_disconnected), user_data);
    std::cout << "[RtspModule] Client connected (" << self->m_client_count << " total)\n";
}

inline void RtspModule::client_disconnected(GstRTSPClient*, gpointer user_data)
{
    auto* self = static_cast<RtspModule*>(user_data);
    self->m_client_count--;
    std::cout << "[RtspModule] Client disconnected (" << self->m_client_count << " remaining)\n";
}

inline void RtspModule::set_gst_buffer_time(GstBuffer* buf)
{
    GST_BUFFER_PTS(buf) = m_pts;
    GST_BUFFER_DTS(buf) = m_pts;

    GstClockTime frame_duration = gst_util_uint64_scale_int(GST_SECOND, 1, m_fps);
    GST_BUFFER_DURATION(buf) = frame_duration;
    m_pts += frame_duration;
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
    if (!m_server) return AppStatus::CONFIGURATION_ERROR;

    m_factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(m_factory, create_launch_pipeline().c_str());
    gst_rtsp_media_factory_set_shared(m_factory, TRUE);
    g_signal_connect(m_factory, "media-configure", G_CALLBACK(media_configure), this);

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(m_server);
    gst_rtsp_mount_points_add_factory(mounts, m_mount_point.c_str(), m_factory);
    g_object_unref(mounts);

    g_signal_connect(m_server, "client-connected", G_CALLBACK(client_connected), this);

    std::cout << "[RtspModule] RTSP server ready at rtsp://0.0.0.0:8554" << m_mount_point << std::endl;

    m_loop_thread = std::thread([this]() { loop(); });
    return AppStatus::SUCCESS;
}

inline AppStatus RtspModule::stop()
{
    if (m_running && m_loop) {
        g_main_loop_quit(m_loop);
        if (m_loop_thread.joinable()) m_loop_thread.join();
    }

    if (m_factory) { g_object_unref(m_factory); m_factory = nullptr; }
    if (m_loop) { g_main_loop_unref(m_loop); m_loop = nullptr; }

    m_running = false;
    return AppStatus::SUCCESS;
}

inline AppStatus RtspModule::add_buffer(const uint8_t* data, size_t size)
{
    if (!m_appsrc) return AppStatus::UNINITIALIZED;
    if (m_client_count <= 0) return AppStatus::SUCCESS;

#if 1
    // --- Zero-copy version ---
    // If the data pointer comes directly from an encoder GstBuffer, you can wrap it
    // without copying. Make sure the memory stays valid until GStreamer releases it.
    GstBuffer* buf = gst_buffer_new_wrapped_full(
        GST_MEMORY_FLAG_READONLY,
        const_cast<uint8_t*>(data),  // GStreamer API not const-correct
        size,
        0,
        size,
        nullptr,  // user_data
        nullptr   // destroy_notify (optional: free callback if you own the memory)
    );

    if (!buf) {
        std::cerr << "[RtspModule] Failed to create wrapped buffer." << std::endl;
        return AppStatus::PIPELINE_ERROR;
    }

    set_gst_buffer_time(buf);

    GstFlowReturn ret;
    g_signal_emit_by_name(m_appsrc, "push-buffer", buf, &ret);
    gst_buffer_unref(buf);

#else
    // --- Copy version (safe fallback) ---
    GstBuffer* buf = gst_buffer_new_and_alloc(size);
    GstMapInfo map;
    if (gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
        memcpy(map.data, data, size);
        gst_buffer_unmap(buf, &map);
    }

    set_gst_buffer_time(buf);

    GstFlowReturn ret;
    g_signal_emit_by_name(m_appsrc, "push-buffer", buf, &ret);
    gst_buffer_unref(buf);
#endif

    if (ret != GST_FLOW_OK) {
        std::cerr << "[RtspModule] Failed to push buffer: " << ret << std::endl;
        return AppStatus::PIPELINE_ERROR;
    }

    return AppStatus::SUCCESS;
}
