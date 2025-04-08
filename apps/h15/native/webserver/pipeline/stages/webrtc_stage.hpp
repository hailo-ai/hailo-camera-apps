#pragma once

#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <iostream>
#include "media_library/encoder.hpp"
#include "hailo_common.hpp"
#include "stage.hpp"
#include "buffer.hpp"
#include "convert_rtp_module.hpp"
#include "resources/webrtc.hpp"

using webserver::resources::WebRtcResource;

class WebrtcStage : public ConnectedStage {
public:
    // Constructor and Destructor
    WebrtcStage(std::string name, std::shared_ptr<WebRtcResource> webrtc_resource, size_t queue_size = 1, bool leaky = false, bool print_fps = false);
    ~WebrtcStage() override = default;

    // Public member functions
    AppStatus create(EncodingType type);
    AppStatus init() override;
    AppStatus deinit() override;
    AppStatus configure(EncodingType type);
    AppStatus process(BufferPtr data);

private:
    // Worker function for processing frames
    void callback_worker();

    // Member variables
    EncodingType m_type;
    ConvertRtpModulePtr m_rtp_converter;
    std::shared_ptr<WebRtcResource> m_webrtc_resource;

    std::thread m_send_thread;
    std::atomic<bool> m_running;
};
