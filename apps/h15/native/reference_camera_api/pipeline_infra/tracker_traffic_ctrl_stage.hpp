#pragma once
#include "hailo_objects.hpp"
#include "stage.hpp"
#include "buffer.hpp"
#include "queue.hpp"
#include "hailo_tracker.hpp"

#define TRACKER_UNCLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS (1)
#define TRACKER_CLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS (1)
#define TRACKER_TRAFFIC_QUEUE_SIZE_DEFAULT (5)

class TrackerTrafficCtrlStage : public ConnectedStage
{
  private:
    size_t m_tracked_unclassified_frame_block_cnt;
    size_t m_tracked_classified_frame_block_cnt;
    std::unordered_map<int, size_t> m_trackingcounts;

    int get_tracking_id(HailoDetectionPtr detection)
    {
        for (auto obj : detection->get_objects_typed(HAILO_UNIQUE_ID))
        {
            HailoUniqueIDPtr id = std::dynamic_pointer_cast<HailoUniqueID>(obj);
            if (id->get_mode() == TRACKING_ID)
            {
                return id->get_id();
            }
        }
        return 0;
    }

    bool is_classified(HailoDetectionPtr detection)
    {
        bool classified = false;
        for (auto classification_obj : detection->get_objects_typed(HAILO_CLASSIFICATION))
        {
            HailoClassificationPtr classification = std::dynamic_pointer_cast<HailoClassification>(classification_obj);
            if (classification->get_type() == HAILO_CLASSIFICATION)
            {
                classified = true;
                // std::cout << "Classification Label: " << classification->get_label() << std::endl;
            }
        }

        return classified;
    }

  public:
    TrackerTrafficCtrlStage(
        std::string name,
        size_t tracked_unclassified_frame_block_cnt = TRACKER_UNCLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS,
        size_t tracked_classified_frame_block_cnt = TRACKER_CLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS,
        size_t queue_size = TRACKER_TRAFFIC_QUEUE_SIZE_DEFAULT, bool leaky = false, bool print_fps = false)
        : ConnectedStage(name, queue_size, leaky, print_fps),
          m_tracked_unclassified_frame_block_cnt(tracked_unclassified_frame_block_cnt),
          m_tracked_classified_frame_block_cnt(tracked_classified_frame_block_cnt)
    {
    }

    AppStatus init() override
    {
        m_trackingcounts.clear();
        return AppStatus::SUCCESS;
    }

    AppStatus deinit() override
    {
        return AppStatus::SUCCESS;
    }

    AppStatus process(BufferPtr data)
    {
        // TODO: Cleann up m_trackingcounts once a while

        HailoROIPtr hailo_roi = data->get_roi();

        std::vector<HailoObjectPtr> detections_to_remove;
        for (auto obj : hailo_roi->get_objects_typed(HAILO_DETECTION))
        {
            bool remove_detection = false;
            HailoDetectionPtr detection = std::dynamic_pointer_cast<HailoDetection>(obj);
            int track_id = get_tracking_id(detection);
            if (track_id)
            {
                size_t tracked_count_fps_block_before_pass = 1;
                if (is_classified(detection))
                    tracked_count_fps_block_before_pass = m_tracked_classified_frame_block_cnt;
                else
                    tracked_count_fps_block_before_pass = m_tracked_unclassified_frame_block_cnt;

                // When classified/unclassified tracked object still does not exceed more than
                // tracked_count_fps_block_before_pass times, we will NOT let it pass to next subscribed.
                if (m_trackingcounts[track_id]++ < tracked_count_fps_block_before_pass)
                {
                    remove_detection = true;
                }
                else
                {
                    // The classified tracked object appears more than tracked_count_fps_block_before_pass we let it
                    // pass to subscribed and we reset the counter
                    m_trackingcounts.erase(track_id);
                }
            }

            // We record for the detection object that we don't want to let it pass to next subscriber
            if (remove_detection)
            {
                detections_to_remove.push_back(obj);
            }
        }

        for (auto obj_to_remove : detections_to_remove)
        {
            hailo_roi->remove_object(obj_to_remove);
        }

        data->add_time_stamp(m_stage_name);
        set_duration(data);
        send_to_subscribers(data);

        return AppStatus::SUCCESS;
    }
};

class TrackerTrafficCtrlStageBuild : public TrackerTrafficCtrlStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = TRACKER_TRAFFIC_QUEUE_SIZE_DEFAULT;
        bool m_leaky = false;
        size_t m_tracked_unclassified_frame_block_cnt = TRACKER_UNCLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS;
        size_t m_tracked_classified_frame_block_cnt = TRACKER_CLASSIFIED_FPS_BLOCK_COUNT_BEFORE_PASS;
        bool m_print_fps = false;

      public:
        Builder &set_stage_name(std::string name)
        {
            m_stage_name = name;
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
        Builder &set_classified_fps_to_block(size_t count)
        {
            m_tracked_classified_frame_block_cnt = count;
            return *this;
        }
        Builder &set_unclassified_fps_to_block(size_t count)
        {
            m_tracked_unclassified_frame_block_cnt = count;
            return *this;
        }
        Builder &set_printfps_opt(bool activate)
        {
            m_print_fps = activate;
            return *this;
        }

        std::shared_ptr<TrackerTrafficCtrlStage> buildptr() const
        {
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

            return std::make_shared<TrackerTrafficCtrlStage>(
                m_stage_name.value(), m_tracked_unclassified_frame_block_cnt, m_tracked_classified_frame_block_cnt,
                m_queue_size, m_leaky, m_print_fps);
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
