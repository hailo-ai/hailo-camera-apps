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
