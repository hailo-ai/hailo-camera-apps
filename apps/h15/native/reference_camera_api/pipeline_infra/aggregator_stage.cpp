#include "aggregator_stage.hpp"
#include "reference_camera_logger.hpp"
#include "stage_debug.hpp"
#include <algorithm>
#include <optional>
#include <vector>
#include <memory>
#include <chrono>
#include <string>
#include <tl/expected.hpp>
#include "hailo_objects.hpp"
#include "reference_camera_perfetto.hpp"

// Internal class for Perfetto tracing to maintain ABI compatibility
class AggTracing
{
  private:
#ifdef HAVE_PERFETTO
    std::string m_counter_name_drop_rate;
    perfetto::CounterTrack m_counter_track_drop_rate;
    std::string m_counter_name_timeout;
    perfetto::CounterTrack m_counter_track_timeout;
#endif

  public:
    AggTracing(const std::string &name)
#ifdef HAVE_PERFETTO
        : m_counter_name_drop_rate("aggregator_" + name + "_drop_rate"),
          m_counter_track_drop_rate(perfetto::DynamicString(m_counter_name_drop_rate), "drop rate"),
          m_counter_name_timeout("aggregator_" + name + "_timeout"),
          m_counter_track_timeout(perfetto::DynamicString(m_counter_name_timeout), "timeout")
#endif
    {
    }

    void track_drop_rate(float drop_rate)
    {
        REFERENCE_CAMERA_TRACE_COUNTER(m_counter_track_drop_rate, drop_rate);
    }

    void track_timeout(std::chrono::milliseconds timeout)
    {
        REFERENCE_CAMERA_TRACE_COUNTER(m_counter_track_timeout, timeout.count());
    }
};

AggregatorStage::AggregatorStage(std::string name, bool blocking, std::string main_inlet_name, size_t main_queue_size,
                                 bool main_queue_leaky, std::string sub_inlet_name, size_t sub_queue_size,
                                 bool sub_queue_leaky, bool multi_scale, bool sync, float iou_threshold,
                                 float m_border_threshold, bool skip_migration, bool print_fps,
                                 std::optional<std::chrono::milliseconds> timeout,
                                 std::optional<std::chrono::milliseconds> min_timeout,
                                 std::optional<std::chrono::milliseconds> max_timeout,
                                 std::chrono::milliseconds timeout_adjustment_period, float drop_rate_threshold,
                                 std::chrono::milliseconds timeout_step_size, bool drop_rate_block)
    : ConnectedStage(name, main_queue_size, main_queue_leaky, print_fps, false), m_blocking(blocking),
      m_main_inlet_name(main_inlet_name), m_main_queue_size(main_queue_size), m_sub_inlet_name(sub_inlet_name),
      m_sub_queue_size(sub_queue_size), m_static_sub_frames(-1), m_multi_scale(multi_scale), m_sync(sync),
      m_iou_threshold(iou_threshold), m_border_threshold(m_border_threshold), m_skip_migration(skip_migration),
      m_timeout(timeout), m_min_timeout(min_timeout), m_max_timeout(max_timeout),
      m_timeout_adjustment_period(timeout_adjustment_period), m_drop_rate_threshold(drop_rate_threshold),
      m_timeout_step_size(timeout_step_size), m_drop_rate_block(drop_rate_block)
{
    m_queues.push_back(std::make_shared<Queue>(name, m_main_inlet_name, m_main_queue_size, main_queue_leaky));
    m_queues.push_back(std::make_shared<Queue>(name, m_sub_inlet_name, m_sub_queue_size, sub_queue_leaky));
    m_agg_tracing = std::make_unique<AggTracing>(name);
}

AggregatorStage::AggregatorStage(std::string name, bool blocking, int static_sub_frames, std::string main_inlet_name,
                                 size_t main_queue_size, bool main_queue_leaky, std::string sub_inlet_name,
                                 size_t sub_queue_size, bool sub_queue_leaky, bool multi_scale, bool sync,
                                 float iou_threshold, float m_border_threshold, bool skip_migration, bool print_fps,
                                 std::optional<std::chrono::milliseconds> timeout,
                                 std::optional<std::chrono::milliseconds> min_timeout,
                                 std::optional<std::chrono::milliseconds> max_timeout,
                                 std::chrono::milliseconds timeout_adjustment_period, float drop_rate_threshold,
                                 std::chrono::milliseconds timeout_step_size, bool drop_rate_block)
    : ConnectedStage(name, main_queue_size, main_queue_leaky, print_fps, false), m_blocking(blocking),
      m_main_inlet_name(main_inlet_name), m_main_queue_size(main_queue_size), m_sub_inlet_name(sub_inlet_name),
      m_sub_queue_size(sub_queue_size), m_static_sub_frames(static_sub_frames), m_multi_scale(multi_scale),
      m_sync(sync), m_iou_threshold(iou_threshold), m_border_threshold(m_border_threshold),
      m_skip_migration(skip_migration), m_timeout(timeout), m_min_timeout(min_timeout), m_max_timeout(max_timeout),
      m_timeout_adjustment_period(timeout_adjustment_period), m_drop_rate_threshold(drop_rate_threshold),
      m_timeout_step_size(timeout_step_size), m_drop_rate_block(drop_rate_block)
{
    m_queues.push_back(std::make_shared<Queue>(name, m_main_inlet_name, m_main_queue_size, main_queue_leaky));
    m_queues.push_back(std::make_shared<Queue>(name, m_sub_inlet_name, m_sub_queue_size, sub_queue_leaky));
    m_agg_tracing = std::make_unique<AggTracing>(name);
}

AggregatorStage::~AggregatorStage() = default;

void AggregatorStage::add_queue(std::string name)
{
}

HailoBBox AggregatorStage::create_flattened_bbox(const HailoBBox &bbox, const HailoBBox &parent_bbox)
{
    float xmin = parent_bbox.xmin() + bbox.xmin() * parent_bbox.width();
    float ymin = parent_bbox.ymin() + bbox.ymin() * parent_bbox.height();

    float width = bbox.width() * parent_bbox.width();
    float height = bbox.height() * parent_bbox.height();

    return HailoBBox(xmin, ymin, width, height);
}

void AggregatorStage::flatten_hailo_roi(HailoROIPtr roi, HailoROIPtr parent_roi, hailo_object_t filter_type)
{
    std::vector<HailoObjectPtr> objects = roi->get_objects();
    for (uint index = 0; index < objects.size(); index++)
    {
        if (objects[index]->get_type() == filter_type)
        {
            HailoROIPtr sub_obj_roi = std::dynamic_pointer_cast<HailoROI>(objects[index]);
            sub_obj_roi->set_bbox(std::move(create_flattened_bbox(sub_obj_roi->get_bbox(), roi->get_scaling_bbox())));
            parent_roi->add_object(sub_obj_roi);
            roi->remove_object(index);
            objects.erase(objects.begin() + index);
            index--;
        }
    }
}

void AggregatorStage::remove_exceeded_bboxes(HailoROIPtr hailo_tile_roi, float border_threshold)
{
    auto detections = hailo_common::get_hailo_detections(hailo_tile_roi);
    HailoBBox tile_bbox = hailo_tile_roi->get_scaling_bbox();

    for (const HailoDetectionPtr &detection : detections)
    {
        HailoBBox bbox = detection->get_bbox();
        bool exceed_xmin = (tile_bbox.xmin() != 0 && bbox.xmin() < border_threshold);
        bool exceed_xmax = (tile_bbox.xmax() != 1 && (1 - bbox.xmax()) < border_threshold);
        bool exceed_ymin = (tile_bbox.ymin() != 0 && bbox.ymin() < border_threshold);
        bool exceed_ymax = (tile_bbox.ymax() != 1 && (1 - bbox.ymax()) < border_threshold);

        if (exceed_xmin || exceed_xmax || exceed_ymin || exceed_ymax)
            hailo_tile_roi->remove_object(detection);
    }
}

float AggregatorStage::iou_calc(const HailoBBox &box_1, const HailoBBox &box_2)
{
    // Calculate IOU between two detection boxes
    const float width_of_overlap_area = std::min(box_1.xmax(), box_2.xmax()) - std::max(box_1.xmin(), box_2.xmin());
    const float height_of_overlap_area = std::min(box_1.ymax(), box_2.ymax()) - std::max(box_1.ymin(), box_2.ymin());
    const float positive_width_of_overlap_area = std::max(width_of_overlap_area, 0.0f);
    const float positive_height_of_overlap_area = std::max(height_of_overlap_area, 0.0f);
    const float area_of_overlap = positive_width_of_overlap_area * positive_height_of_overlap_area;
    const float box_1_area = (box_1.ymax() - box_1.ymin()) * (box_1.xmax() - box_1.xmin());
    const float box_2_area = (box_2.ymax() - box_2.ymin()) * (box_2.xmax() - box_2.xmin());
    // The IOU is a ratio of how much the boxes overlap vs their size outside the overlap.
    // Boxes that are similar will have a higher overlap threshold.
    return area_of_overlap / (box_1_area + box_2_area - area_of_overlap);
}

void AggregatorStage::nms(HailoROIPtr hailo_roi, const float iou_thr)
{
    // The network may propose multiple detections of similar size/score,
    // which are actually the same detection. We want to filter out the lesser
    // detections with a simple nms.

    std::vector<HailoDetectionPtr> objects = hailo_common::get_hailo_detections(hailo_roi);
    std::sort(objects.begin(), objects.end(),
              [](HailoDetectionPtr a, HailoDetectionPtr b) { return a->get_confidence() > b->get_confidence(); });

    for (uint index = 0; index < objects.size(); index++)
    {
        for (uint jindex = index + 1; jindex < objects.size(); jindex++)
        {
            if (objects[index]->get_class_id() == objects[jindex]->get_class_id())
            {
                // For each detection, calculate the IOU against each following detection.
                float iou = iou_calc(objects[index]->get_bbox(), objects[jindex]->get_bbox());
                // If the IOU is above threshold, then we have two similar detections,
                // and want to delete the one.
                if (iou >= iou_thr)
                {
                    // The detections are arranged in highest score order,
                    // so we want to erase the latter detection.
                    hailo_roi->remove_object(objects[jindex]);
                    objects.erase(objects.begin() + jindex);
                    jindex--; // Step back jindex since we just erased the current detection.
                }
            }
        }
    }
}

int AggregatorStage::count_subframes(BufferPtr main_buffer)
{
    int num_subframes = 0;
    bool count_static_sub_frames = (m_static_sub_frames >= 0);

    if (count_static_sub_frames)
    {
        num_subframes = m_static_sub_frames;
    }

    std::vector<MetadataPtr> metadata = main_buffer->get_metadata_of_type(MetadataType::EXPECTED_CROPS);
    if (metadata.size() > 0)
    {
        CroppingMetadataPtr cropping_metadata = std::dynamic_pointer_cast<CroppingMetadata>(metadata[0]);

        if (!count_static_sub_frames)
        {
            num_subframes = cropping_metadata->get_num_crops();
        }

        // remove the meta since we finished using it, to avoid confusing future aggregators
        main_buffer->remove_metadata(cropping_metadata);
    }

    return num_subframes;
}

void AggregatorStage::stamp_and_send(BufferPtr buffer)
{
    buffer->add_time_stamp(m_stage_name);
    set_duration(buffer);
    m_debug_counters->increment_output_frames();
    send_to_subscribers(buffer);

    m_tracing->trace_processing_end();
}

void AggregatorStage::timeout_adjustment()
{
    const auto time_now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    if (time_now - m_last_timeout_adjustment < m_timeout_adjustment_period)
    {
        return;
    }

    const float drop_rate = static_cast<float>(m_dropped_frames) / static_cast<float>(m_processed_frames);
    m_agg_tracing->track_drop_rate(drop_rate);
    if (m_timeout)
    {
        if (m_min_timeout && m_max_timeout)
        {
            auto t = m_timeout.value();
            const auto &low = m_min_timeout.value();
            const auto &high = m_max_timeout.value();
            if (drop_rate > m_drop_rate_threshold)
            {
                t += m_timeout_step_size;
            }
            else if (drop_rate < m_drop_rate_threshold)
            {
                t -= m_timeout_step_size;
            }
            m_timeout = std::clamp(t, low, high);
        }
        m_agg_tracing->track_timeout(m_timeout.value());
    }
    m_drop_rate = drop_rate;
    m_dropped_frames = 0;
    m_processed_frames = 0;
    m_last_timeout_adjustment = time_now;
}

void AggregatorStage::migrate_metadata(BufferPtr main_buffer, std::vector<BufferPtr> &subframes)
{
    if (m_skip_migration)
    {
        // If skip migration is set, we don't want to migrate metadata from subframes to main_buffer
        return;
    }
    // copy metadata from subframes to main frame
    // for (auto BufferPtr subframe : subframes)
    for (BufferPtr subframe : subframes)
    {
        if (m_multi_scale)
        {
            remove_exceeded_bboxes(subframe->get_roi(), m_border_threshold);
        }
        // Flatten subframe roi detections to main_buffer roi's scales.
        // Passing HAILO_DETECTION as a filter type here request to flatten only HailoDetection objects.
        // This passes ownership of the rois to the main buffer
        flatten_hailo_roi(subframe->get_roi(), main_buffer->get_roi(), HAILO_DETECTION);
    }

    if (m_multi_scale)
    {
        // Perform NMS on the main frame's detections after aggragation is done
        nms(main_buffer->get_roi(), m_iou_threshold);
    }
}

void AggregatorStage::loop()
{
    init();

    while (!m_end_of_stream)
    {
        // the first queue is the one that is condisidered the "main stream"
        BufferPtr main_buffer = m_queues[0]->pop();
        m_tracing->trace_processing_start();
        m_debug_counters->increment_input_frames();
        if (main_buffer == nullptr && m_end_of_stream)
        {
            break;
        }

        // Check if the main buffer has cropping metadata
        int num_subframes = count_subframes(main_buffer);
        // If no subframes are requested, send the main buffer as is
        if (num_subframes == 0)
        {
            stamp_and_send(main_buffer);
            continue;
        }

        // Check how many sub frames are available, wait if blocking is enabled
        std::vector<BufferPtr> subframes;
        if (m_blocking)
        {
            if (m_sync && num_subframes == 1)
            {
                // sync case
                std::optional<std::chrono::milliseconds> timeout = m_timeout;
                if (m_first_sync)
                {
                    m_first_sync = false;
                    timeout = std::nullopt;
                }
                if (m_drop_rate_block && m_drop_rate >= 0.99f)
                {
                    REFERENCE_CAMERA_LOG_WARN("[{}] drop rate 100%, allowing indefinite timeout until recovered.",
                                              m_stage_name);
                    timeout = std::nullopt;
                }

                uint64_t mainframe_timestamp = main_buffer->get_buffer()->isp_timestamp_ns;
                auto samples_start = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch());
                uint64_t subframe_timestamp = m_queues[1]->check_timestamp(timeout);
                auto sampling_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch()) -
                                     samples_start;
                if (subframe_timestamp == 0)
                {
                    // if we reached here, then the queue is empty and we are flushing (or timed out)
                    stamp_and_send(main_buffer);
                    continue;
                }

                // main frame is newer than sub frame
                while ((mainframe_timestamp > subframe_timestamp) && (subframe_timestamp != 0))
                {
                    // drop the oldest subframe
                    m_queues[1]->pop();
                    m_processed_frames++;
                    m_dropped_frames++;
                    // check the next subframe
                    if (timeout.has_value())
                    {
                        timeout = m_timeout.value() - sampling_time;
                    }
                    if (timeout.has_value() && timeout <= std::chrono::milliseconds(0))
                    {
                        subframe_timestamp = 0; // skip if acumulated time is more than originally requested
                    }
                    else
                    {
                        subframe_timestamp = m_queues[1]->check_timestamp(timeout);
                        sampling_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch()) -
                                        samples_start;
                    }
                }

                // timestamps match
                if (mainframe_timestamp == subframe_timestamp)
                {
                    subframes.push_back(m_queues[1]->pop());
                    m_processed_frames++;
                    m_debug_counters->increment_extra_counter(static_cast<int>(AggregatorExtraCounters::SUB_FRAMES));
                    if (subframes[0] == nullptr && m_end_of_stream)
                    {
                        deinit();
                        m_tracing->trace_processing_end();
                        return;
                    }
                }
                else
                {
                    // timestamps don't match, main frame is older
                    num_subframes = 0; // pass the main frame as is
                }

                timeout_adjustment();
            }
            else
            {
                // non-sync case
                for (int i = 0; i < num_subframes; i++)
                {
                    subframes.push_back(m_queues[1]->pop());
                    m_debug_counters->increment_extra_counter(static_cast<int>(AggregatorExtraCounters::SUB_FRAMES));
                    if (subframes[i] == nullptr && m_end_of_stream)
                    {
                        deinit();
                        m_tracing->trace_processing_end();
                        return;
                    }
                }
            }
        }
        else
        {
            // leaky case
            if (m_queues[1]->size() >= num_subframes)
            {
                for (int i = 0; i < num_subframes; i++)
                {
                    subframes.push_back(m_queues[1]->pop());
                    m_debug_counters->increment_extra_counter(static_cast<int>(AggregatorExtraCounters::SUB_FRAMES));
                    if (subframes[i] == nullptr && m_end_of_stream)
                    {
                        deinit();
                        m_tracing->trace_processing_end();
                        return;
                    }
                }
            }
        }

        // migrate metadata from subframes to main buffer
        migrate_metadata(main_buffer, subframes);

        // pass the main_buffer to the subscribers
        stamp_and_send(main_buffer);

        trace_fps();
    }

    deinit();
}

AppStatus AggregatorStage::init()
{
    m_debug_counters = std::make_shared<AggregatorCounters>(m_stage_name);
    return AppStatus::SUCCESS;
}

AppStatus AggregatorStage::deinit()
{
    for (auto &queue : m_queues)
    {
        queue->flush();
    }

    return AppStatus::SUCCESS;
}

// Builder implementation
AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_blocking(bool blocking)
{
    m_blocking = blocking;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_static_subframes_opt(int num)
{
    m_static_sub_frames = num;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_main_inlet_name(std::string name)
{
    m_main_inlet_name = name;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_main_queue_size(size_t size)
{
    m_main_queue_size = size;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_main_leaky(bool leaky)
{
    m_main_queue_leaky = leaky;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_sub_inlet_name(std::string name)
{
    m_sub_inlet_name = name;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_sub_queue_size(size_t size)
{
    m_sub_queue_size = size;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_sub_leaky(bool leaky)
{
    m_sub_queue_leaky = leaky;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_multiscale_opt(bool multi_scale)
{
    m_multi_scale = multi_scale;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_sync_opt(bool sync)
{
    m_sync = sync;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_skip_migration_opt(bool skip)
{
    m_skip_migration = skip;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_printfps_opt(bool print)
{
    m_print_fps = print;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_iou_threshold_opt(float threshold)
{
    m_iou_threshold = threshold;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_border_threshold_opt(float threshold)
{
    m_border_threshold = threshold;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_timeout_opt(
    std::optional<std::chrono::milliseconds> timeout)
{
    m_timeout = timeout;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_min_timeout_opt(
    std::optional<std::chrono::milliseconds> min_timeout)
{
    m_min_timeout = min_timeout;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_max_timeout_opt(
    std::optional<std::chrono::milliseconds> max_timeout)
{
    m_max_timeout = max_timeout;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_timeout_adjustment_period(
    std::chrono::milliseconds period)
{
    m_timeout_adjustment_period = period;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_drop_rate_threshold(float threshold)
{
    m_drop_rate_threshold = threshold;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_timeout_step_size(std::chrono::milliseconds step_size)
{
    m_timeout_step_size = step_size;
    return *this;
}

AggregatorStageBuild::Builder &AggregatorStageBuild::Builder::set_drop_rate_block(bool block)
{
    m_drop_rate_block = block;
    return *this;
}

std::shared_ptr<AggregatorStage> AggregatorStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(m_main_inlet_name.has_value(), "set_main_inlet_name");
    THROW_IF_MISSING(m_sub_inlet_name.has_value(), "set_sub_inlet_name");

    return std::make_shared<AggregatorStage>(
        m_stage_name.value(), m_blocking, m_static_sub_frames, m_main_inlet_name.value(), m_main_queue_size,
        m_main_queue_leaky, m_sub_inlet_name.value(), m_sub_queue_size, m_sub_queue_leaky, m_multi_scale, m_sync,
        m_iou_threshold, m_border_threshold, m_skip_migration, m_print_fps, m_timeout, m_min_timeout, m_max_timeout,
        m_timeout_adjustment_period, m_drop_rate_threshold, m_timeout_step_size, m_drop_rate_block);
}

AggregatorStageBuild::Builder AggregatorStageBuild::create()
{
    return Builder();
}
