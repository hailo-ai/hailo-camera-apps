// general includes
#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <tl/expected.hpp>
#include <signal.h>
#include <cxxopts/cxxopts.hpp>

// medialibrary includes
#include "media_library/media_library.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/signal_utils.hpp"

// infra includes
#include "pipeline.hpp"
#include "ai_stage.hpp"
#include "dsp_stages.hpp"
#include "postprocess_stage.hpp"
#include "overlay_stage.hpp"
#include "udp_stage.hpp"
#include "encoder_stage.hpp"
#include "frontend_stage.hpp"
#include "lightweight_tracker_stage.hpp"
#include "persist_stage.hpp"
#include "aggregator_stage.hpp"
#include "reference_camera_logger.hpp"
#include "pipeline_builder.hpp"

// Frontend Params
#define FRONTEND_STAGE "frontend_stage"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/ai_example_medialib_config.json"
#define VISION_SINK "sink0" // The streamid from frontend to 4K stream that shows vision results
#define AI_SINK "sink2"     // The streamid from frontend to AI (FHD)
#define CALLBACK_STAGE "callback_stage"

// Output Params
#define HOST_IP "10.0.0.2"
#define TRACKER_STAGE "tracker"
#define OVERLAY_STAGE "overlay"

/*
    Stage 1 Params (Person/Face Detection)
*/
// Tilling Params
#define TILLING_STAGE "tilling"
#define TILLING_INPUT_WIDTH 1920
#define TILLING_INPUT_HEIGHT 1080
#define TILLING_OUTPUT_WIDTH 640
#define TILLING_OUTPUT_HEIGHT 640
std::vector<HailoBBox> TILES = {
    {0.0, 0.0, 0.6, 0.6}, {0.4, 0, 0.6, 0.6}, {0, 0.4, 0.6, 0.6}, {0.4, 0.4, 0.6, 0.6}, {0.0, 0.0, 1.0, 1.0}};
// Detection AI Params
#define YOLO_HEF_FILE "/home/root/apps/ai_example_app/resources/yolov8n_personface_nv12.hef"
#define DETECTION_AI_STAGE "yolo_detection"
// Detection Postprocess Params
#define POST_STAGE "yolo_post"
#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so"
#define YOLO_FUNC_NAME "yolov8n_personface"
#define YOLO_POST_CONF "/home/root/apps/detection/resources/configs/yolov5_personface.json"
// Stage 1 Aggregator Params
#define DETECTION_AGGREGATOR "detection_aggregator"
#define STAGE_1_AGGREGATOR "stage_1_aggregator"

/*
    Stage 2 Params (Face Landmarks)
*/
// Tee Params
#define TEE_STAGE "vision_tee"
// Bbox crop Parms
#define BBOX_CROP_STAGE "bbox_crops"
#define BBOX_CROP_LABEL "face"
#define BBOX_CROP_OUTPUT_WIDTH 120
#define BBOX_CROP_OUTPUT_HEIGHT 120
// Landmarks AI Params
#define LANDMARKS_HEF_FILE "/home/root/apps/ai_example_app/resources/tddfa_mobilenet_v1_nv12.hef"
#define LANDMARKS_AI_STAGE "face_landmarks"
// Landmarks Postprocess Params
#define LANDMARKS_POST_STAGE "landmarks_post"
#define LANDMARKS_POST_SO "/usr/lib/hailo-post-processes/libfacial_landmarks_post.so"
#define LANDMARKS_FUNC_NAME "facial_landmarks_nv12"
// Whitelist landmarks range
#define LANDMARKS_RANGE_MIN 36
#define LANDMARKS_RANGE_MAX 47
// Stage 2 Aggregator Params
#define LANDMARKS_AGGREGATOR "landmarks_aggregator"
#define STAGE_2_AGGREGATOR "stage_2_aggregator"

// Macro that turns coverts stream ids to port #s
#define PORT_FROM_ID(id) std::to_string(5000 + std::stoi(id.substr(4)) * 2)

enum class ArgumentType
{
    Help,
    PrintFPS,
    PrintLatency,
    Timeout,
    Config,
    Profile,
    SkipDrawing,
    FullLandmarks,
    HostIP,
    Error
};

void print_help(const cxxopts::Options &options)
{
    std::cout << options.help() << std::endl;
}

cxxopts::Options build_arg_parser()
{
    // clang-format off
    cxxopts::Options options("AI pipeline app");
    options.add_options()
    ("h,help", "Show this help")
    ("t,timeout", "Time to run", 
        cxxopts::value<int>()->default_value("60"))
    ("p,print-fps", "Print FPS", 
        cxxopts::value<bool>()->default_value("false"))
    ("l,print-latency", "Print Latency", 
        cxxopts::value<bool>()->default_value("false"))
    ("c,config-file-path", "Media library configuration path", 
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH))
    ("a,profile", "Profile name", 
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("s,skip-drawing", "Skip drawing", 
        cxxopts::value<bool>()->default_value("false"))
    ("f,full-landmarks", "Draw all landmarks (default draws only eyes for face landmarks)", 
        cxxopts::value<bool>()->default_value("false"))
    ("o,host-ip", "Host IP address for UDP output", 
        cxxopts::value<std::string>()->default_value(HOST_IP));
    // clang-format on

    return options;
}

std::vector<ArgumentType> handle_arguments(const cxxopts::ParseResult &result, const cxxopts::Options &options)
{
    std::vector<ArgumentType> arguments;

    if (result.count("help"))
    {
        print_help(options);
        arguments.push_back(ArgumentType::Help);
    }

    if (result.count("print-fps"))
    {
        arguments.push_back(ArgumentType::PrintFPS);
    }

    if (result.count("timeout"))
    {
        arguments.push_back(ArgumentType::Timeout);
    }

    if (result.count("print-latency"))
    {
        arguments.push_back(ArgumentType::PrintLatency);
    }

    if (result.count("config-file-path"))
    {
        arguments.push_back(ArgumentType::Config);
    }

    if (result.count("profile"))
    {
        arguments.push_back(ArgumentType::Profile);
    }

    if (result.count("skip-drawing"))
    {
        arguments.push_back(ArgumentType::SkipDrawing);
    }

    if (result.count("full-landmarks"))
    {
        arguments.push_back(ArgumentType::FullLandmarks);
    }

    if (result.count("host-ip"))
    {
        arguments.push_back(ArgumentType::HostIP);
    }

    // Handle unrecognized options
    for (const auto &unrecognized : result.unmatched())
    {
        std::cerr << "Error: Unrecognized option or argument: " << unrecognized << std::endl;
        return {ArgumentType::Error};
    }

    return arguments;
}
/**
 * @brief Holds the resources required for the application.
 *
 * This structure contains pointers to various components and modules
 * used by the application, including the frontend, encoders, UDP outputs,
 * and the pipeline. It also includes a flag to control whether FPS (frames per second)
 * information should be printed.
 */
struct AppResources
{
    std::shared_ptr<MediaLibrary> media_library;
    std::shared_ptr<FrontendStage> frontend;
    std::map<output_stream_id_t, std::shared_ptr<EncoderStage>> encoders;
    std::map<output_stream_id_t, std::shared_ptr<UdpStage>> udp_outputs;
    PipelinePtr pipeline;
    bool print_fps;
    bool print_latency;
    bool skip_drawing;
    bool full_landmarks;
    std::string medialib_config_path;
    std::string profile_name;
    std::string host_ip = HOST_IP;

    void clear()
    {
        frontend = nullptr;
        pipeline = nullptr;
        encoders.clear();
        udp_outputs.clear();
        print_fps = false;
        print_latency = false;
        skip_drawing = false;
        full_landmarks = false;
        medialib_config_path = "";
        media_library = nullptr;
        profile_name = NO_PROFILE_SELECTED;
        host_ip = HOST_IP;
    }

    ~AppResources()
    {
        clear();
    }
};

std::string read_string_from_file(const char *file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error(std::string("config path (") + file_path + ") is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)), std::istreambuf_iterator<char>());
    file_to_read.close();
    std::cout << "Read config from file: " << file_path << std::endl;
    return file_string;
}

/**
 * @brief Create and configure an encoder and its corresponding UDP output file.
 *
 * This function sets up an encoder and a UDP output module for a given stream ID.
 * It reads configuration files and initializes the encoder and UDP module accordingly.
 *
 * @param id The ID of the output stream.
 * @param app_resources Shared pointer to the application's resources.
 */
void create_encoder_and_udp(const std::string &id, std::shared_ptr<AppResources> app_resources)
{
    // Create and configure encoder
    std::string enc_name = "enc_" + id;
    std::cout << "Creating encoder " << enc_name << std::endl;
    std::shared_ptr<EncoderStage> encoder_stage = EncoderStageBuild::create().set_stage_name(enc_name).buildptr();

    app_resources->encoders[id] = encoder_stage;
    AppStatus enc_config_status = encoder_stage->configure(app_resources->media_library->m_encoders[id]);
    if (enc_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure encoder " << enc_name << std::endl;
        throw std::runtime_error("Failed to configure encoder");
    }

    // Create and conifgure udp
    std::string udp_name = "udp_" + id;
    std::cout << "Creating udp " << udp_name << std::endl;
    std::shared_ptr<UdpStage> udp_stage =
        UdpStageBuild::create().set_stage_name(udp_name).set_leaky_opt(false).set_printfps_opt(true).buildptr();
    app_resources->udp_outputs[id] = udp_stage;
    AppStatus udp_config_status = udp_stage->configure(app_resources->host_ip, PORT_FROM_ID(id), EncodingType::H264);
    if (udp_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure udp " << udp_name << std::endl;
        throw std::runtime_error("Failed to configure udp");
    }
}

/**
 * @brief Configure the frontend and encoders for the application.
 *
 * This function initializes the frontend and sets up encoders for each output stream
 * from the frontend. It reads configuration files to properly configure the components.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
void configure_frontend_and_encoders(std::shared_ptr<AppResources> app_resources)
{
    std::string medialib_config_string = read_string_from_file(app_resources->medialib_config_path.c_str());
    app_resources->media_library = std::make_shared<MediaLibrary>();
    if (app_resources->media_library->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return;
    }
    if (app_resources->profile_name != NO_PROFILE_SELECTED)
    {
        app_resources->media_library->set_profile(app_resources->profile_name);
    }
    // Create and configure frontend
    app_resources->frontend = FrontendStageBuild::create().set_stage_name(FRONTEND_STAGE).buildptr();
    AppStatus frontend_config_status = app_resources->frontend->configure(app_resources->media_library->m_frontend);
    if (frontend_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure frontend " << FRONTEND_STAGE << std::endl;
        throw std::runtime_error("Failed to configure frontend");
    }

    // Get frontend output streams
    auto streams = app_resources->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    // Create encoders and output files for each stream
    for (auto s : streams.value())
    {
        if (s.id == AI_SINK)
        {
            // AI pipeline does not get an encoder since it is merged into 4K
            continue;
        }
        create_encoder_and_udp(s.id, app_resources);
    }
}

/**
 * @brief Create and configure the application's processing pipeline.
 *
 * This function sets up the application's processing pipeline by creating various stages
 * and subscribing them to each other to form a complete pipeline. Each stage is initialized
 * with specific parameters and then added to the pipeline. The stages are also interconnected
 * by subscribing them to ensure data flows correctly between them.
 *
 * @param app_resources Shared pointer to the application's resources, which includes the pipeline object.
 */
void create_main_pipeline(std::shared_ptr<AppResources> app_resources)
{

    try
    {

        // Get the input resolution from frontend
        auto streams = app_resources->frontend->get_outputs_streams();

        // Retrieve the vision input resolution for sanity check and which we will be using for crop
        int bbox_crop_input_width = 0;
        int bbox_crop_input_height = 0;
        if (streams.has_value())
        {
            for (const auto &stream : streams.value())
            {
                if (stream.id == VISION_SINK)
                {
                    bbox_crop_input_width = stream.width;
                    bbox_crop_input_height = stream.height;
                    break;
                }
            }
        }

        if (bbox_crop_input_width == 0 || bbox_crop_input_height == 0)
        {
            std::cerr << "Failed to get input resolution from frontend" << std::endl;
            throw std::runtime_error("Failed to get input resolution from frontend");
        }

        /*
                 _____________________________________
                /                                     \
            +--------+    +------+    +------+    +------------+
            | tiling | -> | yolo | -> | post | -> | aggregator |
            +--------+    +------+    +------+    +------------+
        */

        std::shared_ptr<CallbackStage> callback_stage = CallbackStageBuild::create()
                                                            .set_stage_name(CALLBACK_STAGE)
                                                            .set_queue_size_opt(1)
                                                            .set_leaky_opt(false)
                                                            .set_printfps_opt(app_resources->print_fps)
                                                            .buildptr();
        callback_stage->set_callback([app_resources](BufferPtr data) {
            static int counter = 0;
            static const int threshold = 2; // Toggle every 2 calls
            counter = (counter + 1) % threshold;

            if (counter < threshold / 2)
            {
                CroppingMetadataPtr cropping_meta = std::make_shared<CroppingMetadata>(1);
                data->add_metadata(cropping_meta);
            }
            else
            {
                CroppingMetadataPtr cropping_meta = std::make_shared<CroppingMetadata>(0);
                data->add_metadata(cropping_meta);
            }
        });

        std::shared_ptr<TillingCropStage> tilling_stage = TillingCropStageBuild::create()
                                                              .set_stage_name(TILLING_STAGE)
                                                              .set_output_pool_size(50)
                                                              .set_input_width(TILLING_INPUT_WIDTH)
                                                              .set_input_height(TILLING_INPUT_HEIGHT)
                                                              .set_output_width(TILLING_OUTPUT_WIDTH)
                                                              .set_output_height(TILLING_OUTPUT_HEIGHT)
                                                              .set_main_sub_name(DETECTION_AGGREGATOR)
                                                              .set_sub_sub_name(DETECTION_AI_STAGE)
                                                              .set_bbox_tiles(TILES)
                                                              .set_queue_size(5)
                                                              .set_leaky_opt(true)
                                                              .set_printfps_opt(app_resources->print_fps)
                                                              .set_pool_mode_opt(StagePoolMode::BLOCKING)
                                                              .set_crop_every_x_frames(1)
                                                              .buildptr();

        std::shared_ptr<HailortAsyncStage> detection_stage =
            HailortAsyncStageBuild::create()
                .set_stage_name(DETECTION_AI_STAGE)
                .set_hef_path(YOLO_HEF_FILE)
                .set_queue_size(5)
                .set_output_pool_size(50)
                .set_group_id("device0")
                .set_batch_size(5)
                .set_job_limit(10)
                .set_scheduler_threshold_opt(5)
                .set_dynamic_threshold_opt(false)
                .set_scheduler_timeout_opt(std::chrono::milliseconds(100))
                .set_printfps_opt(app_resources->print_fps)
                .set_pool_mode_opt(StagePoolMode::BLOCKING)
                .buildptr();

        std::shared_ptr<PostprocessStage> detection_post_stage = PostprocessStageBuild::create()
                                                                     .set_stage_name(POST_STAGE)
                                                                     .set_so_path(YOLO_POST_SO)
                                                                     .set_function_name_opt(YOLO_FUNC_NAME)
                                                                     .set_config_path_opt(YOLO_POST_CONF)
                                                                     .set_queue_size_opt(5)
                                                                     .set_leaky_opt(false)
                                                                     .set_printfps_opt(app_resources->print_fps)
                                                                     .buildptr();

        std::shared_ptr<AggregatorStage> detection_agg_stage = AggregatorStageBuild::create()
                                                                   .set_stage_name(DETECTION_AGGREGATOR)
                                                                   .set_blocking(true)
                                                                   .set_main_inlet_name(TILLING_STAGE)
                                                                   .set_main_queue_size(4)
                                                                   .set_main_leaky(false)
                                                                   .set_sub_inlet_name(POST_STAGE)
                                                                   .set_sub_queue_size(5)
                                                                   .set_sub_leaky(false)
                                                                   .set_multiscale_opt(true)
                                                                   .set_sync_opt(false)
                                                                   .set_iou_threshold_opt(0.3)
                                                                   .set_border_threshold_opt(0.1)
                                                                   .set_printfps_opt(app_resources->print_fps)
                                                                   .buildptr();

        std::shared_ptr<AggregatorStage> stage_1_agg_stage = AggregatorStageBuild::create()
                                                                 .set_stage_name(STAGE_1_AGGREGATOR)
                                                                 .set_blocking(true)
                                                                 .set_main_inlet_name(CALLBACK_STAGE)
                                                                 .set_main_queue_size(4)
                                                                 .set_main_leaky(true)
                                                                 .set_sub_inlet_name(DETECTION_AGGREGATOR)
                                                                 .set_sub_queue_size(3)
                                                                 .set_sub_leaky(false)
                                                                 .set_multiscale_opt(false)
                                                                 .set_sync_opt(true)
                                                                 .set_iou_threshold_opt(0.3)
                                                                 .set_border_threshold_opt(0.1)
                                                                 .set_printfps_opt(app_resources->print_fps)
                                                                 .set_timeout_opt(std::chrono::milliseconds(66))
                                                                 .buildptr();

        /*
                 __________________________________________
                /                                          \
            +--------+    +-----------+    +------+    +------------+
            |  crop  | -> | mobilenet | -> | post | -> | aggregator |
            +--------+    +-----------+    +------+    +------------+
        */

        std::shared_ptr<TeeStage> tee_stage = TeeStageBuild::create()
                                                  .set_stage_name(TEE_STAGE)
                                                  .set_queue_size(1)
                                                  .set_leaky_opt(false)
                                                  .set_printfps_opt(app_resources->print_fps)
                                                  .buildptr();

        std::shared_ptr<BBoxCropStage> bbox_crop_stage = BBoxCropStageBuild::create()
                                                             .set_stage_name(BBOX_CROP_STAGE)
                                                             .set_output_pool_size(150)
                                                             .set_input_width(bbox_crop_input_width)
                                                             .set_input_height(bbox_crop_input_height)
                                                             .set_output_width(BBOX_CROP_OUTPUT_WIDTH)
                                                             .set_output_height(BBOX_CROP_OUTPUT_HEIGHT)
                                                             .set_main_sub_name(LANDMARKS_AGGREGATOR)
                                                             .set_sub_sub_name(LANDMARKS_AI_STAGE)
                                                             .set_label(BBOX_CROP_LABEL)
                                                             .set_queue_size(5)
                                                             .set_leaky_opt(true)
                                                             .set_printfps_opt(app_resources->print_fps)
                                                             .set_pool_mode_opt(StagePoolMode::BLOCKING)
                                                             .buildptr();

        std::shared_ptr<HailortAsyncStage> landmarks_stage =
            HailortAsyncStageBuild::create()
                .set_stage_name(LANDMARKS_AI_STAGE)
                .set_hef_path(LANDMARKS_HEF_FILE)
                .set_queue_size(100)
                .set_output_pool_size(201)
                .set_group_id("device0")
                .set_batch_size(50)
                .set_job_limit(60)
                .set_scheduler_threshold_opt(50)
                .set_dynamic_threshold_opt(true)
                .set_scheduler_timeout_opt(std::chrono::milliseconds(100))
                .set_printfps_opt(app_resources->print_fps)
                .set_pool_mode_opt(StagePoolMode::BLOCKING)
                .buildptr();

        std::shared_ptr<PostprocessStage> landmarks_post_stage = PostprocessStageBuild::create()
                                                                     .set_stage_name(LANDMARKS_POST_STAGE)
                                                                     .set_so_path(LANDMARKS_POST_SO)
                                                                     .set_function_name_opt(LANDMARKS_FUNC_NAME)
                                                                     .set_config_path_opt("")
                                                                     .set_queue_size_opt(100)
                                                                     .set_leaky_opt(false)
                                                                     .set_printfps_opt(app_resources->print_fps)
                                                                     .buildptr();

        std::shared_ptr<AggregatorStage> landmarks_agg_stage = AggregatorStageBuild::create()
                                                                   .set_stage_name(LANDMARKS_AGGREGATOR)
                                                                   .set_blocking(true)
                                                                   .set_main_inlet_name(BBOX_CROP_STAGE)
                                                                   .set_main_queue_size(3)
                                                                   .set_main_leaky(false)
                                                                   .set_sub_inlet_name(LANDMARKS_POST_STAGE)
                                                                   .set_sub_queue_size(100)
                                                                   .set_sub_leaky(false)
                                                                   .set_multiscale_opt(false)
                                                                   .set_sync_opt(false)
                                                                   .set_iou_threshold_opt(0.3)
                                                                   .set_border_threshold_opt(0.1)
                                                                   .set_printfps_opt(app_resources->print_fps)
                                                                   .buildptr();

        std::shared_ptr<AggregatorStage> stage_2_agg_stage = AggregatorStageBuild::create()
                                                                 .set_stage_name(STAGE_2_AGGREGATOR)
                                                                 .set_blocking(true)
                                                                 .set_static_subframes_opt(1)
                                                                 .set_main_inlet_name(TEE_STAGE)
                                                                 .set_main_queue_size(5)
                                                                 .set_main_leaky(false)
                                                                 .set_sub_inlet_name(LANDMARKS_AGGREGATOR)
                                                                 .set_sub_queue_size(3)
                                                                 .set_sub_leaky(false)
                                                                 .set_multiscale_opt(false)
                                                                 .set_sync_opt(true)
                                                                 .set_iou_threshold_opt(0.3)
                                                                 .set_border_threshold_opt(0.1)
                                                                 .set_printfps_opt(app_resources->print_fps)
                                                                 .set_timeout_opt(std::chrono::milliseconds(33))
                                                                 .buildptr();

        /*
            +---------+    +---------+
            | tracker | -> | overlay |
            +---------+    +---------+
        */

        std::shared_ptr<LightweightTrackerStage> tracker_stage = LightweightTrackerStageBuild::create()
                                                                     .set_stage_name(TRACKER_STAGE)
                                                                     .set_queue_size_opt(1)
                                                                     .set_leaky_opt(false)
                                                                     .set_printfps_opt(false)
                                                                     .set_classification_ids({1, 2})
                                                                     .set_add_tracking_id(false)
                                                                     .set_grace_period(4)
                                                                     .set_smooth_alpha(0.5f)
                                                                     .set_weighted_average_decay(0.4f)
                                                                     .set_copy_nested_objects(false, 1)
                                                                     .set_copy_nested_objects(true, 2)
                                                                     .buildptr();

        std::shared_ptr<OverlayStage> overlay_stage = OverlayStageBuild::create()
                                                          .set_stage_name(OVERLAY_STAGE)
                                                          .set_skip_opt(app_resources->skip_drawing)
                                                          .set_partial_landmarks(!app_resources->full_landmarks)
                                                          .set_min_landmark(LANDMARKS_RANGE_MIN)
                                                          .set_max_landmark(LANDMARKS_RANGE_MAX)
                                                          .set_queue_size(1)
                                                          .set_leaky_opt(false)
                                                          .set_printfps_opt(app_resources->print_fps)
                                                          .buildptr();

        /*
            Add stages to pipeline using pipeline builder
        */

        PipelineBuilder pip_builder;

        pip_builder.add_stage(app_resources->frontend, StageType::SOURCE)
            .add_stage(stage_1_agg_stage)
            .add_stage(callback_stage)
            .add_stage(tilling_stage)
            .add_stage(detection_stage)
            .add_stage(detection_post_stage)
            .add_stage(detection_agg_stage)
            .add_stage(tee_stage)
            .add_stage(stage_2_agg_stage)
            .add_stage(bbox_crop_stage)
            .add_stage(landmarks_stage)
            .add_stage(landmarks_post_stage)
            .add_stage(landmarks_agg_stage)
            .add_stage(tracker_stage)
            .add_stage(overlay_stage);

        // Add encoder and udp to stage (except AI_SINK)
        for (auto s : streams.value())
        {
            // AI_SINK does not get an encoder since it is merged into 4K
            if (s.id != AI_SINK)
            {
                // Add encoder/udp to pipeline stage
                pip_builder.add_stage(app_resources->encoders[s.id], StageType::SINK)
                    .add_stage(app_resources->udp_outputs[s.id], StageType::SINK);
            }
        }

        /*
            Subscribe stages of the pipeline to each other
        */

        // FrontEnd pipeline stages subscription
        for (auto s : streams.value())
        {
            if (s.id == AI_SINK)
            {
                REFERENCE_CAMERA_LOG_INFO("subscribing ai pipeline to frontend for {}", s.id);
                // Subscribe tiling to frontend
                pip_builder.connect_frontend(FRONTEND_STAGE, s.id, TILLING_STAGE);
            }
            else if (s.id == VISION_SINK)
            {
                REFERENCE_CAMERA_LOG_INFO("subscribing to frontend for {}", s.id);
                // Subscribe tiling aggregator to frontend
                pip_builder.connect_frontend(FRONTEND_STAGE, s.id, CALLBACK_STAGE);
            }
            else
            {
                REFERENCE_CAMERA_LOG_INFO("subscribing to frontend for {}", s.id);
                // Subscribe encoder to frontend
                pip_builder.connect_frontend(FRONTEND_STAGE, s.id, app_resources->encoders[s.id]->get_name());
            }
        }

        // Stage 1 AI Subscriptions
        pip_builder.connect(CALLBACK_STAGE, STAGE_1_AGGREGATOR)
            .connect(TILLING_STAGE, DETECTION_AGGREGATOR)
            .connect(TILLING_STAGE, DETECTION_AI_STAGE)
            .connect(DETECTION_AI_STAGE, POST_STAGE)
            .connect(POST_STAGE, DETECTION_AGGREGATOR)
            .connect(DETECTION_AGGREGATOR, STAGE_1_AGGREGATOR);

        // Stage 2 AI Subscriptions
        pip_builder.connect(STAGE_1_AGGREGATOR, TEE_STAGE)
            .connect(TEE_STAGE, STAGE_2_AGGREGATOR)
            .connect(TEE_STAGE, BBOX_CROP_STAGE)
            .connect(BBOX_CROP_STAGE, LANDMARKS_AGGREGATOR)
            .connect(BBOX_CROP_STAGE, LANDMARKS_AI_STAGE)
            .connect(LANDMARKS_AI_STAGE, LANDMARKS_POST_STAGE)
            .connect(LANDMARKS_POST_STAGE, LANDMARKS_AGGREGATOR)
            .connect(LANDMARKS_AGGREGATOR, STAGE_2_AGGREGATOR);

        // Vision Pipeline stages
        pip_builder.connect(STAGE_2_AGGREGATOR, TRACKER_STAGE)
            .connect(TRACKER_STAGE, OVERLAY_STAGE)
            .connect(OVERLAY_STAGE, app_resources->encoders[VISION_SINK]->get_name());

        // Stream Out pipeline stages
        for (auto s : streams.value())
        {
            // AI_SINK does not get an encoder since it is merged into 4K
            if (s.id != AI_SINK)
            {
                pip_builder.connect(app_resources->encoders[s.id]->get_name(),
                                    app_resources->udp_outputs[s.id]->get_name());
            }
        }

        /*
            Build the pipeline
        */
        app_resources->pipeline = pip_builder.build();
    }
    catch (const std::exception &e)
    {
        REFERENCE_CAMERA_LOG_ERROR("{} failed: {}", __func__, e.what());
    }
}

/**
 * @brief Main function to initialize and run the application.
 *
 * This function sets up the application resources, registers a signal handler for SIGINT,
 * parses user arguments, configures the frontend and encoders, creates the pipeline,
 * subscribes elements, starts the pipeline, waits for a specified timeout, and then stops the pipeline.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return int Exit status of the application.
 */
int main(int argc, char *argv[])
{
    {
        // App resources
        std::shared_ptr<AppResources> app_resources = std::make_shared<AppResources>();
        app_resources->medialib_config_path = MEDIALIB_CONFIG_PATH;

        // register signal SIGINT and signal handler
        signal_utils::register_signal_handler([app_resources](int signal) {
            std::cout << "Stopping Pipeline..." << std::endl;
            REFERENCE_CAMERA_LOG_INFO("Stopping Pipeline...");
            // Stop pipeline
            app_resources->pipeline->stop_pipeline();
            app_resources->clear();
            // terminate program
            exit(0);
        });

        // Parse user arguments
        cxxopts::Options options = build_arg_parser();
        auto result = options.parse(argc, argv);
        std::vector<ArgumentType> argument_handling_results = handle_arguments(result, options);
        int timeout = result["timeout"].as<int>();

        for (ArgumentType argument : argument_handling_results)
        {
            switch (argument)
            {
            case ArgumentType::Help:
                return 0;
            case ArgumentType::Timeout:
                break;
            case ArgumentType::PrintFPS:
                app_resources->print_fps = true;
                break;
            case ArgumentType::PrintLatency:
                app_resources->print_latency = true;
                break;
            case ArgumentType::Config:
                app_resources->medialib_config_path = result["config-file-path"].as<std::string>();
                break;
            case ArgumentType::Profile:
                app_resources->profile_name = result["profile"].as<std::string>();
                break;
            case ArgumentType::SkipDrawing:
                app_resources->skip_drawing = true;
                break;
            case ArgumentType::FullLandmarks:
                app_resources->full_landmarks = true;
                break;
            case ArgumentType::HostIP:
                app_resources->host_ip = result["host-ip"].as<std::string>();
                break;
            case ArgumentType::Error:
                return 1;
            }
        }
        
        setenv("MEDIALIB_USE_DIV_FRAMERATE_LOGIC", "1", 1);

        // Configure frontend and encoders
        configure_frontend_and_encoders(app_resources);

        // Create pipeline and stages
        create_main_pipeline(app_resources);

        // Start pipeline
        std::cout << "Starting." << std::endl;
        REFERENCE_CAMERA_LOG_INFO("Starting.");
        app_resources->media_library->start_pipeline();
        app_resources->pipeline->start_pipeline();

        REFERENCE_CAMERA_LOG_INFO("Started playing for {} seconds.", timeout);

        // Wait
        std::this_thread::sleep_for(std::chrono::seconds(timeout));

        // Stop pipeline
        std::cout << "Stopping." << std::endl;
        REFERENCE_CAMERA_LOG_INFO("Stopping.");
        app_resources->pipeline->stop_pipeline();
        app_resources->media_library->stop_pipeline();
        app_resources->clear();
    }
    return 0;
}
