#pragma once

#include "stage.hpp"
#include "hailo_common.hpp"
#include <memory>
#include "media_library/media_library.hpp"
#include "media_library/media_library_types.hpp"
#include "hailo/hailort.hpp"
#include <cstdlib>
#include <ctime>

class AnalyticsDBStage : public ConnectedStage
{
  private:
    std::shared_ptr<MediaLibrary> m_media_library;
    std::string m_analytics_data_id;
    AnalyticsType m_type;

    AppStatus process_instance_segmentation(BufferPtr data, HailoMediaLibraryBufferPtr media_lib_buffer)
    {
        auto &analytics_db = m_media_library->get_analytics_db();
        auto application_analytics_config = analytics_db.get_application_analytics_config();
        if (application_analytics_config.instance_segmentation_analytics_config.find(m_analytics_data_id) ==
            application_analytics_config.instance_segmentation_analytics_config.end())
        {
            REFERENCE_CAMERA_LOG_ERROR("Analytics config not found for ID: {}", m_analytics_data_id);
            return AppStatus::MEDIA_LIBRARY_ERROR;
        }
        auto &analytics_config_for_id =
            application_analytics_config.instance_segmentation_analytics_config.at(m_analytics_data_id);
        uint32_t ai_width = analytics_config_for_id.width;
        uint32_t ai_height = analytics_config_for_id.height;

        auto hailo_segmetations = hailo_common::get_hailo_segmentations(data->get_roi());
        std::vector<hailo_detection_with_byte_mask_t> segmentations = {};
        for (const auto &hailo_segmentation : hailo_segmetations)
        {
            hailo_detection_with_byte_mask_t segmentation = hailo_segmentation->get_segmentation();
            segmentation.box.x_min = static_cast<uint32_t>(
                std::max(std::floor(segmentation.box.x_min * static_cast<float32_t>(ai_width)), 0.0f));
            segmentation.box.y_min = static_cast<uint32_t>(
                std::max(std::floor(segmentation.box.y_min * static_cast<float32_t>(ai_height)), 0.0f));

            segmentation.box.x_max =
                std::min(static_cast<uint32_t>(std::ceil(segmentation.box.x_max * static_cast<float32_t>(ai_width))),
                         static_cast<uint32_t>(ai_width));

            segmentation.box.y_max =
                std::min(static_cast<uint32_t>(std::ceil(segmentation.box.y_max * static_cast<float32_t>(ai_height))),
                         static_cast<uint32_t>(ai_height));

            auto roi_width = static_cast<uint32_t>(segmentation.box.x_max - segmentation.box.x_min);
            auto roi_height = static_cast<uint32_t>(segmentation.box.y_max - segmentation.box.y_min);
            REFERENCE_CAMERA_LOG_TRACE("Segmentation found: label {}, score {}, bbox: [{}, {}, {}, {}], mask size: {}, "
                                       "ROI width: {}, ROI height: {}",
                                       segmentation.class_id, segmentation.score, segmentation.box.x_min,
                                       segmentation.box.y_min, segmentation.box.x_max, segmentation.box.y_max,
                                       segmentation.mask_size, roi_width, roi_height);
            segmentations.push_back(segmentation);
        }

        // Send timestamp and instance segmentations to the analytics database
        auto isp_timestamp = data->get_buffer()->isp_timestamp_ns;
        auto timestamp = std::chrono::time_point<std::chrono::steady_clock>(std::chrono::nanoseconds(isp_timestamp));

        REFERENCE_CAMERA_LOG_DEBUG("Adding {} segmentations to analytics DB at id {}, timestamp {}",
                                   segmentations.size(), m_analytics_data_id, isp_timestamp);

        InstanceSegmentationAnalyticsData db_data = {
            .ts = timestamp, .analytics_buffer = segmentations, .medialib_buffer_ptr = media_lib_buffer};
        auto ret = analytics_db.add_instance_segmentation_entry(m_analytics_data_id, db_data);

        if (ret != media_library_return::MEDIA_LIBRARY_SUCCESS)
        {
            REFERENCE_CAMERA_LOG_ERROR("Failed to add entry to analytics DB");
            return AppStatus::MEDIA_LIBRARY_ERROR;
        }

        return AppStatus::SUCCESS;
    }

  public:
    AnalyticsDBStage(const std::string &name, std::shared_ptr<MediaLibrary> media_library, size_t queue_size,
                     bool leaky, const std::string &analytics_data_id,
                     AnalyticsType type = AnalyticsType::INSTANCE_SEGMENTATION, bool print_fps = false)
        : ConnectedStage(name, queue_size, leaky, print_fps), m_media_library(media_library),
          m_analytics_data_id(analytics_data_id), m_type(type)
    {
        switch (m_type)
        {
        case AnalyticsType::INSTANCE_SEGMENTATION:
            break;
        case AnalyticsType::DETECTION:
            REFERENCE_CAMERA_LOG_ERROR("DETECTION analytics type is not supported in AnalyticsDBStage");
            throw std::runtime_error("DETECTION analytics type is not supported in AnalyticsDBStage");
        default:
            REFERENCE_CAMERA_LOG_ERROR("Unsupported AnalyticsType: {}", static_cast<int>(m_type));
            throw std::runtime_error("Unsupported AnalyticsType in AnalyticsDBStage");
        }
    }

    AppStatus process(BufferPtr data) override
    {
        REFERENCE_CAMERA_LOG_DEBUG("[{}] process() called", m_stage_name);
        std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::TENSOR);
        if (metadata.empty())
        {
            REFERENCE_CAMERA_LOG_ERROR("No TENSOR metadata found in the buffer");
            return AppStatus::PIPELINE_ERROR;
        }

        TensorMetadataPtr buffer_metadata_ptr = std::dynamic_pointer_cast<TensorMetadata>(metadata[0]);
        HailoMediaLibraryBufferPtr media_lib_buffer = buffer_metadata_ptr->get_buffer()->get_buffer();
        media_lib_buffer->sync_end();

        AppStatus status = AppStatus::SUCCESS;

        switch (m_type)
        {
        case AnalyticsType::INSTANCE_SEGMENTATION:
            status = process_instance_segmentation(data, media_lib_buffer);
            break;
        case AnalyticsType::DETECTION:
        default:
            REFERENCE_CAMERA_LOG_ERROR("Unsupported AnalyticsType: {}", static_cast<int>(m_type));
            return AppStatus::MEDIA_LIBRARY_ERROR;
        }

        if (status != AppStatus::SUCCESS)
        {
            return status;
        }
        
        send_to_subscribers(data);
        return AppStatus::SUCCESS;
    }

    AppStatus deinit() override
    {
        return AppStatus::SUCCESS;
    }
};

class AnalyticsDBStageBuild : public AnalyticsDBStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::shared_ptr<MediaLibrary> m_media_library;
        size_t m_queue_size = 10;
        bool m_leaky = false;
        bool m_print_fps = false;
        std::optional<std::string> m_analytics_data_id;
        AnalyticsType m_type = AnalyticsType::INSTANCE_SEGMENTATION;

      public:
        Builder &set_stage_name(std::string name)
        {
            m_stage_name = name;
            return *this;
        }

        Builder &set_media_library(const std::shared_ptr<MediaLibrary> &media_library)
        {
            m_media_library = media_library;
            return *this;
        }

        Builder &set_queue_size(size_t size)
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

        Builder &set_analytics_data_id(const std::string &analytics_data_id)
        {
            m_analytics_data_id = analytics_data_id;
            return *this;
        }

        Builder &set_type(AnalyticsType type)
        {
            m_type = type;
            return *this;
        }

        std::shared_ptr<AnalyticsDBStage> buildptr() const
        {
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
            THROW_IF_MISSING(m_media_library != nullptr, "set_media_library");
            THROW_IF_MISSING(m_analytics_data_id.has_value(), "set_analytics_data_id");

            return std::make_shared<AnalyticsDBStage>(m_stage_name.value(), m_media_library, m_queue_size, m_leaky,
                                                      m_analytics_data_id.value(), m_type, m_print_fps);
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
