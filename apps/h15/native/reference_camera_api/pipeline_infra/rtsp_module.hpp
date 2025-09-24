#pragma once

#include "output_module.hpp"
#include "hailo_common.hpp"
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <thread>
#include <atomic>
#include <iostream>
#include <sstream>

class RtspModule : public OutputModule {
public:
    RtspModule(const std::string &name,
               const std::string &mount_point,
               EncodingType type,
               bool print_fps);
    ~RtspModule();

    static tl::expected<std::shared_ptr<RtspModule>, AppStatus> create(
        const std::string &name,
        const std::string &mount_point,
        EncodingType type,
        bool print_fps);

    AppStatus add_buffer(HailoMediaLibraryBufferPtr ptr, size_t size) override;
    AppStatus start() override;
    AppStatus stop() override;

private:
    std::string m_name;
    std::string m_mount_point;
    EncodingType m_type;
    bool m_print_fps;

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

inline tl::expected<std::shared_ptr<RtspModule>, AppStatus> RtspModule::create(
        const std::string &name,
        const std::string &mount_point,
        EncodingType type,
        bool print_fps)
{
    auto module = std::make_shared<RtspModule>(name, mount_point, type, print_fps);
    return module;
}

inline RtspModule::RtspModule(const std::string &name,
                              const std::string &mount_point,
                              EncodingType type,
                              bool print_fps)
    : OutputModule(name, type, print_fps),
      m_name(name),
      m_mount_point(mount_point),
      m_type(type),
      m_print_fps(print_fps)
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
    std::ostringstream pipeline;
    pipeline << "appsrc name=rtsp_src is-live=true block=true format=time "
             << "caps=video/x-" << (m_type == EncodingType::H264 ? "h264" : "h265")
             << ",stream-format=byte-stream,alignment=au "
             << " ! "
             << (m_type == EncodingType::H264 ? "rtph264pay name=pay0 pt=96" : "rtph265pay name=pay0 pt=96");
    return pipeline.str();
}

inline void RtspModule::media_configure(GstRTSPMediaFactory *factory,
                                        GstRTSPMedia *media,
                                        gpointer user_data)
{
    RtspModule* self = static_cast<RtspModule*>(user_data);
    GstElement* element = gst_rtsp_media_get_element(media);
    self->m_appsrc = GST_APP_SRC(gst_bin_get_by_name_recurse_up(GST_BIN(element), "rtsp_src"));
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
        if (m_loop_thread.joinable()) m_loop_thread.join();
    }
    if (m_appsrc) {
        gst_object_unref(m_appsrc);
        m_appsrc = nullptr;
    }
    return AppStatus::SUCCESS;
}

inline AppStatus RtspModule::add_buffer(HailoMediaLibraryBufferPtr ptr, size_t size)
{
    if (!m_appsrc) return AppStatus::UNINITIALIZED;

    GstBuffer* gst_buffer = gst_buffer_new_wrapped_full(
        GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS,
        ptr->get_plane_ptr(0),
        ptr->get_plane_size(0),
        0, size,
        new HailoMediaLibraryBufferPtr(ptr),
        GDestroyNotify([](gpointer data) {
            delete static_cast<HailoMediaLibraryBufferPtr*>(data);
        })
    );

    GstFlowReturn ret = gst_app_src_push_buffer(m_appsrc, gst_buffer);
    if (ret != GST_FLOW_OK) return AppStatus::PIPELINE_ERROR;
    return AppStatus::SUCCESS;
}
