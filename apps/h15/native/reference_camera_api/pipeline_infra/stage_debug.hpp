#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <string>
#include <cstring>
#include <iomanip> // for std::setw and std::left
#include "reference_camera_logger.hpp"
// Base class: StageDebugCounters
class StageDebugCounters {
public:
    std::string stage_name;
    StageDebugCounters(const std::string& stage_name)
        : stage_name(stage_name), num_input_frames(0), num_dropped_frames(0),
          num_output_frames(0), num_available_buffers(0), num_failed_acquire_buffer(0) {}

    // Setters and Getters for the base counters
    void increment_input_frames() { num_input_frames++; }
    void increment_dropped_frames() { num_dropped_frames++; }
    void increment_output_frames() { num_output_frames++; }
    void increment_available_buffers() { num_available_buffers++; }
    void increment_failed_acquire_buffer() { num_failed_acquire_buffer++; }

    void increment_by_val_input_frames(int count) { num_input_frames += count; }
    void increment_by_val_dropped_frames(int count) { num_dropped_frames += count; }
    void increment_by_val_output_frames(int count) { num_output_frames += count; }
    void increment_by_val_available_buffers(int count) { num_available_buffers += count; }
    void increment_by_val_failed_acquire_buffer(int count) { num_failed_acquire_buffer += count; }

    void set_input_frames(int count) { num_input_frames = count; }
    void set_dropped_frames(int count) { num_dropped_frames = count; }
    void set_output_frames(int count) { num_output_frames = count; }
    void set_available_buffers(int count) { num_available_buffers = count; }
    void set_failed_acquire_buffer(int count) { num_failed_acquire_buffer = count; }

    
    void increment_extra_counter(int counter_index = 0) {
        validate_index(counter_index);
        extra_counters[counter_index]++;
    }

    void increment_by_val_extra_counter(int count, int counter_index = 0) {
        validate_index(counter_index);
        extra_counters[counter_index] += count;
    }

    void set_extra_counter(int count, int counter_index = 0) {
        validate_index(counter_index);
        extra_counters[counter_index] = count;
    }


    virtual void write_additional_counter(std::ofstream& file) const = 0;
    virtual ~StageDebugCounters() = default;


protected:
    int num_input_frames;
    int num_dropped_frames;
    int num_output_frames;
    int num_available_buffers;
    int num_failed_acquire_buffer;
    int* extra_counters;
    
    virtual size_t enum_size() const = 0;
    void validate_index(int counter_index) {
        if (counter_index < 0 || counter_index >= static_cast<int>(enum_size())) {
            throw std::out_of_range("Invalid counter index in Extra Counters");
        }
    }
};

enum class PostProcessExtraCounters
{
    POST_PROCESS_EXTRA_COUNTERS_SIZE  /* should be last */
};
class PostProcessCounters : public StageDebugCounters {
public:
    PostProcessCounters(const std::string& stage_name) : StageDebugCounters(stage_name) {
        extra_counters = new int[enum_size()]();
    }

    void write_additional_counter(std::ofstream& file) const override {
        return;
    }

protected:
    size_t enum_size() const override {
        return static_cast<size_t>(PostProcessExtraCounters::POST_PROCESS_EXTRA_COUNTERS_SIZE);
    }
};

enum class CropsExtraCounters
{
    CROPS = 0,
    DROPPED,
    CROP_EXTRA_COUNTERS_SIZE  /* should be last */
};
class CropsCounters : public StageDebugCounters {
public:
    CropsCounters(const std::string& stage_name) : StageDebugCounters(stage_name) {
        extra_counters = new int[enum_size()]();
    }

    void write_additional_counter(std::ofstream& file) const override {
        if (file.is_open()) {
            file << "crops: " << extra_counters[static_cast<int>(CropsExtraCounters::CROPS)];
            file << " dropped: " << extra_counters[static_cast<int>(CropsExtraCounters::DROPPED)];
        }
    }
protected:
    size_t enum_size() const override {
        return static_cast<size_t>(CropsExtraCounters::CROP_EXTRA_COUNTERS_SIZE);
    }
};

enum class OverlayExtraCounters
{
    DETECTIONS = 0,
    LANDMARKS,
    OVERLAY_EXTRA_COUNTERS_SIZE  /* should be last */
};

class OverlayCounters : public StageDebugCounters {
public:
    OverlayCounters(const std::string& stage_name) : StageDebugCounters(stage_name) {
        extra_counters = new int[enum_size()]();
    }

    void write_additional_counter(std::ofstream& file) const override {
        if (file.is_open()) {
            file << "detections: " << extra_counters[static_cast<int>(OverlayExtraCounters::DETECTIONS)];
            file << " landmarks: " << extra_counters[static_cast<int>(OverlayExtraCounters::LANDMARKS)];
        }
    }
protected:
    size_t enum_size() const override {
        return static_cast<size_t>(OverlayExtraCounters::OVERLAY_EXTRA_COUNTERS_SIZE);
    }
};

enum class AIExtraCounters
{
    TENSORS = 0,
    AI_EXTRA_COUNTERS_SIZE  /* should be last */
};
class AIStageCounters : public StageDebugCounters {
public:
    AIStageCounters(const std::string& stage_name) : StageDebugCounters(stage_name) {
        extra_counters = new int[enum_size()]();
    }

    void write_additional_counter(std::ofstream& file) const override {
        if (file.is_open()) {
            file << "tensors: " << extra_counters[static_cast<int>(AIExtraCounters::TENSORS)];
        }
    }
protected:
    size_t enum_size() const override {
        return static_cast<size_t>(AIExtraCounters::AI_EXTRA_COUNTERS_SIZE);
    }
};

enum class AggregatorExtraCounters
{
    SUB_FRAMES = 0,
    AGGREGATOR_EXTRA_COUNTERS_SIZE  /* should be last */
};
class AggregatorCounters : public StageDebugCounters {
public:
    AggregatorCounters(const std::string& stage_name) : StageDebugCounters(stage_name) {
        extra_counters = new int[enum_size()]();
    }

    // Implementation of the additional counter logic specific to AI stage
    void write_additional_counter(std::ofstream& file) const override {
        if (file.is_open()) {
            file << "sub frames: " << extra_counters[static_cast<int>(AggregatorExtraCounters::SUB_FRAMES)];
        }
    }
protected:
    size_t enum_size() const override {
        return static_cast<size_t>(AggregatorExtraCounters::AGGREGATOR_EXTRA_COUNTERS_SIZE);
    }
};