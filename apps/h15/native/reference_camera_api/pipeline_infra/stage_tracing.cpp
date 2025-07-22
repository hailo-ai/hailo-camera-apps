#include "stage_tracing.hpp"
#include "reference_camera_perfetto.hpp"
#include <chrono>

StageTracing::StageTracing(const std::string &name)
    : m_stage_name(name), m_counter(0), m_first_fps_measured(false)
#ifdef HAVE_PERFETTO
      ,
      m_trace_processing_string("processing_" + name),
      m_trace_processing_name(perfetto::DynamicString(m_trace_processing_string)), m_fps_counter_name("fps_" + name),
      m_fps_counter_track(perfetto::DynamicString(m_fps_counter_name), "fps")
#endif
{
    m_last_time = std::chrono::steady_clock::now();
}

void StageTracing::trace_fps()
{
    REFERENCE_CAMERA_TRACE_COUNTER(m_fps_counter_track, m_counter);
}

void StageTracing::increment_counter()
{
    // Handle first measurement
    if (!m_first_fps_measured)
    {
        m_last_time = std::chrono::steady_clock::now();
        m_first_fps_measured = true;
    }

    m_counter++;
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_seconds = std::chrono::duration<double>(current_time - m_last_time);

    if (elapsed_seconds.count() >= 1.0)
    {
        trace_fps();
        m_counter = 0;
        m_last_time = current_time;
    }
}

void StageTracing::trace_processing_start()
{
    REFERENCE_CAMERA_TRACE_EVENT_BEGIN(m_trace_processing_name);
}

void StageTracing::trace_processing_end()
{
    REFERENCE_CAMERA_TRACE_EVENT_END();
}

void StageTracing::trace_async_event_begin(uint64_t unique_id)
{
    REFERENCE_CAMERA_TRACE_ASYNC_EVENT_BEGIN(m_trace_processing_name, unique_id);
}

void StageTracing::trace_async_event_end(uint64_t unique_id)
{
    REFERENCE_CAMERA_TRACE_ASYNC_EVENT_END(m_trace_processing_name, unique_id);
}

void StageTracing::trace_async_event_begin(uint64_t unique_id, const char *category)
{
    REFERENCE_CAMERA_TRACE_ASYNC_EVENT_BEGIN_WITH_TRACK(perfetto::DynamicString(category), unique_id,
                                                        m_trace_processing_name);
}
