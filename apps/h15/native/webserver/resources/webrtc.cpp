#include "webrtc.hpp"
using namespace webserver::resources;

WebRtcResource::WebRtcResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<ConfigResourceBase> configs) : Resource(event_bus){
    WEBSERVER_LOG_INFO("Initializing WebRtcResource");
    m_stream_codec = configs->get_encoder_default_config()["hailo_encoder"]["config"]["output_stream"]["codec"];
    subscribe_callback(EventType::STREAM_CONFIG, [this](ResourceStateChangeNotification notification){
        WEBSERVER_LOG_INFO("Received STREAM_CONFIG event");
        auto state = std::static_pointer_cast<StreamConfigResourceState>(notification.resource_state);
        if (state->resolutions[0].framerate_changed || state->dewarp_state_changed || state->flip_state_changed)
        {
            return;
        }
        close_all_connections();
    });
    subscribe_callback(EventType::CODEC_CHANGE, [this](ResourceStateChangeNotification notification){
        WEBSERVER_LOG_INFO("Received CODEC_CHANGE event");
        auto state = std::static_pointer_cast<EncoderResource::EncoderResourceState>(notification.resource_state);
        close_all_connections();
        m_stream_codec = state->control.codec_to_str.at(state->control.codec);
    });
}

std::string gathering_state_to_string(rtc::PeerConnection::GatheringState state) {
    switch (state) {
        case rtc::PeerConnection::GatheringState::New: return "NEW";
        case rtc::PeerConnection::GatheringState::InProgress: return "INPROGRESS";
        case rtc::PeerConnection::GatheringState::Complete: return "COMPLETE";
        default:
            std::runtime_error("Unknown gathering state");
            return "Unknown";
    }
}

std::string rtc_state_to_string(rtc::PeerConnection::State state) {
    switch (state) {
        case rtc::PeerConnection::State::New: return "NEW";
        case rtc::PeerConnection::State::Connecting: return "CONNECTING";
        case rtc::PeerConnection::State::Connected: return "CONNECTED";
        case rtc::PeerConnection::State::Disconnected: return "DISCONNECTED";
        case rtc::PeerConnection::State::Failed: return "FAILED";
        case rtc::PeerConnection::State::Closed: return "CLOSED";
        default:
            std::runtime_error("Unknown rtc state");
            return "Unknown";
    }
}

void WebRtcResource::reset_config(){
     close_all_connections();
}

void WebRtcResource::remove_inactive_sessions() {
    WEBSERVER_LOG_INFO("Removing inactive sessions");
    std::unique_lock lock(m_session_mutex);
    m_sessions.erase(
        std::remove_if(m_sessions.begin(), m_sessions.end(),
                       [](const std::shared_ptr<WebrtcSession> &session) {
                           return session->state == rtc::PeerConnection::State::Closed ||
                                  session->state == rtc::PeerConnection::State::Failed ||
                                  session->state == rtc::PeerConnection::State::Disconnected;
                       }),
        m_sessions.end());
}

void WebRtcResource::close_all_connections() {
    WEBSERVER_LOG_INFO("Closing all WebRTC connections...");

    for (auto& session : m_sessions) {
        if (session->peer_connection->state() != rtc::PeerConnection::State::Closed) {
            session->peer_connection->close();
        }
    }
    WEBSERVER_LOG_INFO("All WebRTC sessions closed.");
}

std::shared_ptr<WebRtcResource::WebrtcSession> WebRtcResource::create_media_sender() {
    WEBSERVER_LOG_INFO("Creating media sender");
    auto session = std::make_shared<WebrtcSession>();
    session->ssrc = 42;
    session->codec = m_stream_codec;
    // Ensure no external ICE servers are provided
    rtc::Configuration config;
    config.iceServers.clear();
    config.bindAddress = "10.0.0.1";
    session->peer_connection = std::make_shared<rtc::PeerConnection>(config);
    session->peer_connection->onStateChange(
        [this, session](rtc::PeerConnection::State state) {
            WEBSERVER_LOG_INFO("WebRtc State: {}", rtc_state_to_string(state));
            session->state = state;
            if (state == rtc::PeerConnection::State::Closed || state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Disconnected) {
                remove_inactive_sessions();
            }
        });

    session->peer_connection->onGatheringStateChange([this, session](rtc::PeerConnection::GatheringState state) {
        WEBSERVER_LOG_INFO("WebRtc Gathering State: {}", gathering_state_to_string(state));
        session->gathering_state = state;
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = session->peer_connection->localDescription();
            nlohmann::json message = {
                            {"type", description->typeString()},
                            {"sdp", std::string(description.value())}};
            session->ICE_offer = message;
            WEBSERVER_LOG_DEBUG("Generated ICE offer: {}", message.dump());
        }
    });
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    if (session->codec == "CODEC_TYPE_H264")
        media.addH264Codec(this->codec_payload_type_map.at(session->codec));
    else if (session->codec == "CODEC_TYPE_HEVC")
        media.addH265Codec(this->codec_payload_type_map.at(session->codec));
    else
        throw std::runtime_error("Unsupported codec");
    media.addSSRC(session->ssrc, "video-send");
    session->track = session->peer_connection->addTrack(media);
    session->peer_connection->setLocalDescription();

    return session;
}

void WebRtcResource::send_rtp_packet(GstSample *sample){
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        WEBSERVER_LOG_ERROR("Failed to map buffer");
        throw std::runtime_error("Failed to map buffer");
    }
    std::shared_lock lock(m_session_mutex);
    for (auto &session : m_sessions) {
        if (session->state != rtc::PeerConnection::State::Connected) {
            gst_buffer_unmap(buffer, &map);
            return;
        }
        if (!session->track->isOpen()) {
            WEBSERVER_LOG_WARNING("Track is not open yet. Cannot send RTP packet.");
            gst_buffer_unmap(buffer, &map);
            return;
        }
        auto len = gst_buffer_get_size(buffer);
        if (len < sizeof(rtc::RtpHeader) || !session->track->isOpen()) {
            gst_buffer_unmap(buffer, &map);
            WEBSERVER_LOG_ERROR("Invalid buffer size or track not open");
            throw std::runtime_error("Invalid buffer size or track not open");
        }
        auto rtp = reinterpret_cast<rtc::RtpHeader *>(map.data);
        rtp->setSsrc(session->ssrc);
        session->track->send(reinterpret_cast<const std::byte *>(map.data), len);
    }
    gst_buffer_unmap(buffer, &map);
}

void WebRtcResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    WEBSERVER_LOG_INFO("Registering HTTP endpoints");
    srv->Get("/Offer_RTC", std::function<nlohmann::json()>([this]()
                                                              {
                                                                WEBSERVER_LOG_INFO("Creating new WebRTC connection");
                                                                try {
                                                                    std::shared_ptr<WebRtcResource::WebrtcSession> session = this->create_media_sender();
                                                                    std::unique_lock lock(m_session_mutex);
                                                                    if (!m_sessions.empty() && (m_sessions.back()->peer_connection->state() == rtc::PeerConnection::State::Connecting || m_sessions.back()->peer_connection->state() == rtc::PeerConnection::State::New)) {
                                                                        //allow only one session to be in the state of connecting at a time
                                                                        m_sessions.pop_back();
                                                                    }

                                                                    while (session->gathering_state != rtc::PeerConnection::GatheringState::Complete) {
                                                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                                                    }
                                                                    
                                                                    nlohmann::json ret = {
                                                                        {"rtc_status", gathering_state_to_string(session->gathering_state)},
                                                                        {"rtc_offer", session->ICE_offer}};
                                                                    
                                                                    m_sessions.push_back(session);
                                                                    return ret;
                                                                } catch (const std::exception& e) {
                                                                    WEBSERVER_LOG_ERROR("Failed to create WebRTC offer: {}", e.what());
                                                                    return nlohmann::json{{"error", "Failed to create WebRTC offer"}};
                                                                }
                                                              }));

    srv->Post("/Responce_RTC", std::function<void(const nlohmann::json &)>([this](const nlohmann::json &j_body)
                    {
                        WEBSERVER_LOG_INFO("Processing WebRTC response");
                        try {
                            rtc::Description answer(j_body["sdp"].get<std::string>(), j_body["type"].get<std::string>());
                            std::shared_lock lock(m_session_mutex);
                            if (!m_sessions.empty()) {
                                m_sessions.back()->peer_connection->setRemoteDescription(answer);
                                WEBSERVER_LOG_INFO("Remote description set successfully");
                            } else {
                                WEBSERVER_LOG_WARNING("No active session found to set remote description");
                            }
                        } catch (const std::exception& e) {
                            WEBSERVER_LOG_ERROR("Error processing WebRTC response: {}", e.what());
                            throw;
                        }
                        WEBSERVER_LOG_INFO("WebRTC response processed successfully");
                    }));
}