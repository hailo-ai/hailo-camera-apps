#pragma once

// General includes
#include <algorithm>
#include <iostream>

// Media-Library includes
#include "media_library/encoder.hpp"

// Tappas includes
#include "hailo_common.hpp"

// Infra includes
#include "stage.hpp"
#include "buffer.hpp"
#include "file_module.hpp"

class FileStage : public ConnectedStage
{
private:
    std::string m_filepath;
    EncodingType m_type;
    FileModulePtr m_file;
    
public:
    FileStage(std::string name, size_t queue_size=1, bool leaky=false, bool print_fps=false) : 
        ConnectedStage(name, queue_size, leaky, print_fps)
    {
    }

    AppStatus create(std::string filepath, EncodingType type)
    {
        if (m_file == nullptr)
        {
            tl::expected<FileModulePtr, AppStatus> file_expected = FileModule::create(m_stage_name, filepath, type);
            if (!file_expected.has_value())
            {
                std::cerr << "Failed to create file module" << std::endl;
                REFERENCE_CAMERA_LOG_ERROR("Failed to create file module");
                return AppStatus::CONFIGURATION_ERROR;
            }
            m_file = file_expected.value();
            m_filepath = filepath;
            m_type = type;
        }
        return AppStatus::SUCCESS;
    }

    AppStatus init() override
    {
        if (m_file == nullptr)
        {
            std::cerr << "File " << m_file << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("File {}  not configured. Call configure()", m_file);
            return AppStatus::UNINITIALIZED;
        }
        m_file->start();
        return AppStatus::SUCCESS;
    }

    AppStatus deinit() override
    {
        if (m_file)
        {
            m_file->stop();
        }
        return AppStatus::SUCCESS;
    }

    AppStatus configure(std::string filepath, EncodingType type)
    {
        if (m_file == nullptr)
        {
            return create(filepath, type);
        }
        m_file->stop();
        m_file = nullptr;
        return create(filepath, type);
    }

    AppStatus process(BufferPtr data)
    {
        if (m_file == nullptr)
        {
            std::cerr << "File " << m_file << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("File {}  not configured. Call configure()", m_file);
            return AppStatus::UNINITIALIZED;
        }

        std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::SIZE);
        if (metadata.size() <= 0)
        {
            std::cerr << "File " << m_file << " got buffer of unknown size, add SizeMeta" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("File {}  not configured. Call configure()", m_file);
            return AppStatus::PIPELINE_ERROR;
        }
        SizeMetadataPtr size_metadata = std::dynamic_pointer_cast<SizeMetadata>(metadata[0]);
        size_t size = size_metadata->get_size();
        m_file->add_buffer(data->get_buffer(), size);

        return AppStatus::SUCCESS;
    }
};
