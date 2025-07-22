#include "stage.hpp"
class ValveStage : public ConnectedStage
{
  private:
    std::atomic<bool> m_valve;

  public:
    ValveStage(std::string name, size_t queue_size, bool leaky = false, bool print_fps = false)
        : ConnectedStage(name, queue_size, leaky, print_fps), m_valve(true)
    {
    }

    AppStatus process(BufferPtr data) override
    {
        if (m_valve)
        {
            data->add_time_stamp(m_stage_name);
            set_duration(data);
            send_to_subscribers(data);
        }
        return AppStatus::SUCCESS;
    }
    void set_valve(bool valve)
    {
        m_valve = valve;
    }
};


class ValveStageBuild : public ValveStage
{
public:
    class Builder {
    
    private:
        std::optional<std::string>  m_stage_name;
        size_t                      m_queue_size=0;
        bool                        m_leaky=false;
        bool                        m_print_fps=false;        
    
    public:
        Builder& set_stage_name(std::string name) { m_stage_name=name; return *this;}
        Builder& set_queue_size(size_t size) { m_queue_size=size; return *this;}
        Builder& set_leaky_opt(bool activate) { m_leaky=activate; return *this;}
        Builder& set_printfps_opt(bool activate) { m_print_fps=activate; return *this;}

        std::shared_ptr<ValveStage> buildptr() const { 
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
            THROW_IF_MISSING((m_queue_size != 0), "set_queue_size");

            return std::make_shared<ValveStage>(m_stage_name.value(), m_queue_size, m_leaky, m_print_fps);}
    };

    static Builder create() { return Builder(); }

};
