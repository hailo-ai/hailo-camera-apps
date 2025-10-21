#pragma once

// General includes
#include <algorithm>

// Media-Library includes
#include "media_library/encoder.hpp"

// Tappas includes
#include "hailo_common.hpp"

// Infra includes
#include "stage.hpp"
#include "buffer.hpp"
#include "rtsp_module.hpp"

#include <tl/expected.hpp>
#include <memory>
#include <vector>
#include <iostream>

#define RTSP_QUEUE_SIZE_DEFAULT (1)

class RtspStage : public ConnectedStage
{
private:
    EncodingType m_type;
    std::shared_ptr<RtspModule> m_rtsp;
    std::string m_mount_point;

public:
    RtspStage(std::string name, EncodingType type = EncodingType::H264,
              size_t queue_size = RTSP_QUEUE_SIZE_DEFAULT,
              bool leaky = false, bool print_fps = false)
        : ConnectedStage(name, queue_size, leaky, print_fps), m_type(type) {}

    ~RtspStage() override = default;

    AppStatus configure(const std::string &mount_point)
    {
        if (!m_rtsp)
        {
            auto rtsp_exp = RtspModule::create(m_stage_name, mount_point, m_type, m_print_fps);
            if (!rtsp_exp.has_value())
            {
                std::cerr << "Failed to create RTSP module" << std::endl;
                REFERENCE_CAMERA_LOG_ERROR("Failed to create RTSP module");
                return rtsp_exp.error();
            }
            m_rtsp = rtsp_exp.value();
            m_mount_point = mount_point;
        }
        return AppStatus::SUCCESS;
    }

    AppStatus init() override
    {
        if (!m_rtsp)
        {
            std::cerr << "Rtsp " << m_stage_name << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Rtsp {} not configured. Call configure()", m_stage_name);
            return AppStatus::UNINITIALIZED;
        }
        return m_rtsp->start();
    }

    AppStatus deinit() override
    {
        if (!m_rtsp) return AppStatus::UNINITIALIZED;
        return m_rtsp->stop();
    }
    enum NaluType {
    UNSUPPORTED = 0,
    NALU_TYPE_SLICE = 1, // Slice (I/P/B frame)
    NALU_TYPE_IDR = 5,   // IDR frame (I-frame)
    NALU_TYPE_SPS = 7,   // Sequence Parameter Set (SPS)
    NALU_TYPE_PPS = 8,   // Picture Parameter Set (PPS)
    NALU_TYPE_SEI = 6,   // Supplemental Enhancement Information
    NALU_TYPE_AUD = 9    // Access Unit Delimiter
};

// Function to extract NALU type from NAL header (1 byte)
NaluType get_nalu_type(uint8_t nal_header) {
    uint8_t nal_unit_type = nal_header & 0x1F; // Extract NALU type (low 5 bits)
    
    switch (nal_unit_type) {
        case 1: return NALU_TYPE_SLICE;  // Slice (I/P/B)
        case 5: return NALU_TYPE_IDR;    // IDR (I-frame)
        case 7: return NALU_TYPE_SPS;    // SPS
        case 8: return NALU_TYPE_PPS;    // PPS
        case 6: return NALU_TYPE_SEI;    // SEI
        case 9: return NALU_TYPE_AUD;    // AUD
        default: return UNSUPPORTED;     // Unsupported NALU type
    }
}

// Function to split buffer into NALUs based on start codes (0x000001 or 0x00000001)
std::vector<std::vector<uint8_t>> split_into_nalus(const uint8_t* buf, size_t size) {
    std::vector<std::vector<uint8_t>> nalus;
    size_t i = 0;

    while (i < size) {
        // Check for start code 0x000001 (most common)
        if (i + 2 < size && buf[i] == 0x00 && buf[i+1] == 0x00 && buf[i+2] == 0x01) {
            size_t start = i + 3;  // NALU start
            i = start;
            while (i + 2 < size && !(buf[i] == 0x00 && buf[i+1] == 0x00 && buf[i+2] == 0x01)) {
                i++;
            }
            nalus.push_back(std::vector<uint8_t>(buf + start, buf + i));
        }
        // Check for start code 0x00000001 (less common)
        else if (i + 3 < size && buf[i] == 0x00 && buf[i+1] == 0x00 && buf[i+2] == 0x00 && buf[i+3] == 0x01) {
            size_t start = i + 4;
            i = start;
            while (i + 3 < size && !(buf[i] == 0x00 && buf[i+1] == 0x00 && buf[i+2] == 0x00 && buf[i+3] == 0x01)) {
                i++;
            }
            nalus.push_back(std::vector<uint8_t>(buf + start, buf + i));
        } else {
            i++;
        }
    }

    return nalus;
}

// Function to print the type of NALU
void print_nalu_type(const std::vector<uint8_t>& nalu) {
    if (nalu.empty()) return;
    
    NaluType type = get_nalu_type(nalu[0]);
    switch (type) {
        case NALU_TYPE_SLICE:
            std::cout << "Slice (I/P/B Frame)" << std::endl;
            break;
        case NALU_TYPE_IDR:
            std::cout << "IDR Frame (I Frame)" << std::endl;
            break;
        case NALU_TYPE_SPS:
            std::cout << "SPS (Sequence Parameter Set)" << std::endl;
            break;
        case NALU_TYPE_PPS:
            std::cout << "PPS (Picture Parameter Set)" << std::endl;
            break;
        case NALU_TYPE_SEI:
            std::cout << "SEI (Supplemental Enhancement Info)" << std::endl;
            break;
        case NALU_TYPE_AUD:
            std::cout << "AUD (Access Unit Delimiter)" << std::endl;
            break;
        default:
            std::cout << "Unsupported NALU type" << std::endl;
    }
}

// Main function to parse the video buffer
void process_video_buffer(const uint8_t* buffer, size_t size) {
    // Split the buffer into NALUs
    auto nalus = split_into_nalus(buffer, size);
    
    std::cout << "Processing buffer with " << nalus.size() << " NALUs." << std::endl;

    // For each NALU, print its type
    for (size_t i = 0; i < nalus.size(); ++i) {
        std::cout << "NALU " << i + 1 << ": ";
        print_nalu_type(nalus[i]);
    }
}


    AppStatus process(BufferPtr buffer) override
    {
        if (!m_rtsp)
        {
            std::cerr << "Rtsp " << m_stage_name << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Rtsp {} not configured. Call configure()", m_stage_name);
            return AppStatus::UNINITIALIZED;
        }

        std::vector<MetadataPtr> metadata = buffer->get_metadata_of_type(MetadataType::SIZE);
        if (metadata.size() <= 0)
        {
            std::cerr << "Udp " << m_stage_name << " got buffer of unknown size, add SizeMeta" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Udp {} got buffer of unknown size, add SizeMeta");
            return AppStatus::PIPELINE_ERROR;
        }
        SizeMetadataPtr size_metadata = std::dynamic_pointer_cast<SizeMetadata>(metadata[0]);
        size_t size = size_metadata->get_size();
        m_rtsp->add_buffer(buffer->get_buffer(), size);

        std::cerr << "RTSP stage, buffer size: " << size << " size2: " << buffer->get_buffer()->get_plane_size(0) << std::endl;
        const uint8_t* tmp = reinterpret_cast<const uint8_t*>(buffer->get_buffer()->get_plane_ptr(0));
        process_video_buffer(tmp, size);
        return AppStatus::SUCCESS;
    }
};

// ================= Builder =================

class RtspStageBuilder
{
private:
    std::string m_name = "RtspStage";
    EncodingType m_type = EncodingType::H264;
    bool m_print_fps = false;
    std::string m_mount_point = "/live";

public:
    RtspStageBuilder &name(const std::string &name)
    {
        m_name = name;
        return *this;
    }

    RtspStageBuilder &encoding(EncodingType type)
    {
        m_type = type;
        return *this;
    }

    RtspStageBuilder &print_fps(bool enable)
    {
        m_print_fps = enable;
        return *this;
    }

    RtspStageBuilder &mount_point(const std::string &mp)
    {
        m_mount_point = mp;
        return *this;
    }

    std::shared_ptr<RtspStage> build()
    {
        auto stage = std::make_shared<RtspStage>(m_name, m_type, RTSP_QUEUE_SIZE_DEFAULT, false, m_print_fps);
        stage->configure(m_mount_point);
        return stage;
    }
};
