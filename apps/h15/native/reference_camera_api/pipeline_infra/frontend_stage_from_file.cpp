#include "frontend_stage_from_file.hpp"

#include <chrono>
#include <thread>
#include <utility>

// Media-Library includes
#include "media_library/buffer_pool.hpp"

// Tappas includes
#include "reference_camera_logger.hpp"

AppStatus FrontendStageFromFile::stop()
{
    m_feeding_thread_active = false;
    if (m_feeding_thread.joinable())
    {
        m_feeding_thread.join();
    }

    return FrontendStage::stop();
}

AppStatus FrontendStageFromFile::init()
{
    if (m_file_reader == nullptr)
    {
        REFERENCE_CAMERA_LOG_ERROR("FileReader not configured for frontend {}", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }

    AppStatus status = m_file_reader->init();
    if (status != AppStatus::SUCCESS)
    {
        REFERENCE_CAMERA_LOG_ERROR("Failed to initialize file reader for frontend {}", m_stage_name);
        return status;
    }

    status = FrontendStage::init();
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
        REFERENCE_CAMERA_LOG_ERROR("Failed to initialize buffer pool for frontend stage {}", m_stage_name);
        return AppStatus::BUFFER_ALLOCATION_ERROR;
    }

    REFERENCE_CAMERA_LOG_INFO("Created buffer pool for frontend stage '{}': {} buffers of size {}x{}", m_stage_name,
                              m_buffer_pool_size, m_width, m_height);

    m_feeding_thread_active = true;
    m_feeding_thread = std::thread(&FrontendStageFromFile::feeding_thread_func, this);

    return AppStatus::SUCCESS;
}

AppStatus FrontendStageFromFile::deinit()
{
    m_feeding_thread_active = false;
    if (m_feeding_thread.joinable())
    {
        m_feeding_thread.join();
    }

    return FrontendStage::deinit();
}

AppStatus FrontendStageFromFile::configure(MediaLibraryFrontendPtr frontend)
{
    m_file_reader = std::make_shared<FileReader>(m_stage_name + "_reader", m_file_location, m_width, m_height, m_fps,
                                                 m_loop_enabled);

    return FrontendStage::configure(std::move(frontend));
}

void FrontendStageFromFile::feeding_thread_func()
{
    REFERENCE_CAMERA_LOG_INFO("Frontend feeding thread started");

    auto last_frame_time = std::chrono::steady_clock::now();
    auto frame_interval = m_file_reader->get_frame_interval();

    while (m_feeding_thread_active && !m_end_of_stream)
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
            REFERENCE_CAMERA_LOG_INFO("File reading completed, stopping feeding thread");
            set_end_of_stream(true);
            trace_processing_end();
            break;
        }

        buffer->isp_timestamp_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();

        if (m_frontend->add_buffer(buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            REFERENCE_CAMERA_LOG_WARN("Failed to add buffer to frontend, skipping frame");
            trace_processing_end();
            continue;
        }

        trace_processing_end();

        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_frame_time);

        if (elapsed < frame_interval)
        {
            std::this_thread::sleep_for(frame_interval - elapsed);
        }

        last_frame_time = std::chrono::steady_clock::now();
    }

    REFERENCE_CAMERA_LOG_INFO("Frontend feeding thread ended");
}
