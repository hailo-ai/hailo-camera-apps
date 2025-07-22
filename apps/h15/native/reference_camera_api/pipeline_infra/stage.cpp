#include "stage.hpp"

Stage::Stage(std::string name, bool print_fps) : m_stage_name(name), m_print_fps(print_fps)
{
    m_tracing = std::make_unique<StageTracing>(name);
}

std::string Stage::get_name()
{
    return m_stage_name;
}

AppStatus Stage::start()
{
    m_end_of_stream = false;
    m_thread = std::thread(&Stage::loop, this);
#if defined(__linux__)
    // Set thread name to stage name
    pthread_setname_np(m_thread.native_handle(), m_stage_name.substr(0, 15).c_str());
#endif

    return AppStatus::SUCCESS;
}

AppStatus Stage::stop()
{
    set_end_of_stream(true);
    m_thread.join();
    return AppStatus::SUCCESS;
}

AppStatus Stage::init()
{
    return AppStatus::SUCCESS;
}

AppStatus Stage::deinit()
{
    return AppStatus::SUCCESS;
}

void Stage::add_queue(std::string name)
{
}

void Stage::push(BufferPtr buffer, std::string caller_name)
{
}

void Stage::loop()
{
}

AppStatus Stage::process(BufferPtr buffer)
{
    return AppStatus::SUCCESS;
}

void Stage::set_end_of_stream(bool end_of_stream)
{
    m_end_of_stream = end_of_stream;
}

void Stage::set_print_fps(bool print_fps)
{
    m_print_fps = print_fps;
}

std::chrono::duration<double, std::micro> Stage::get_duration()
{
    return m_duration;
}

void Stage::set_duration(BufferPtr buff)
{
    if (buff->get_num_stages() >= 2)
    {
        std::chrono::steady_clock::time_point ts_start = buff->get_time_stamp(buff->get_num_stages() - 2);
        std::chrono::steady_clock::time_point ts_end = buff->get_time_stamp(buff->get_num_stages() - 1);
        m_duration = std::chrono::duration_cast<std::chrono::microseconds>(ts_end - ts_start);
    }
}

void Stage::trace_fps()
{
    m_tracing->increment_counter();
}

void Stage::print_fps()
{
    // TODO: Delete this function and the entire print_fps mechanism
}

// ConnectedStage Implementation
ConnectedStage::ConnectedStage(std::string name, size_t queue_size, bool leaky, bool print_fps,
                               bool trace_processing_operations)
    : Stage(name, print_fps), m_queue_size(queue_size), m_leaky(leaky),
      m_trace_processing_operations(trace_processing_operations)
{
}

void ConnectedStage::add_queue(std::string name)
{
    m_queues.push_back(std::make_shared<Queue>(name, m_queue_size, m_leaky));
}

void ConnectedStage::add_subscriber(ConnectedStagePtr subscriber)
{
    m_subscribers.push_back(subscriber);
    subscriber->add_queue(m_stage_name);
}

void ConnectedStage::push(BufferPtr data, std::string caller_name)
{
    for (auto &queue : m_queues)
    {
        if (queue->name() == caller_name)
        {
            queue->push(data);
            break;
        }
    }
}

void ConnectedStage::set_end_of_stream(bool end_of_stream)
{
    m_end_of_stream = end_of_stream;
    if (end_of_stream)
    {
        for (auto &queue : m_queues)
        {
            queue->flush();
        }
    }
}

void ConnectedStage::send_to_subscribers(BufferPtr data)
{
    for (auto &subscriber : m_subscribers)
    {
        subscriber->push(data, m_stage_name);
    }
}

void ConnectedStage::send_to_specific_subsciber(std::string stage_name, BufferPtr data)
{
    for (auto &subscriber : m_subscribers)
    {
        if (stage_name == subscriber->get_name())
        {
            subscriber->push(data, m_stage_name);
        }
    }
}

void ConnectedStage::loop()
{
    init();

    while (!m_end_of_stream)
    {
        BufferPtr data = m_queues[0]->pop(); // The first connected queue is always considered "main stream"
        if (data == nullptr && m_end_of_stream)
        {
            break;
        }

        if (m_trace_processing_operations)
        {
            m_tracing->trace_processing_start();
        }

        process(data);

        if (m_trace_processing_operations)
        {
            m_tracing->trace_processing_end();
        }

        trace_fps();
    }

    deinit();
}

// CallbackStage Implementation
CallbackStage::CallbackStage(std::string name, size_t queue_size, bool leaky, std::function<void(BufferPtr)> callback,
                             bool print_fps)
    : ConnectedStage(name, queue_size, leaky, false), m_callback(callback)
{
}

AppStatus CallbackStage::process(BufferPtr data)
{
    if (m_callback)
        m_callback(data);

    data->add_time_stamp(m_stage_name);
    set_duration(data);

    send_to_subscribers(data);

    return AppStatus::SUCCESS;
}

void CallbackStage::set_callback(std::function<void(BufferPtr)> callback)
{
    m_callback = callback;
}

// CallbackStageBuild::Builder Implementation
CallbackStageBuild::Builder &CallbackStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

CallbackStageBuild::Builder &CallbackStageBuild::Builder::set_queue_size_opt(size_t size)
{
    m_queue_size = size;
    return *this;
}

CallbackStageBuild::Builder &CallbackStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

CallbackStageBuild::Builder &CallbackStageBuild::Builder::set_printfps_opt(bool activate)
{
    m_print_fps = activate;
    return *this;
}

std::shared_ptr<CallbackStage> CallbackStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<CallbackStage>(m_stage_name.value(), m_queue_size, m_leaky, nullptr, m_print_fps);
}

CallbackStageBuild::Builder CallbackStageBuild::create()
{
    return Builder();
}

// TeeStage Implementation
TeeStage::TeeStage(std::string name, size_t queue_size, bool leaky, bool print_fps)
    : ConnectedStage(name, queue_size, leaky, print_fps)
{
}

AppStatus TeeStage::process(BufferPtr data)
{
    for (auto &subscriber : m_subscribers)
    {
        // The copy constractor performs a shallow copy
        BufferPtr new_buffer = std::make_shared<Buffer>(*data);
        subscriber->push(new_buffer, m_stage_name);
    }

    return AppStatus::SUCCESS;
}

// TeeStageBuild::Builder Implementation
TeeStageBuild::Builder &TeeStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

TeeStageBuild::Builder &TeeStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}

TeeStageBuild::Builder &TeeStageBuild::Builder::set_leaky_opt(bool activate)
{
    m_leaky = activate;
    return *this;
}

TeeStageBuild::Builder &TeeStageBuild::Builder::set_printfps_opt(bool activate)
{
    m_print_fps = activate;
    return *this;
}

std::shared_ptr<TeeStage> TeeStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

    return std::make_shared<TeeStage>(m_stage_name.value(), m_queue_size, m_leaky, m_print_fps);
}

TeeStageBuild::Builder TeeStageBuild::create()
{
    return Builder();
}
