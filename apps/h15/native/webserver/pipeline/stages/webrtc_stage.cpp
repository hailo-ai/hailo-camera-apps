#include "webrtc_stage.hpp"

WebrtcStage::WebrtcStage(std::string name, std::shared_ptr<WebRtcResource> webrtc_resource, size_t queue_size, bool leaky, bool print_fps)
    : ConnectedStage(name, queue_size, leaky, print_fps),
      m_rtp_converter(nullptr),
      m_webrtc_resource(webrtc_resource),
      m_running(false) {
}


AppStatus WebrtcStage::create(EncodingType type) {
    if (m_rtp_converter == nullptr) {
        auto rtp_converter_expected = ConvertRtpModule::create(m_stage_name, type);
        if (!rtp_converter_expected.has_value()) {
            std::cerr << "Failed to create rtp converter" << std::endl;
            return AppStatus::CONFIGURATION_ERROR;
        }
        m_rtp_converter = rtp_converter_expected.value();
        m_type = type;
    }
    return AppStatus::SUCCESS;
}

AppStatus WebrtcStage::init() {
    if (m_rtp_converter == nullptr) {
        std::cerr << "rtp converter " << m_stage_name << " not configured. Call configure()" << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("rtp converter {} not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }
    m_rtp_converter->start();

    m_running.store(true);
    m_send_thread = std::thread(&WebrtcStage::callback_worker, this);

    return AppStatus::SUCCESS;
}

AppStatus WebrtcStage::deinit() {
    m_running.store(false);
    if (m_send_thread.joinable()) {
        m_send_thread.join();
    }
    if (m_rtp_converter != nullptr) {
        m_rtp_converter->stop();
    }
    return AppStatus::SUCCESS;
}

AppStatus WebrtcStage::configure(EncodingType type) {
    deinit();
    m_rtp_converter = nullptr;
    return create(type);
}

AppStatus WebrtcStage::process(BufferPtr data) {
    if (m_rtp_converter == nullptr) {
        std::cerr << "rtp converter " << m_stage_name << " not configured. Call configure()" << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("rtp converter {} not configured. Call configure()", m_stage_name);
        return AppStatus::UNINITIALIZED;
    }

    auto metadata = data->get_metadata_of_type(MetadataType::SIZE);
    if (metadata.empty()) {
        std::cerr << "rtp converter " << m_stage_name << " got buffer of unknown size, add SizeMeta" << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("rtp converter {} got buffer of unknown size, add SizeMeta", m_stage_name);
        return AppStatus::PIPELINE_ERROR;
    }

    auto size_metadata = std::dynamic_pointer_cast<SizeMetadata>(metadata[0]);
    size_t size = size_metadata->get_size();

    m_rtp_converter->add_buffer(data->get_buffer(), size);

    return AppStatus::SUCCESS;
}

void WebrtcStage::callback_worker() {
    while (m_running.load()) {
        GstSample* sample = m_rtp_converter->get_frame();
        if (sample != nullptr) {
            m_webrtc_resource->send_rtp_packet(sample);
            gst_sample_unref(sample);
        }
    }
}
