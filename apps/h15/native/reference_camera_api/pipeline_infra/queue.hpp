#pragma once

// General includes
#include <atomic>
#include <optional>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <string>
#include <memory>
#include <iostream>

// Infra includes
#include "buffer.hpp"

// Forward declaration of internal QueueTracing class
class QueueTracing;

class Queue
{
  private:
    std::queue<BufferPtr> m_queue;
    size_t m_max_buffers;
    bool m_leaky;
    bool m_print_level;
    std::string m_name;
    std::atomic<bool> m_flushing;
    std::unique_ptr<std::condition_variable> m_condvar;
    std::shared_ptr<std::mutex> m_mutex;
    uint64_t m_drop_count = 0, m_push_count = 0;
    std::unique_ptr<QueueTracing> m_tracing;

  public:
    Queue(std::string name, size_t max_buffers, bool leaky = false, bool print_level = false);
    ~Queue();

    std::string name();
    int size();
    void push(BufferPtr buffer);
    BufferPtr pop();
    uint64_t check_timestamp(std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    void flush();
};

using QueuePtr = std::shared_ptr<Queue>;
