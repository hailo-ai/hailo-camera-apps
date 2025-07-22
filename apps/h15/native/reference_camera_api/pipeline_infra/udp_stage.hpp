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
#include "udp_module.hpp"

#define UDP_QUEUE_SIZE_DEFAULT (1)

class UdpStage : public ConnectedStage
{
  private:
    std::string m_host;
    std::string m_port;
    EncodingType m_type;
    UdpModulePtr m_udp;

  public:
    inline UdpStage(std::string name, size_t queue_size = UDP_QUEUE_SIZE_DEFAULT, bool leaky = false, bool print_fps = false)
        : ConnectedStage(name, queue_size, leaky, print_fps)
    {
    }

    inline AppStatus create(std::string host, std::string port, EncodingType type)
    {
        if (m_udp == nullptr)
        {
            tl::expected<UdpModulePtr, AppStatus> udp_expected =
                UdpModule::create(m_stage_name, host, port, type, m_print_fps);
            if (!udp_expected.has_value())
            {
                std::cout << "Failed to create udp" << std::endl;
                REFERENCE_CAMERA_LOG_ERROR("Failed to create udp");
                return AppStatus::CONFIGURATION_ERROR;
            }
            m_udp = udp_expected.value();
            m_host = host;
            m_port = port;
            m_type = type;
        }
        return AppStatus::SUCCESS;
    }

    inline AppStatus init() override
    {
        if (m_udp == nullptr)
        {
            std::cerr << "Udp " << m_stage_name << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Udp  {} not configured. Call configure()", m_stage_name);
            return AppStatus::UNINITIALIZED;
        }
        m_udp->start();
        return AppStatus::SUCCESS;
    }

    inline AppStatus deinit() override
    {
        m_udp->stop();
        return AppStatus::SUCCESS;
    }

    inline AppStatus configure(std::string host, std::string port, EncodingType type)
    {
        if (m_udp == nullptr)
        {
            return create(host, port, type);
        }
        m_udp->stop();
        m_udp = nullptr;
        return create(host, port, type);
    }

    inline AppStatus process(BufferPtr data)
    {
        if (m_udp == nullptr)
        {
            std::cerr << "Udp " << m_stage_name << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Udp {} not configured. Call configure()", m_stage_name);
            return AppStatus::UNINITIALIZED;
        }

        std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::SIZE);
        if (metadata.size() <= 0)
        {
            std::cerr << "Udp " << m_stage_name << " got buffer of unknown size, add SizeMeta" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Udp {} got buffer of unknown size, add SizeMeta");
            return AppStatus::PIPELINE_ERROR;
        }
        SizeMetadataPtr size_metadata = std::dynamic_pointer_cast<SizeMetadata>(metadata[0]);
        size_t size = size_metadata->get_size();
        m_udp->add_buffer(data->get_buffer(), size);

        return AppStatus::SUCCESS;
    }
};


class UdpStageBuild : public UdpStage
{
public:
    class Builder {
    
    private:
        std::optional<std::string>  m_stage_name;
        size_t                      m_queue_size=UDP_QUEUE_SIZE_DEFAULT;
        bool                        m_leaky=false;
        bool                        m_print_fps=false;        
    
    public:
        Builder& set_stage_name(std::string name) { m_stage_name=name; return *this;}
        Builder& set_queue_size_opt(size_t size) { m_queue_size=size; return *this;}
        Builder& set_leaky_opt(bool activate) { m_leaky=activate; return *this;}
        Builder& set_printfps_opt(bool activate) { m_print_fps=activate; return *this;}

        std::shared_ptr<UdpStage> buildptr() const { 
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

            return std::make_shared<UdpStage>(m_stage_name.value(), m_queue_size, m_leaky, m_print_fps);}
    };

    static Builder create() { return Builder(); }

};
