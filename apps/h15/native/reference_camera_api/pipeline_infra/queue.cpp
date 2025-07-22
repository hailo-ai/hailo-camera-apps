#include "queue.hpp"
#include "reference_camera_perfetto.hpp"

// Internal class for Perfetto tracing to maintain ABI compatibility
class QueueTracing
{
  private:
#ifdef HAVE_PERFETTO
    std::string m_counter_name;
    perfetto::CounterTrack m_counter_track;
#endif

  public:
    QueueTracing(const std::string &name)
#ifdef HAVE_PERFETTO
        : m_counter_name("queue_" + name), m_counter_track(perfetto::DynamicString(m_counter_name), "queue level")
#endif
    {
    }

    void track_queue_size(size_t size)
    {
        REFERENCE_CAMERA_TRACE_COUNTER(m_counter_track, size);
    }
};

Queue::Queue(std::string name, size_t max_buffers, bool leaky, bool print_level)
    : m_max_buffers(max_buffers), m_leaky(leaky), m_print_level(print_level), m_name(name), m_flushing(false)
{
    m_mutex = std::make_shared<std::mutex>();
    m_condvar = std::make_unique<std::condition_variable>();
    m_queue = std::queue<BufferPtr>();
    m_tracing = std::make_unique<QueueTracing>(name);
}

Queue::~Queue()
{
    m_flushing = true;
    m_condvar->notify_all();
    flush();
}

std::string Queue::name()
{
    return m_name;
}

int Queue::size()
{
    std::unique_lock<std::mutex> lock(*(m_mutex));
    return m_queue.size();
}

void Queue::push(BufferPtr buffer)
{
    std::unique_lock<std::mutex> lock(*(m_mutex));
    if (m_flushing)
    {
        return;
    }
    if (!m_leaky)
    {
        // if not leaky, then wait until there is space in the queue
        m_condvar->wait(lock, [this] { return m_queue.size() < m_max_buffers; });
    }
    else
    {
        // if leaky, pop the front for a full queue
        if (m_queue.size() >= m_max_buffers)
        {
            m_queue.pop();
            m_drop_count++;
        }
    }
    m_queue.push(buffer);
    m_tracing->track_queue_size(m_queue.size());

    m_push_count++;
    if (m_print_level)
    {
        std::cout << "Queue: " << m_name << " level: " << m_queue.size() << std::endl;
    }
    REFERENCE_CAMERA_LOG_TRACE("Queue: {} level: {} leaky: {} push count: {} drop count: {}", m_name, m_queue.size(),
                               m_leaky, m_push_count, m_drop_count);
    m_condvar->notify_one();
}

BufferPtr Queue::pop()
{
    std::unique_lock<std::mutex> lock(*(m_mutex));
    // wait for there to be something in the queue to pull
    m_condvar->wait(lock, [this] { return !m_queue.empty() || m_flushing == true; });
    if (m_queue.empty())
    {
        // if we reached here, then the queue is empty and we are flushing
        return nullptr;
    }
    BufferPtr buffer = m_queue.front();
    m_queue.pop();
    m_tracing->track_queue_size(m_queue.size());

    m_condvar->notify_one();
    return buffer;
}

uint64_t Queue::check_timestamp(std::optional<std::chrono::milliseconds> timeout)
{
    std::unique_lock<std::mutex> lock(*(m_mutex));
    // wait for there to be something in the queue to check
    if (timeout.has_value())
    {
        if (m_condvar->wait_for(lock, timeout.value(), [this] { return !m_queue.empty() || m_flushing == true; }) ==
            false)
        {
            // if we reached here, then we timed out
            return 0;
        }
    }
    else
    {
        m_condvar->wait(lock, [this] { return !m_queue.empty() || m_flushing == true; });
    }
    if (m_queue.empty())
    {
        // if we reached here, then the queue is empty and we are flushing
        return 0;
    }
    return m_queue.front()->get_buffer()->isp_timestamp_ns;
}

void Queue::flush()
{
    std::unique_lock<std::mutex> lock(*(m_mutex));
    m_flushing = true;
    while (!m_queue.empty())
    {
        m_queue.pop();
    }
    m_condvar->notify_all();
}
