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

class DemuxerStage : public ConnectedStage
{
  protected:
    std::string m_main_outlet_name;
    std::string m_sub_outlet_name;
    bool m_copy_roi_metadata;

  public:
    DemuxerStage(std::string name, std::string main_outlet_name, std::string sub_outlet_name, size_t queue_size = 10,
                 bool leaky = false, bool print_fps = false, bool copy_roi_metadata = false);

    AppStatus process(BufferPtr buffer) override;
    AppStatus init() override;
    AppStatus deinit() override;

  private:
    void copy_hailo_roi_metadata(BufferPtr main_buffer, BufferPtr sub_buffer);
};

class DemuxerStageBuild : public DemuxerStage
{
  public:
    class Builder
    {
      private:
        std::optional<std::string> m_stage_name;
        std::optional<std::string> m_main_outlet_name;
        std::optional<std::string> m_sub_outlet_name;
        size_t m_queue_size = 10;
        bool m_leaky = false;
        bool m_print_fps = false;
        bool m_copy_roi_metadata = false;

      public:
        Builder &set_stage_name(std::string name);
        Builder &set_main_outlet_name(std::string name);
        Builder &set_sub_outlet_name(std::string name);
        Builder &set_queue_size(size_t size);
        Builder &set_leaky_opt(bool leaky);
        Builder &set_printfps_opt(bool print);
        Builder &set_copy_roi_metadata_opt(bool copy_roi);
        std::shared_ptr<DemuxerStage> buildptr() const;
    };

    static Builder create();
};
