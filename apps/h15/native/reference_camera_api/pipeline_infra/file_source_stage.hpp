#pragma once

// Media-Library includes
#include "media_library/buffer_pool.hpp"
#include "media_library/dma_memory_allocator.hpp"

// Tappas includes
#include "hailo_common.hpp"

// Infra includes
#include "file_reader_module.hpp"
#include "stage.hpp"
#include "buffer.hpp"

class FileSourceStage : public ConnectedStage
{
  private:
    std::shared_ptr<FileReader> m_file_reader;
    std::shared_ptr<MediaLibraryBufferPool> m_buffer_pool;

    // File parameters for creating FileReader
    std::string m_file_location;
    size_t m_width;
    size_t m_height;
    double m_fps;
    bool m_loop_enabled;
    size_t m_buffer_pool_size;

  public:
    /**
     * @brief Constructor for FileSourceStage with file configuration
     * @param name Stage name
     * @param file_location Path to the raw video file (NV12 format)
     * @param width Video width in pixels
     * @param height Video height in pixels
     * @param fps Frames per second for playback
     * @param loop_enabled Whether to loop the video when it reaches the end
     * @param queue_size Queue size for the stage
     * @param leaky Whether the queue is leaky
     * @param print_fps Whether to print FPS information
     * @param buffer_pool_size Size of the buffer pool
     * @param trace_processing_operations Whether to trace processing operations
     */
    FileSourceStage(std::string name, const std::string &file_location, size_t width, size_t height, double fps,
                    bool loop_enabled, size_t queue_size, bool leaky, bool print_fps, size_t buffer_pool_size,
                    bool trace_processing_operations = true)
        : ConnectedStage(std::move(name), queue_size, leaky, print_fps, trace_processing_operations),
          m_file_location(file_location), m_width(width), m_height(height), m_fps(fps), m_loop_enabled(loop_enabled),
          m_buffer_pool_size(buffer_pool_size)
    {
        m_file_reader =
            std::make_shared<FileReader>(m_stage_name + "_reader", file_location, width, height, fps, loop_enabled);
    }

    /**
     * @brief Initialize the file source stage
     * @return AppStatus indicating success or failure
     */
    AppStatus init() override;

    /**
     * @brief Deinitialize the file source stage
     * @return AppStatus indicating success or failure
     */
    AppStatus deinit() override;

    /**
     * @brief Stop the file source stage
     * @return AppStatus indicating success or failure
     */
    AppStatus stop() override;

    /**
     * @brief Main loop that reads frames from file and sends them to subscribers
     */
    void loop() override;

  private:
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

class FileSourceStageBuild : public FileSourceStage
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
        size_t m_queue_size = 10;
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

        std::shared_ptr<FileSourceStage> buildptr() const
        {
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
            THROW_IF_MISSING(m_file_location.has_value(), "set_file_location");
            THROW_IF_MISSING(m_width.has_value(), "set_width");
            THROW_IF_MISSING(m_height.has_value(), "set_height");
            THROW_IF_MISSING(m_fps.has_value(), "set_fps");
            THROW_IF_MISSING(m_buffer_pool_size.has_value(), "set_buffer_pool_size");

            if (m_buffer_pool_size.value() == 0)
            {
                throw std::invalid_argument("Buffer pool size must be greater than 0 for FileSourceStage. Use "
                                            "set_buffer_pool_size() to set a valid size.");
            }

            auto stage = std::make_shared<FileSourceStage>(
                m_stage_name.value(), m_file_location.value(), m_width.value(), m_height.value(), m_fps.value(), true,
                m_queue_size, m_leaky, m_print_fps, m_buffer_pool_size.value(), m_trace_processing_operations);
            return stage;
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
