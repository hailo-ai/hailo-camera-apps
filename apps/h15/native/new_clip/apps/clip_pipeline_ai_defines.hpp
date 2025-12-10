#pragma once

#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include "hailo_objects.hpp"

namespace app {

//==============================================================================
// File System & Storage Configuration
//==============================================================================

/**
 * @brief Defines paths to configuration files, AI models, and libraries.
 */
namespace paths {
    inline const std::string medialib_config = "/etc/imaging/cfg/medialib_configs/clip_example_medialib_config.json";

    // YOLOv8 model paths and names
    inline const std::string yolo_hef       = "/home/root/apps/ai_example_app/resources/yolov8n_personface_nv12.hef";
    inline const std::string yolo_post_so   = "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so";
    inline const std::string yolo_func_name = "yolov8n_personface";
    inline const std::string yolo_config    = "/home/root/apps/new_clip/resources/configs/yolov8n_personface.json";
} // namespace paths

/**
 * @brief Defines medialib profile.
 */
namespace medialib_profile {
    
    inline const std::string play_from_file = "PlayFromFile";
    inline const std::string default_profile_node = "default_profile";

} // namespace medialib_profile

/**
 * @brief Defines constants for file storage, such as database names and file prefixes.
 */
namespace storage {
    inline const std::string clip_database_file   = "clip_database.db";
    inline const std::string video_segment_prefix = "video_segment";
    inline const std::string thumbnail_prefix     = "thumbnail_vga";
} // namespace storage


//==============================================================================
// Network & Stream Configuration
//==============================================================================

/**
 * @brief Network-related constants like IP addresses and ports.
 */
namespace net {
    inline const std::string host_ip     = "10.0.0.2";
    inline constexpr int      udp_port_4k = 5000;
} // namespace net

/**
 * @brief Unique identifiers for different video/data streams in the pipeline.
 */
namespace stream_id {
    inline const std::string highres = "HighRes";
    inline const std::string stream_vga = "VGA";
    inline const std::string stream_ai  = "AI";
} // namespace stream_id

//==============================================================================
// AI / Detection Configuration
//==============================================================================

/**
 * @brief Defines the classes/labels for object detection.
 */
namespace classes {
    enum class detection_id : int {
        person = 1
    };
    inline const std::string clip_crop_target_label = "person";
} // namespace classes

/**
 * @brief A simple structure to hold width and height dimensions.
 */
struct size {
    int width;
    int height;
};

/**
 * @brief Tiling configuration for the AI inference input.
 */
namespace tiling {
    inline constexpr size input{1920, 1080};
    inline constexpr size output{640, 384};

    // Normalized tiles [x, y, w, h] for inference.
    // Assumes HailoBBox is defined elsewhere (e.g., as a struct or type alias).
    using tile = HailoBBox; 
    inline std::vector<tile> tiles = {
        tile{0.0, 0.0, 0.6, 0.6},
        tile{0.4, 0.0, 0.6, 0.6},
        tile{0.0, 0.4, 0.6, 0.6},
        tile{0.4, 0.4, 0.6, 0.6},
        tile{0.0, 0.0, 1.0, 1.0} // Full frame
    };
} // namespace tiling


//==============================================================================
// Pipeline Stage Identifiers
//==============================================================================

/**
 * @brief Unique string identifiers for each stage in the processing pipeline.
 */
namespace stage {
    // VGA stream stages
    inline const std::string vga_tee           = "vga_tee_stage";
    inline const std::string vga_aggregator    = "vga_agg_stage";
    inline const std::string vga_overlay       = "vga_overlay_stage";
    inline const std::string thumbnail_storage = "thumb_storage_stage";
    inline const std::string thumbnail_cache   = "thumb_cache_stage";

    // Main 4K stream stages
    inline const std::string main_4k_tee        = "main_4k_tee_stage";
    inline const std::string main_4k_aggregator = "main_4k_agg_stage";
    inline const std::string main_4k_overlay    = "main_4k_overlay_stage";
    inline const std::string main_mkv_storage   = "main_mkv_storage_stage";
    inline const std::string main_4k_udp        = "main_4k_udp_stage";
    inline const std::string main_4k_webrtc     = "main_4k_webrtc_stage";

    // Detection and tracking stages
    inline const std::string detection_tiling  = "detection_tiling_stage";
    inline const std::string tiling_aggregator = "tiling_agg_stage";
    inline const std::string detection_infer   = "detection_infer_stage";
    inline const std::string detection_post    = "detection_post_stage";
    inline const std::string detection_tee_out = "det_tee_out_stage";
    inline const std::string tracker_light     = "det_tracker_light_stage";

    // Clip generation stages
    inline const std::string tracker_traffic_ctrl = "tracker_traffic_control_stage";
    inline const std::string clip_crop            = "clip_crop_stage";
    inline const std::string clip_quality_check   = "clip_quality_check_stage";
    inline const std::string clip_tee             = "clip_tee_stage";
    inline const std::string faiss_storage        = "faiss_storage_stage";
} // namespace stage

} // namespace app
