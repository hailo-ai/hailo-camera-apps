#pragma once

#include <memory>
#include <string>
#include <vector>
#include <tl/expected.hpp>

#include "utils/clip_app_config_parser.hpp"

// Forward declarations
namespace httplib {
    class Server;
    class Response;
}

// Forward declarations for local types
struct ClipAppConfig;
class ClipVideoPipeline;
class WebRTCStreamerExt;
class ClipQueryServiceExt;
class VideoStreamingServiceExt;
class StorageMonitorServiceExt;

class IntegratedWebServer
{
  private:
    struct WebRtcStreamers
    {
        enum class StreamType
        {
            MAIN_LIVE,
            QUERY_PLAYBACK,
        } type;

        std::string session_id;
        std::shared_ptr<WebRTCStreamerExt> streamer_ext;

        WebRtcStreamers(StreamType t, const std::string &id, std::shared_ptr<WebRTCStreamerExt> ext);
    };

    std::unique_ptr<httplib::Server> server;
    std::unique_ptr<ClipAppConfig> web_server_config;
    std::shared_ptr<ClipQueryServiceExt> clip_query_service;
    std::shared_ptr<VideoStreamingServiceExt> clip_query_player_streaming_service;
    std::shared_ptr<StorageMonitorServiceExt> storage_monitor_service;

    std::shared_ptr<ClipVideoPipeline> m_app;
    std::vector<WebRtcStreamers> m_webrtc_streamers;

    struct EmbeddingInfo
    {
        struct EmbeddingData
        {
            std::string prompt;
            std::vector<float> embedding;
        };

        std::string network_id;
        EmbeddingData positive_embedding;
        std::vector<EmbeddingData> negative_embeddings; // For multi-prompt support
        float score_threshold;
        int max_query;
        int remove_duplicate_within_sec;
        bool text_decode_on_device = true;
    };

    // Image data structure for thumbnail gallery
    struct ImageData
    {
        std::string jpeg_data;   // Base64 encoded JPEG data
        std::string description; // Text description
        int64_t timestamp;       // Epoch time in milliseconds
        float score;             // score of this query result
    };

    // Error types
    enum class ImageError
    {
        FILE_NOT_FOUND,
        INVALID_FORMAT,
        ENCODING_ERROR,
        NETWORK_ERROR,
        JSON_PARSE_ERROR
    };

    // Thumbnail gallery functionality
    std::vector<ImageData> m_images;

    // Private constructor - Should use Create to instantiate IntegratedWebServer
    explicit IntegratedWebServer(ClipAppConfig config);

    // Base64 encode function
    std::string base64_encode(const std::string &data);

    // Load JPEG file and convert to base64
    tl::expected<std::string, ImageError> loadJpegFile(const std::string &filepath);

    // Add image to gallery
    tl::expected<void, ImageError> addImage(const std::string &jpeg_path, const std::string &description,
                                            int64_t timestamp, float score);

    void clearAllImages();

    // Helper function to search the type of WebRTC streamer
    std::shared_ptr<WebRtcStreamers> findWebRtcStreamer(WebRtcStreamers::StreamType type);

    // Helper function to search the session ID of WebRTC streamer
    std::shared_ptr<WebRtcStreamers> findWebRtcStreamerBySessionId(const std::string &session_id);

    void validateModelFiles();
    void setupCORS();
    void setupRoutes();
    void serveIntegratedHTML(httplib::Response &res);
    bool processEmbedding(const EmbeddingInfo &embedding_info);
    std::string createPlayFromFileMediaConfigTempFile();

  public:
    // Delete copy operations
    IntegratedWebServer(const IntegratedWebServer &) = delete;
    IntegratedWebServer &operator=(const IntegratedWebServer &) = delete;

    // Move operations
    IntegratedWebServer(IntegratedWebServer &&) noexcept = default;
    IntegratedWebServer &operator=(IntegratedWebServer &&) noexcept = default;

    // Static factory method
    static tl::expected<std::shared_ptr<IntegratedWebServer>, std::string> create(const ClipAppConfig &config);

    void start(std::string host, int port = 80);
    void stop();
};
