#include "file_source_stage.hpp"

#include <chrono>
#include <thread>
#include <memory>

// Media-Library includes
#include "media_library/buffer_pool.hpp"

// Infra includes
#include "buffer.hpp"
#include "reference_camera_logger.hpp"

AppStatus FileSourceStage::init()
{
    if (m_file_reader == nullptr)
    {
        REFERENCE_CAMERA_LOG_ERROR("FileSourceStage not properly configured");
        return AppStatus::UNINITIALIZED;
    }

    AppStatus status = m_file_reader->init();
    if (status != AppStatus::SUCCESS)
    {
        return status;
    }

    // Create buffer pool - buffer_pool_size is guaranteed to be > 0 by builder validation
    const std::string pool_name = m_stage_name + "_buffer_pool";
    m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(m_width, m_height, HAILO_FORMAT_NV12, m_buffer_pool_size,
                                                             HAILO_MEMORY_TYPE_DMABUF, pool_name);

    if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        REFERENCE_CAMERA_LOG_ERROR("Failed to initialize buffer pool for file source stage {}", m_stage_name);
        return AppStatus::BUFFER_ALLOCATION_ERROR;
    }

    REFERENCE_CAMERA_LOG_INFO("Created buffer pool for file source stage '{}': {} buffers of size {}x{}", m_stage_name,
                              m_buffer_pool_size, m_width, m_height);

    return AppStatus::SUCCESS;
}

AppStatus FileSourceStage::deinit()
{
    REFERENCE_CAMERA_LOG_INFO("FileSourceStage deinitialized");
    return AppStatus::SUCCESS;
}

AppStatus FileSourceStage::stop()
{
    set_end_of_stream(true);
    m_thread.join();
    return AppStatus::SUCCESS;
}

void FileSourceStage::loop()
{
    REFERENCE_CAMERA_LOG_INFO("FileSourceStage loop started");

    if (init() != AppStatus::SUCCESS)
    {
        REFERENCE_CAMERA_LOG_ERROR("Failed to initialize FileSourceStage");
        return;
    }

    auto last_frame_time = std::chrono::steady_clock::now();
    auto frame_interval = m_file_reader->get_frame_interval();

    while (!m_end_of_stream)
    {
        trace_processing_start();

        HailoMediaLibraryBufferPtr buffer = std::make_shared<hailo_media_library_buffer>();

        // Acquire buffer from pool - buffer pool is guaranteed to exist
        if (m_buffer_pool->acquire_buffer(buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            REFERENCE_CAMERA_LOG_WARN("Failed to acquire buffer from pool, skipping frame");
            trace_processing_end();
            continue;
        }

        if (!m_file_reader->read_next_frame(buffer))
        {
            REFERENCE_CAMERA_LOG_INFO("Stopping.");
            set_end_of_stream(true);
            trace_processing_end();
            break;
        }

        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_frame_time);

        if (elapsed < frame_interval)
        {
            std::this_thread::sleep_for(frame_interval - elapsed);
        }

        BufferPtr wrapped_buffer = std::make_shared<Buffer>(buffer);
        wrapped_buffer->get_buffer()->isp_timestamp_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();

        send_to_subscribers(wrapped_buffer);

        trace_processing_end();

        last_frame_time = std::chrono::steady_clock::now();

        trace_fps();
    }

    deinit();
    REFERENCE_CAMERA_LOG_INFO("FileSourceStage loop ended");
}
