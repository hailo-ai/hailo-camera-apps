#pragma once

// Infra includes
#include "stage.hpp"
#include "buffer.hpp"
#include "queue.hpp"

// Tappas includes
#include "hailo_objects.hpp"
#include "hailo_common.hpp"

class PersistStage : public ConnectedStage
{
  private:
    std::vector<HailoDetectionPtr> m_detections;
    size_t m_expiration_threshold;
    size_t m_count = 0;

  public:
    PersistStage(std::string name, size_t expiration = 5, size_t queue_size = 5, bool leaky = false,
                 bool print_fps = false)
        : ConnectedStage(name, queue_size, leaky, print_fps), m_expiration_threshold(expiration)
    {
    }

    AppStatus process(BufferPtr data)
    {
        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        HailoROIPtr hailo_roi = data->get_roi();

        std::vector<HailoDetectionPtr> incoming_detections = hailo_common::get_hailo_detections(hailo_roi);
        if (incoming_detections.size() > 0)
        {
            m_detections = incoming_detections;
        }
        else if (m_detections.size() > 0)
        {
            hailo_common::add_detection_pointers(hailo_roi, m_detections);
            ++m_count;
            if (m_count >= m_expiration_threshold)
            {
                m_detections.clear();
                m_count = 0;
            }
        }

        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        if (m_print_fps)
        {
            std::cout << "Persist time = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()
                      << "[microseconds]" << std::endl;
        }
        REFERENCE_CAMERA_LOG_DEBUG("Persist time = {}[microseconds]",
                                   std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
        data->add_time_stamp(m_stage_name);
        set_duration(data);
        send_to_subscribers(data);

        return AppStatus::SUCCESS;
    }
};

class PersistStageBuild : public PersistStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_expiration = 5;
        size_t m_queue_size = 5;
        bool m_leaky = false;
        bool m_print_fps = false;

      public:
        Builder &set_stage_name(std::string name)
        {
            m_stage_name = name;
            return *this;
        }
        Builder &set_expiration_opt(size_t expiration)
        {
            m_expiration = expiration;
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

        std::shared_ptr<PersistStage> buildptr() const
        {
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

            return std::make_shared<PersistStage>(m_stage_name.value(), m_expiration, m_queue_size, m_leaky,
                                                  m_print_fps);
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
