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

// Infra includes
#include "buffer.hpp"
#include "queue.hpp"
#include "stage.hpp"

// Defines
#define SRC_QUEUE_NAME "appsrc_q"

enum class EncodingType
{
    H264 = 0,
    H265,
};

class OutputModule;
using OutputModulePtr = std::shared_ptr<OutputModule>;

class OutputModule
{
private:
    GstAppSrc *m_appsrc;
    GMainLoop *m_main_loop;
    std::shared_ptr<std::thread> m_main_loop_thread;
    std::string m_name;

protected:
    EncodingType m_type;
    GstElement *m_pipeline;

public:
    virtual ~OutputModule();
    OutputModule(std::string name, EncodingType type);
    AppStatus start();
    AppStatus stop();
    AppStatus add_buffer(HailoMediaLibraryBufferPtr ptr, size_t size);
    void on_fps_measurement(GstElement *fpssink, gdouble fps, gdouble droprate,
                            gdouble avgfps);
    gboolean on_bus_call(GstBus *bus, GstMessage *msg);
    static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer user_data)
    {
        OutputModule *output_module = static_cast<OutputModule *>(user_data);
        return output_module->on_bus_call(bus, msg);
    }
    void set_gst_callbacks(std::string source);

private:
    static void fps_measurement(GstElement *fpssink, gdouble fps,
                                gdouble droprate, gdouble avgfps,
                                gpointer user_data)
    {
        OutputModule *output_module = static_cast<OutputModule *>(user_data);
        output_module->on_fps_measurement(fpssink, fps, droprate, avgfps);
    }
    GstFlowReturn add_buffer_internal(GstBuffer *buffer);
};

inline OutputModule::OutputModule(std::string name, EncodingType type)
    : m_name(name), m_type(type)
{
    m_main_loop = g_main_loop_new(NULL, FALSE);
}

inline OutputModule::~OutputModule()
{
    if (m_pipeline != nullptr && GST_MINI_OBJECT_REFCOUNT_VALUE(m_pipeline) > 0)
    {
        gst_object_unref(m_pipeline);
    }
}

inline AppStatus OutputModule::start()
{
    GstStateChangeReturn ret =
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        std::cerr << "Failed to start output pipeline" << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed to start output pipeline");
        return AppStatus::PIPELINE_ERROR;
    }
    m_main_loop_thread = std::make_shared<std::thread>(
        [this]()
        { g_main_loop_run(m_main_loop); });

    return AppStatus::SUCCESS;
}

inline AppStatus OutputModule::stop()
{
    gboolean ret = gst_element_send_event(m_pipeline, gst_event_new_eos());
    if (!ret)
    {
        std::cerr << "Failed to stop the output pipeline" << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed to stop the output pipeline");
        return AppStatus::PIPELINE_ERROR;
    }

    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    g_main_loop_quit(m_main_loop);
    m_main_loop_thread->join();
    return AppStatus::SUCCESS;
}

/**
 * Print the FPS of the pipeline
 *
 * @note Prints the FPS to the stdout.
 */
inline void OutputModule::on_fps_measurement(GstElement *fpsdisplaysink,
                                    gdouble fps,
                                    gdouble droprate,
                                    gdouble avgfps)
{
    gchar *name;
    g_object_get(G_OBJECT(fpsdisplaysink), "name", &name, NULL);
    std::cout << m_name << ", DROP RATE: " << droprate << " FPS: " << fps << " AVG_FPS: " << avgfps << std::endl;
    g_free(name);
}

/**
 * Set the callbacks
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 */
inline void OutputModule::set_gst_callbacks(std::string appsrc_name)
{
    // set fps callbacks
    GstElement *fpssink = gst_bin_get_by_name(GST_BIN(m_pipeline), "fpsdisplaysink");
    g_signal_connect(fpssink, "fps-measurements", G_CALLBACK(fps_measurement),this);
    gst_object_unref(fpssink);

    GstElement *appsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), appsrc_name.c_str());
    m_appsrc = GST_APP_SRC(appsrc);
    gst_object_unref(appsrc);
}

inline gboolean OutputModule::on_bus_call(GstBus *bus, GstMessage *msg)
{
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_EOS:
    {
        //TODO: EOS never received
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        g_main_loop_quit(m_main_loop);
        m_main_loop_thread->join();
        break;
    }
    case GST_MESSAGE_ERROR:
    {
        gchar *debug;
        GError *err;

        gst_message_parse_error(msg, &err, &debug);
        std::cerr << "Received an error message from the output pipeline: " << err->message << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Received an error message from the output pipeline: {}", err->message);
        g_error_free(err);
        g_free(debug);
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        g_main_loop_quit(m_main_loop);
        m_main_loop_thread->join();
        break;
    }
    default:
        break;
    }
    return TRUE;
}

struct OutputPtrWrapper
{
    HailoMediaLibraryBufferPtr ptr;
};

static inline void hailo_media_library_output_release(OutputPtrWrapper *wrapper)
{
    delete wrapper;
}

inline AppStatus OutputModule::add_buffer(HailoMediaLibraryBufferPtr ptr, size_t size)
{
    // convert to GstBuffer
    OutputPtrWrapper *wrapper = new OutputPtrWrapper();
    wrapper->ptr = ptr;
    GstBuffer *gst_buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS,
                                                        ptr->get_plane_ptr(0),
                                                        ptr->get_plane_size(0),
                                                        0, size, wrapper, GDestroyNotify(hailo_media_library_output_release));
    gst_buffer_add_hailo_buffer_meta(gst_buffer, ptr, size);
    
    GstFlowReturn ret = this->add_buffer_internal(gst_buffer);
    if (ret != GST_FLOW_OK)
    {
        return AppStatus::PIPELINE_ERROR;
    }
    return AppStatus::SUCCESS;
}

inline GstFlowReturn OutputModule::add_buffer_internal(GstBuffer *buffer)
{
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buffer);
    if (ret != GST_FLOW_OK)
    {
        GST_ERROR_OBJECT(m_appsrc, "Failed to push buffer to appsrc");
        return ret;
    }
    return ret;
}
