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

class MuxerStage : public ConnectedStage
{
  protected:
    std::string m_main_inlet_name;
    size_t m_main_queue_size;
    std::string m_sub_inlet_name;
    size_t m_sub_queue_size;

  public:
    MuxerStage(std::string name, std::string main_inlet_name, size_t main_queue_size, bool main_queue_leaky,
               std::string sub_inlet_name, size_t sub_queue_size, bool sub_queue_leaky, bool print_fps = false);

    void add_queue(std::string name) override;
    void loop() override;
    AppStatus init() override;
    AppStatus deinit() override;
};

class MuxerStageBuild : public MuxerStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_main_inlet_name;
        size_t m_main_queue_size = 10;
        bool m_main_queue_leaky = false;
        std::optional<std::string> m_sub_inlet_name;
        size_t m_sub_queue_size = 10;
        bool m_sub_queue_leaky = false;
        bool m_print_fps = false;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_main_inlet_name(std::string name);
        Builder &set_main_queue_size(size_t size);
        Builder &set_main_leaky(bool leaky);
        Builder &set_sub_inlet_name(std::string name);
        Builder &set_sub_queue_size(size_t size);
        Builder &set_sub_leaky(bool leaky);
        Builder &set_printfps_opt(bool print);
        std::shared_ptr<MuxerStage> buildptr() const;
    };

    static Builder create();
};
