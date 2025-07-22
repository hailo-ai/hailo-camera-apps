#include "stage.hpp"

class FreezeStage : public ConnectedStage
{
  private:
    BufferPtr m_saved_buffer;
    std::atomic<bool> m_freeze;

  public:
    FreezeStage(std::string name, size_t queue_size, bool leaky = false, bool print_fps = false)
        : ConnectedStage(name, queue_size, leaky, print_fps), m_saved_buffer(nullptr), m_freeze(false)
    {
    }

    AppStatus process(BufferPtr data) override
    {
        if (m_freeze && m_saved_buffer != nullptr)
        {
            data = m_saved_buffer;
        }
        else
        {
            m_saved_buffer = data;
        }

        data->add_time_stamp(m_stage_name);
        set_duration(data);

        send_to_subscribers(data);

        return AppStatus::SUCCESS;
    }
    bool is_freeze()
    {
        return m_freeze;
    }
    void set_freeze(bool freeze)
    {
        m_freeze = freeze;
    }
    void set_saved_buffer(BufferPtr buffer)
    {
        m_saved_buffer = buffer;
    }
    BufferPtr get_saved_buffer()
    {
        return m_saved_buffer;
    }
    void clear_saved_buffer()
    {
        m_saved_buffer = nullptr;
    }
};

class FreezeStageBuild : public FreezeStage
{
  public:
    class Builder
    {

      private:
        std::optional<std::string> m_stage_name;
        size_t m_queue_size = 0;
        bool m_leaky = false;
        bool m_print_fps = false;

      public:
        Builder &set_stage_name(std::string name)
        {
            m_stage_name = name;
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

        std::shared_ptr<FreezeStage> buildptr() const
        {
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
            THROW_IF_MISSING((m_queue_size != 0), "set_queue_size");

            return std::make_shared<FreezeStage>(m_stage_name.value(), m_queue_size, m_leaky, m_print_fps);
        }
    };

    static Builder create()
    {
        return Builder();
    }
};
