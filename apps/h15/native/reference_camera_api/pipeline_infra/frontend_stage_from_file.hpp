#pragma once

// General includes
#include <memory>
#include <thread>

// Media-Library includes
#include "media_library/buffer_pool.hpp"

// Infra includes
#include "file_reader_module.hpp"
#include "frontend_stage.hpp"

/**
 * @brief Frontend stage that reads video data from a file instead of a camera
 *
 * This class extends FrontendStage to read raw video data from a file and feeds it
 * to the MediaLibrary frontend, which then processes it through the configured pipeline.
 * It inherits all the frontend subscription and stream management functionality from
 * the base FrontendStage class.
 */
class FrontendStageFromFile : public FrontendStage
{
  private:
    std::shared_ptr<FileReader> m_file_reader;
    std::shared_ptr<MediaLibraryBufferPool> m_buffer_pool;
    bool m_feeding_thread_active;
    std::thread m_feeding_thread;

    // File parameters for creating FileReader
    std::string m_file_location;
    size_t m_width;
    size_t m_height;
    double m_fps;
    bool m_loop_enabled;
    size_t m_buffer_pool_size;

  public:
    /**
     * @brief Constructor for FrontendStageFromFile
     * @param name Stage name
     * @param file_location Path to the raw video file (NV12 format)
     * @param width Video width in pixels
     * @param height Video height in pixels
     * @param fps Frames per second for playback
     * @param loop_enabled Whether to loop the video when it reaches the end
     * @param queue_size Queue size for the stage
     * @param leaky Whether the queue is leaky
     * @param print_fps Whether to print FPS information
     * @param buffer_pool_size Size of the buffer pool (must be > 0)
     * @param trace_processing_operations Whether to trace processing operations
     */
    FrontendStageFromFile(std::string name, const std::string &file_location, size_t width, size_t height, double fps,
                          bool loop_enabled, size_t queue_size, bool leaky, bool print_fps, size_t buffer_pool_size,
                          bool trace_processing_operations = true)
        : FrontendStage(name, queue_size, leaky, print_fps, trace_processing_operations),
          m_feeding_thread_active(false), m_file_location(file_location), m_width(width), m_height(height), m_fps(fps),
          m_loop_enabled(loop_enabled), m_buffer_pool_size(buffer_pool_size)
    {
        m_file_reader = nullptr;
    }

    /**
     * @brief Create and configure the frontend stage with file input
     * @param frontend MediaLibrary frontend instance
     * @return AppStatus indicating success or failure
     */
    AppStatus create(MediaLibraryFrontendPtr frontend)
    {
        // Create FileReader internally with the provided parameters
        m_file_reader = std::make_shared<FileReader>(m_stage_name + "_reader", m_file_location, m_width, m_height,
                                                     m_fps, m_loop_enabled);

        return FrontendStage::create(frontend);
    }

    /**
     * @brief Stop the frontend stage
     * @return AppStatus indicating success or failure
     */
    virtual AppStatus stop() override;

    /**
     * @brief Initialize the frontend stage
     * @return AppStatus indicating success or failure
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the frontend stage
     * @return AppStatus indicating success or failure
     */
    AppStatus deinit() override;

    /**
     * @brief Configure the frontend stage
     * @param frontend MediaLibrary frontend instance
     * @return AppStatus indicating success or failure
     */
    AppStatus configure(MediaLibraryFrontendPtr frontend);

  private:
    /**
     * @brief Thread function that reads frames from file and feeds them to the frontend
     */
    void feeding_thread_func();

    /**
     * @brief Start processing trace if tracing is enabled
     */
    void trace_processing_start()
    {
        if (m_trace_processing_operations)
        {
            m_tracing->trace_processing_start();
        }
    }

    /**
     * @brief End processing trace if tracing is enabled
     */
    void trace_processing_end()
    {
        if (m_trace_processing_operations)
        {
            m_tracing->trace_processing_end();
        }
    }
};

/**
 * @brief Builder class for FrontendStageFromFile
 */
class FrontendStageFromFileBuild : public FrontendStageFromFile
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_file_location;
        std::optional<size_t> m_width;
        std::optional<size_t> m_height;
        std::optional<double> m_fps;
        std::optional<size_t> m_buffer_pool_size; // Now required, no default
        bool m_loop_enabled = true;
        size_t m_queue_size = FRONEND_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        bool m_print_fps = false;
        bool m_trace_processing_operations = true;

      public:
        Builder &set_stage_name(std::string name)
        {
            m_stage_name = name;
            return *this;
        }

        Builder &set_file_location(std::string file_location)
        {
            m_file_location = file_location;
            return *this;
        }

        Builder &set_width(size_t width)
        {
            m_width = width;
            return *this;
        }

        Builder &set_height(size_t height)
        {
            m_height = height;
            return *this;
        }

        Builder &set_fps(double fps)
        {
            m_fps = fps;
            return *this;
        }

        Builder &set_loop_enabled_opt(bool loop_enabled)
        {
            m_loop_enabled = loop_enabled;
            return *this;
        }

        Builder &set_queue_size_opt(size_t size)
        {
            m_queue_size = size;
            return *this;
        }

        Builder &set_leaky_opt(bool activate)
        {
            m_leaky = activate;
            return *this;
        }

        Builder &set_printfps_opt(bool activate)
        {
            m_print_fps = activate;
            return *this;
        }

        Builder &set_buffer_pool_size(size_t size)
        {
            m_buffer_pool_size = size;
            return *this;
        }

        Builder &set_trace_processing_operations_opt(bool activate)
        {
            m_trace_processing_operations = activate;
            return *this;
        }

        std::shared_ptr<FrontendStageFromFile> buildptr() const
        {
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
            THROW_IF_MISSING(m_file_location.has_value(), "set_file_location");
            THROW_IF_MISSING(m_width.has_value(), "set_width");
            THROW_IF_MISSING(m_height.has_value(), "set_height");
            THROW_IF_MISSING(m_fps.has_value(), "set_fps");
            THROW_IF_MISSING(m_buffer_pool_size.has_value(), "set_buffer_pool_size");

            if (m_buffer_pool_size.value() == 0)
            {
                throw std::invalid_argument("Buffer pool size must be greater than 0 for FrontendStageFromFile. Use "
                                            "set_buffer_pool_size() to set a valid size.");
            }

            return std::make_shared<FrontendStageFromFile>(m_stage_name.value(), m_file_location.value(),
                                                           m_width.value(), m_height.value(), m_fps.value(),
                                                           m_loop_enabled, m_queue_size, m_leaky, m_print_fps,
                                                           m_buffer_pool_size.value(), m_trace_processing_operations);
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
