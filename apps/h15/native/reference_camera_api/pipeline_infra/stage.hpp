#pragma once

// General includes
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Tappas includes
#include "hailo_objects.hpp"
#include "hailo_common.hpp"
#include "reference_camera_logger.hpp"
#include "stage_tracing.hpp"
// Note: reference_camera_perfetto.hpp is not directly included anymore as it's used via the tracing classes

// Media library includes
#include "media_library/media_library_types.hpp"

// Infra includes
#include "buffer.hpp"
#include "queue.hpp"
#include "stage_debug.hpp"
#include "error_utils.hpp"

enum class AppStatus
{
    SUCCESS = 0,
    INVALID_ARGUMENT,
    CONFIGURATION_ERROR,
    BUFFER_ALLOCATION_ERROR,
    HAILORT_ERROR,
    DSP_OPERATION_ERROR,
    UNINITIALIZED,
    PIPELINE_ERROR,
    DMA_ERROR,
    MEDIA_LIBRARY_ERROR
};

/* StagePoolMode: once no available buffer in the stage pool
Leaky - drop the current frame
Blocking - wait till the next available buffer.
Fail on empty - return an error.
*/
enum class StagePoolMode
{
    FAIL_ON_EMPTY_POOL = 0,
    LEAKY,
    BLOCKING,
    STAGE_POOL_MODE_MAX
};

// Forward declaration
class StageTracing;

class Stage
{
  protected:
    std::atomic<bool> m_end_of_stream = false;
    std::string m_stage_name;
    std::thread m_thread;

    // FPS Related members
    bool m_print_fps = false;
    std::chrono::duration<double, std::micro> m_duration;

    // Tracing related
    std::unique_ptr<StageTracing> m_tracing;

  public:
    std::shared_ptr<StageDebugCounters> m_debug_counters;
    Stage(std::string name, bool print_fps);
    virtual ~Stage() = default;

    std::string get_name();
    virtual AppStatus start();
    virtual AppStatus stop();
    virtual AppStatus init();
    virtual AppStatus deinit();
    virtual void add_queue(std::string name);
    virtual void push(BufferPtr buffer, std::string caller_name);
    virtual void loop();
    virtual AppStatus process(BufferPtr buffer);
    virtual void set_end_of_stream(bool end_of_stream);
    void set_print_fps(bool print_fps);
    std::chrono::duration<double, std::micro> get_duration();
    void set_duration(BufferPtr buff);
    void trace_fps();
    void print_fps(); // TODO: Delete this function and the entire print_fps mechanism
};
using StagePtr = std::shared_ptr<Stage>;

class ConnectedStage;
using ConnectedStagePtr = std::shared_ptr<ConnectedStage>;
class ConnectedStage : public Stage
{
  protected:
    size_t m_queue_size;
    bool m_leaky;
    std::vector<QueuePtr> m_queues;
    std::vector<ConnectedStagePtr> m_subscribers;
    bool m_trace_processing_operations;

  public:
    ConnectedStage(std::string name, size_t queue_size, bool leaky = false, bool print_fps = false,
                   bool trace_processing_operations = true);
    void add_queue(std::string name) override;
    void add_subscriber(ConnectedStagePtr subscriber);
    void push(BufferPtr data, std::string caller_name) override;
    void set_end_of_stream(bool end_of_stream) override;
    void send_to_subscribers(BufferPtr data);
    void send_to_specific_subsciber(std::string stage_name, BufferPtr data);
    void loop() override;
};

class CallbackStage : public ConnectedStage
{
  protected:
    std::function<void(BufferPtr)> m_callback;

  public:
    CallbackStage(std::string name, size_t queue_size, bool leaky = false,
                  std::function<void(BufferPtr)> callback = NULL, bool print_fps = false);
    AppStatus process(BufferPtr data) override;
    void set_callback(std::function<void(BufferPtr)> callback);
};
using CallbackStagePtr = std::shared_ptr<CallbackStage>;

class CallbackStageBuild : public CallbackStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = 1;
        bool m_leaky = false;
        bool m_print_fps = false;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_queue_size_opt(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_printfps_opt(bool activate);
        std::shared_ptr<CallbackStage> buildptr() const;
    };

    static Builder create();
};

class TeeStage : public ConnectedStage
{
  public:
    TeeStage(std::string name, size_t queue_size, bool leaky = false, bool print_fps = false);
    AppStatus process(BufferPtr data) override;
};
using TeeStagePtr = std::shared_ptr<TeeStage>;

class TeeStageBuild : public TeeStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = 10;
        bool m_leaky = false;
        bool m_print_fps = false;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_queue_size(size_t size);
        Builder &set_leaky_opt(bool activate);
        Builder &set_printfps_opt(bool activate);
        std::shared_ptr<TeeStage> buildptr() const;
    };

    static Builder create();
};
