#pragma once

#include "stage.hpp"
#include "buffer.hpp"
#include "hailo_common.hpp"
#include "stage_debug.hpp"
#include "hailo_objects.hpp"
#include <algorithm>
#include <optional>
#include <vector>
#include <memory>
#include <chrono>
#include <string>
#include "reference_camera_perfetto.hpp"

// Forward declaration
class AggTracing;

class AggregatorStage : public ConnectedStage
{
  protected:
    bool m_blocking;
    std::string m_main_inlet_name;
    size_t m_main_queue_size;
    std::string m_sub_inlet_name;
    size_t m_sub_queue_size;
    int m_static_sub_frames;
    bool m_multi_scale;
    bool m_sync;
    float m_iou_threshold;
    float m_border_threshold;
    bool m_skip_migration;
    std::optional<std::chrono::milliseconds> m_timeout;
    std::optional<std::chrono::milliseconds> m_min_timeout;
    std::optional<std::chrono::milliseconds> m_max_timeout;
    bool m_first_sync = true;
    int m_dropped_frames = 0;
    int m_processed_frames = 0;
    float m_drop_rate = 0.0f;
    std::chrono::milliseconds m_last_timeout_adjustment = std::chrono::milliseconds(0);
    std::chrono::milliseconds m_timeout_adjustment_period;
    float m_drop_rate_threshold;
    std::chrono::milliseconds m_timeout_step_size;
    bool m_drop_rate_block;
    std::unique_ptr<AggTracing> m_agg_tracing;

  public:
    AggregatorStage(std::string name, bool blocking, std::string main_inlet_name, size_t main_queue_size,
                    bool main_queue_leaky, std::string sub_inlet_name, size_t sub_queue_size, bool sub_queue_leaky,
                    bool multi_scale = false, bool sync = false, float iou_threshold = 0.3,
                    float m_border_threshold = 0.1, bool skip_migration = false, bool print_fps = false,
                    std::optional<std::chrono::milliseconds> timeout = std::nullopt,
                    std::optional<std::chrono::milliseconds> min_timeout = std::nullopt,
                    std::optional<std::chrono::milliseconds> max_timeout = std::nullopt,
                    std::chrono::milliseconds timeout_adjustment_period = std::chrono::milliseconds(1000),
                    float drop_rate_threshold = 0.2,
                    std::chrono::milliseconds timeout_step_size = std::chrono::milliseconds(10),
                    bool drop_rate_block = false);

    AggregatorStage(std::string name, bool blocking, int static_sub_frames, std::string main_inlet_name,
                    size_t main_queue_size, bool main_queue_leaky, std::string sub_inlet_name, size_t sub_queue_size,
                    bool sub_queue_leaky, bool multi_scale = false, bool sync = false, float iou_threshold = 0.3,
                    float m_border_threshold = 0.1, bool skip_migration = false, bool print_fps = false,
                    std::optional<std::chrono::milliseconds> timeout = std::nullopt,
                    std::optional<std::chrono::milliseconds> min_timeout = std::nullopt,
                    std::optional<std::chrono::milliseconds> max_timeout = std::nullopt,
                    std::chrono::milliseconds timeout_adjustment_period = std::chrono::milliseconds(1000),
                    float drop_rate_threshold = 0.2,
                    std::chrono::milliseconds timeout_step_size = std::chrono::milliseconds(10),
                    bool drop_rate_block = false);

    ~AggregatorStage();

    void add_queue(std::string name) override;
    HailoBBox create_flattened_bbox(const HailoBBox &bbox, const HailoBBox &parent_bbox);
    void flatten_hailo_roi(HailoROIPtr roi, HailoROIPtr parent_roi, hailo_object_t filter_type);

    /**
     * Remove detections close to the boundary of the tile.
     * Not including tile borders that located on one of the borders of the full frame.
     *
     * @param[in] hailo_tile_roi  HailoTileROIPtr taken from the buffer.
     * @param[in] border_threshold    float.  threshold - 0 - 1 value of 'close to border' ratio.
     * @return void.
     */
    void remove_exceeded_bboxes(HailoROIPtr hailo_tile_roi, float border_threshold);
    float iou_calc(const HailoBBox &box_1, const HailoBBox &box_2);

    /**
     * @brief Perform IOU based NMS on detection objects of HailoRoi
     *
     * @param hailo_roi  -  HailoROIPtr
     *        The HailoROI contains detections to perform NMS on.
     *
     * @param iou_thr  -  float
     *        Threshold for IOU filtration
     */
    void nms(HailoROIPtr hailo_roi, const float iou_thr);
    int count_subframes(BufferPtr main_buffer);
    void stamp_and_send(BufferPtr buffer);
    void timeout_adjustment();
    void migrate_metadata(BufferPtr main_buffer, std::vector<BufferPtr> &subframes);

    void loop() override;
    AppStatus init() override;
    AppStatus deinit() override;
};

class AggregatorStageBuild : public AggregatorStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        bool m_blocking = true;
        int m_static_sub_frames = -1;
        std::optional<std::string> m_main_inlet_name;
        size_t m_main_queue_size = 10;
        bool m_main_queue_leaky = false;

        std::optional<std::string> m_sub_inlet_name;
        size_t m_sub_queue_size = 10;
        bool m_sub_queue_leaky = false;
        bool m_multi_scale = false;
        bool m_sync = false;
        bool m_skip_migration = false;
        bool m_print_fps = false;
        float m_iou_threshold = 0.3;
        float m_border_threshold = 0.1;
        std::optional<std::chrono::milliseconds> m_timeout = std::nullopt;
        std::optional<std::chrono::milliseconds> m_min_timeout = std::nullopt;
        std::optional<std::chrono::milliseconds> m_max_timeout = std::nullopt;
        std::chrono::milliseconds m_timeout_adjustment_period = std::chrono::milliseconds(1000);
        float m_drop_rate_threshold = 0.2;
        std::chrono::milliseconds m_timeout_step_size = std::chrono::milliseconds(10);
        bool m_drop_rate_block = false;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_blocking(bool blocking);
        Builder &set_static_subframes_opt(int num);
        Builder &set_main_inlet_name(std::string name);
        Builder &set_main_queue_size(size_t size);
        Builder &set_main_leaky(bool leaky);
        Builder &set_sub_inlet_name(std::string name);
        Builder &set_sub_queue_size(size_t size);
        Builder &set_sub_leaky(bool leaky);
        Builder &set_multiscale_opt(bool multi_scale);
        Builder &set_sync_opt(bool sync);
        Builder &set_skip_migration_opt(bool skip);
        Builder &set_printfps_opt(bool print);
        Builder &set_iou_threshold_opt(float threshold);
        Builder &set_border_threshold_opt(float threshold);
        Builder &set_timeout_opt(std::optional<std::chrono::milliseconds> timeout);
        Builder &set_min_timeout_opt(std::optional<std::chrono::milliseconds> min_timeout);
        Builder &set_max_timeout_opt(std::optional<std::chrono::milliseconds> max_timeout);
        Builder &set_timeout_adjustment_period(std::chrono::milliseconds period);
        Builder &set_drop_rate_threshold(float threshold);
        Builder &set_timeout_step_size(std::chrono::milliseconds step_size);
        Builder &set_drop_rate_block(bool block);
        std::shared_ptr<AggregatorStage> buildptr() const;
    };

    static Builder create();
};
