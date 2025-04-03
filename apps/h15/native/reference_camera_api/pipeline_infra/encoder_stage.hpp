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

class EncoderStage : public ConnectedStage
{
private:
    MediaLibraryEncoderPtr m_encoder;
    
public:
    EncoderStage(std::string name, size_t queue_size=1, bool leaky=false, bool print_fps=false) : 
        ConnectedStage(name, queue_size, leaky, print_fps) 
    {
        m_encoder = nullptr;
    }

    AppStatus create(MediaLibraryEncoderPtr encoder)
    {
        m_encoder = encoder;//TODO because the encoder not created in the stage i cant control its name
        m_encoder->subscribe(
            [this](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                // Keep in mind, this does not pass Buffer metadata from encoder input to the next stage
                // It is generally assumed that this is near the end of pipeline.
                BufferPtr wrapped_buffer = std::make_shared<Buffer>(buffer);
                SizeMetadataPtr size_meta = std::make_shared<SizeMetadata>(this->m_stage_name, size);
                wrapped_buffer->add_metadata(size_meta);
                this->send_to_subscribers(wrapped_buffer);
            });
        return AppStatus::SUCCESS;
    }

    AppStatus init() override
    {
        if (m_encoder == nullptr)
        {
            std::cerr << "Encoder " << m_stage_name << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Encoder {} not configured. Call configure()", m_stage_name);
            return AppStatus::UNINITIALIZED;
        }
        m_encoder->start();
        return AppStatus::SUCCESS;
    }

    AppStatus deinit() override
    {
        m_encoder->stop();
        return AppStatus::SUCCESS;
    }

    AppStatus configure(MediaLibraryEncoderPtr encoder)
    {
        if (m_encoder == nullptr)
        {
            return create(encoder);
        }
        m_encoder->stop();
        m_encoder = nullptr;
        return create(encoder);
    }

    AppStatus process(BufferPtr data)
    {
        if (m_encoder == nullptr)
        {
            std::cerr << "Encoder " << m_stage_name << " not initialized" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Encoder {} not initialized", m_stage_name);
            return AppStatus::UNINITIALIZED;
        }
        m_encoder->add_buffer(data->get_buffer());
        return AppStatus::SUCCESS;
    }
};
