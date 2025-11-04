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

#define RTSP_QUEUE_SIZE_DEFAULT (3)

class RtspStage : public ConnectedStage
{
private:
    EncodingType m_type;
    std::shared_ptr<RtspModule> m_rtsp;
    std::string m_mount_point;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_fps;
    std::shared_ptr<GstRTSPServer> m_server;
    std::shared_ptr<GstRTSPMountPoints> m_mounts;

public:
    RtspStage(std::string name,
              EncodingType type,
              size_t queue_size,
              bool print_fps,
              uint32_t width,
              uint32_t height,
              uint32_t fps,
              std::shared_ptr<GstRTSPServer> &server,
              std::shared_ptr<GstRTSPMountPoints> &mounts)
        : ConnectedStage(name, queue_size, false, print_fps),
          m_type(type),
          m_width(width),
          m_height(height),
          m_fps(fps),
          m_server(server),
          m_mounts(mounts)  {}

    ~RtspStage() override = default;

    AppStatus configure(const std::string &mount_point)
    {
        if (!m_rtsp)
        {
            auto rtsp_exp = RtspModule::create(m_stage_name, mount_point, m_type, m_width, m_height, m_fps, m_server, m_mounts);
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
        const uint8_t* tmp = reinterpret_cast<const uint8_t*>(buffer->get_buffer()->get_plane_ptr(0));
        m_rtsp->add_buffer(tmp, size);

        //std::cerr << "RTSP stage, buffer size: " << size << " size2: " << buffer->get_buffer()->get_plane_size(0) << std::endl;
        //const uint8_t* tmp = reinterpret_cast<const uint8_t*>(buffer->get_buffer()->get_plane_ptr(0));
        //process_video_buffer(tmp, size);
        //process_video_buffer_for_rtsp(tmp, size);
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
    uint32_t m_width = 3840;
    uint32_t m_height = 2160;
    uint32_t m_fps = 30;
    std::shared_ptr<GstRTSPServer> m_server = nullptr;
    std::shared_ptr<GstRTSPMountPoints> m_mounts = nullptr;

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
    RtspStageBuilder &width(const uint32_t &mp)
    {
        m_width = mp;
        return *this;
    }
    RtspStageBuilder &height(const uint32_t &mp)
    {
        m_height = mp;
        return *this;
    }
    RtspStageBuilder &fps(const uint32_t &mp)
    {
        m_fps = mp;
        return *this;
    }
    RtspStageBuilder &server(const std::shared_ptr<GstRTSPServer> &server)
    {
        m_server = server;
        return *this;
    }
    RtspStageBuilder &mount(const std::shared_ptr<GstRTSPMountPoints> &mounts)
    {
        m_mounts = mounts;
        return *this;
    }

    std::shared_ptr<RtspStage> build()
    {
        auto stage = std::make_shared<RtspStage>(m_name, m_type, RTSP_QUEUE_SIZE_DEFAULT,
                                         m_print_fps, m_width, m_height, m_fps, m_server, m_mounts);
        stage->configure(m_mount_point);
        return stage;
    }
};
