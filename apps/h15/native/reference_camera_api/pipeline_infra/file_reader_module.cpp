#include "file_reader_module.hpp"

#include <chrono>
#include <fstream>

// Media-Library includes
#include "media_library/dma_memory_allocator.hpp"

// Tappas includes
#include "reference_camera_logger.hpp"

FileReader::FileReader(const std::string &name, const std::string &file_location, size_t width, size_t height,
                       double fps, bool loop_enabled)
    : m_file_location(file_location), m_width(width), m_height(height), m_frame_size(0), m_total_frames(0),
      m_current_frame_index(0), m_loop_enabled(loop_enabled), m_fps(fps), m_frame_interval(33), m_name(name)
{
    m_frame_size = calculate_frame_size();
    m_y_plane_size = m_width * m_height;
    m_uv_plane_size = m_width * m_height / 2;
    m_frame_interval = std::chrono::milliseconds(static_cast<long>(1000.0 / fps));

    REFERENCE_CAMERA_LOG_INFO("Configured FileReader '{}' with file: {}, resolution: {}x{}, fps: {}", name,
                              file_location, width, height, fps);
}

AppStatus FileReader::init()
{
    bool is_configured = !m_file_location.empty() && m_width > 0 && m_height > 0 && m_fps > 0.0;

    if (!is_configured)
    {
        REFERENCE_CAMERA_LOG_ERROR("FileReader '{}' not properly configured", m_name);
        return AppStatus::UNINITIALIZED;
    }

    if (!validate_file())
    {
        REFERENCE_CAMERA_LOG_ERROR("File validation failed for '{}': {}", m_name, m_file_location);
        return AppStatus::CONFIGURATION_ERROR;
    }

    REFERENCE_CAMERA_LOG_INFO("File validation successful for '{}': {} frames of size {} bytes each", m_name,
                              m_total_frames, m_frame_size);

    m_file_stream.open(m_file_location, std::ios::in | std::ios::binary);
    if (!m_file_stream.is_open())
    {
        REFERENCE_CAMERA_LOG_ERROR("Failed to open file for '{}': {}", m_name, m_file_location);
        return AppStatus::CONFIGURATION_ERROR;
    }

    REFERENCE_CAMERA_LOG_INFO("FileReader '{}' initialized successfully", m_name);
    return AppStatus::SUCCESS;
}

AppStatus FileReader::deinit()
{
    if (m_file_stream.is_open())
    {
        m_file_stream.close();
    }

    REFERENCE_CAMERA_LOG_INFO("FileReader '{}' deinitialized", m_name);
    return AppStatus::SUCCESS;
}

bool FileReader::read_next_frame(HailoMediaLibraryBufferPtr buffer)
{
    if (!m_file_stream.is_open())
    {
        REFERENCE_CAMERA_LOG_ERROR("File stream is not open for '{}'", m_name);
        return false;
    }

    // Handle end-of-file and loopback logic upfront
    if (m_current_frame_index >= m_total_frames)
    {
        if (m_loop_enabled)
        {
            REFERENCE_CAMERA_LOG_DEBUG("FileReader '{}' looping back to beginning of file", m_name);
            m_file_stream.clear(); // Clear EOF flag
            m_current_frame_index = 0;
        }
        else
        {
            REFERENCE_CAMERA_LOG_INFO("FileReader '{}' reached end of file", m_name);
            return false;
        }
    }

    m_file_stream.seekg(m_current_frame_index * m_frame_size, std::ios::beg);

    auto read_plane = [&](int plane_index, size_t size, const char *plane_name) -> bool {
        DmaMemoryAllocator::get_instance().dmabuf_sync_start(buffer->get_plane_ptr(plane_index));
        m_file_stream.read(reinterpret_cast<char *>(buffer->get_plane_ptr(plane_index)), size);
        DmaMemoryAllocator::get_instance().dmabuf_sync_end(buffer->get_plane_ptr(plane_index));

        if (!m_file_stream)
        {
            REFERENCE_CAMERA_LOG_ERROR("Failed to read {} plane for '{}'", plane_name, m_name);
            return false;
        }

        return true;
    };

    if (!read_plane(0, m_y_plane_size, "Y"))
    {
        return false;
    }

    if (!read_plane(1, m_uv_plane_size, "UV"))
    {
        return false;
    }

    m_current_frame_index++;

    return true;
}

void FileReader::reset()
{
    m_current_frame_index = 0;
    if (m_file_stream.is_open())
    {
        m_file_stream.clear();
        m_file_stream.seekg(0, std::ios::beg);
    }
    REFERENCE_CAMERA_LOG_DEBUG("FileReader '{}' reset to beginning", m_name);
}

size_t FileReader::calculate_frame_size() const
{
    return m_width * m_height * 3 / 2;
}

bool FileReader::validate_file()
{
    std::ifstream test_stream(m_file_location, std::ios::in | std::ios::binary | std::ios::ate);
    if (!test_stream.is_open())
    {
        REFERENCE_CAMERA_LOG_ERROR("Cannot open file for '{}': {}", m_name, m_file_location);
        return false;
    }

    // Get file size
    auto file_size = test_stream.tellg();
    test_stream.close();

    if (file_size <= 0)
    {
        REFERENCE_CAMERA_LOG_ERROR("File is empty for '{}': {}", m_name, m_file_location);
        return false;
    }

    size_t expected_frame_size = calculate_frame_size();

    if (file_size % expected_frame_size != 0)
    {
        REFERENCE_CAMERA_LOG_ERROR("File size ({}) is not a multiple of frame size ({}) for '{}'. "
                                   "File may be corrupted or have incorrect resolution.",
                                   static_cast<size_t>(file_size), expected_frame_size, m_name);
        return false;
    }

    m_total_frames = file_size / expected_frame_size;

    if (m_total_frames == 0)
    {
        REFERENCE_CAMERA_LOG_ERROR("Calculated zero frames in file for '{}'", m_name);
        return false;
    }

    REFERENCE_CAMERA_LOG_INFO("File validation successful for '{}': {} frames of size {} bytes each", m_name,
                              m_total_frames, expected_frame_size);

    return true;
}

FileReader::~FileReader()
{
    deinit();
}
