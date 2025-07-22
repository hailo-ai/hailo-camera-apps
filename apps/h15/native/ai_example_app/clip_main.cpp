// general includes
#include <chrono>
#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <tl/expected.hpp>
#include <signal.h>
#include <cxxopts/cxxopts.hpp>

// medialibrary includes
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
#include "tracker_stage.hpp"
#include "persist_stage.hpp"
#include "aggregator_stage.hpp"
#include "zmq_comm_stage.hpp"
#include <zmq.hpp>
#include "tracker_traffic_ctrl_stage.hpp"

// Frontend Params
#define FRONTEND_STAGE "frontend_stage"
#define FRONTEND_CONFIG_FILE "/home/root/apps/ai_example_app/resources/configs/clip_profile.json"
#define ENCODER_OSD_CONFIG_FILE(id) get_encoder_osd_config_file(id)
#define OUTPUT_FILE(id) get_output_file(id)

#define OVERLAY_STAGE "overlay"
#define TRACKER_STAGE "tracker"
#define TEE_STAGE "teeroicp"
#define TRACKER_TRAFFIC_CTRL_STAGE2 "TrackerTrafficCtrlStage2"
#define UDP_0_STAGE "udp_0"
#define HOST_IP "10.0.0.2"

// AI Pipeline Params
#define AI_VISION_SINK "sink0" // The streamid from frontend to 4K stream that shows vision results
#define AI_SINK "sink1"        // The streamid from frontend to AI
// Detection AI Params
#define YOLO_HEF_FILE "/home/root/apps/ai_example_app/resources/yolov8n_personface_nv12.hef"
#define DETECTION_AI_STAGE "yolo_detection"
// Detection Postprocess Params
#define POST_STAGE "yolo_post"
#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so"
#define YOLO_FUNC_NAME "yolov8n_personface"
// Aggregator Params
#define AGGREGATOR_STAGE "aggregator"
#define AGGREGATOR_STAGE_2 "aggregator2"
// Callback Params
#define AI_CALLBACK_STAGE "ai_to_encoder"

// Tilling Params
#define TILLING_STAGE "tilling"
#define TILLING_INPUT_WIDTH 1920
#define TILLING_INPUT_HEIGHT 1080
#define TILLING_OUTPUT_WIDTH 640
#define TILLING_OUTPUT_HEIGHT 640
std::vector<HailoBBox> TILES = {{0.0, 0.0, 1.0, 1.0}};

// Bbox crop Parms
#define BBOX_CROP_STAGE "bbox_crops"
#define BBOX_CROP_LABEL "person"
#define BBOX_CROP_OUTPUT_WIDTH 288
#define BBOX_CROP_OUTPUT_HEIGHT 288

// CLIP AI Params
#define CLIP_HEF_FILE "/home/root/apps/ai_example_app/resources/clip_resnet_50x4_image_encoder_nv12.hef"
#define CLIP_AI_STAGE "clip"
// Clip Postprocess Params
#define CLIP_POST_STAGE "clip_post"
#define CLIP_POST_SO "/usr/lib/hailo-post-processes/libclip_post.so"
#define CLIP_FUNC_NAME "clip_resnet_50_nv12"

// Clip ZMQ Params
#define CLIP_ZMQ_RECEIVER_STAGE "clip_zmq_receiver"
#define CLIP_ZMQ_PUBLISHER_STAGE "clip_zmq_publisher"
#define CLIP_ZMQ_SUB_ADDR "tcp://10.0.0.2:5555"
#define CLIP_ZMQ_PUB_ADDR "tcp://10.0.0.1:7000"
#define CLIP_CLASS_IDS_TO_DRAW std::unordered_set<int>{1}
// Macro that turns coverts stream ids to port #s
#define PORT_FROM_ID(id) std::to_string(5000 + std::stoi(id.substr(4)) * 2)

enum class ArgumentType
{
    Help,
    PrintFPS,
    PrintLatency,
    Timeout,
    Config,
    HostIP,
    Error
};

void print_help(const cxxopts::Options &options)
{
    std::cout << options.help() << std::endl;
}

cxxopts::Options build_arg_parser()
{
    cxxopts::Options options("AI pipeline app");
    options.add_options()("h,help", "Show this help")("t,timeout", "Time to run",
                                                      cxxopts::value<int>()->default_value("12"))(
        "f,print-fps", "Print FPS", cxxopts::value<bool>()->default_value("false"))(
        "l, print-latency", "Print Latency", cxxopts::value<bool>()->default_value("false"))(
        "c, config-file-path", "Frontend Configuration Path",
        cxxopts::value<std::string>()->default_value(FRONTEND_CONFIG_FILE))(
        "o,host-ip", "Host IP address for UDP output", cxxopts::value<std::string>()->default_value(HOST_IP));
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
    std::string frontend_config;
    std::string host_ip = HOST_IP;

    void clear()
    {
        frontend = nullptr;
        pipeline = nullptr;
        encoders.clear();
        udp_outputs.clear();
        print_fps = false;
        print_latency = false;
        frontend_config = "";
        host_ip = HOST_IP;
    }

    ~AppResources()
    {
        clear();
    }
};

inline std::string get_encoder_osd_config_file(const std::string &id)
{
    return "/home/root/apps/ai_example_app/resources/configs/encoder_osd_" + id + ".json";
}

std::string read_string_from_file(const char *file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error("config path is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)), std::istreambuf_iterator<char>());
    file_to_read.close();
    std::cout << "Read config from file: " << file_path << std::endl;
    return file_string;
}

/**
 * @brief Subscribe elements within the application pipeline.
 *
 * This function subscribes the output streams from the frontend to appropriate
 * pipeline stages and encoders, ensuring that the data flows correctly through
 * the pipeline. It sets up callbacks for handling the data and integrates encoders
 * with UDP outputs.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
void subscribe_to_frontend(std::shared_ptr<AppResources> app_resources)
{
    // Get frontend output streams
    auto streams = app_resources->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }
    else
    {
        for (const auto &s : streams.value())
        {
            std::cout << "Stream ID: " << s.id << ", width: " << s.width << ", height: " << s.height << std::endl;
        }
    }

    // Subscribe to frontend
    for (auto s : streams.value())
    {
        if (s.id == AI_SINK)
        {
            std::cout << "subscribing ai pipeline to frontend for '" << s.id << "'" << std::endl;
            // Subscribe tiling to frontend
            app_resources->frontend->subscribe_to_stream(
                s.id,
                std::static_pointer_cast<ConnectedStage>(app_resources->pipeline->get_stage_by_name(TILLING_STAGE)));
        }
        else if (s.id == AI_VISION_SINK)
        {
            std::cout << "subscribing to frontend for '" << s.id << "'" << std::endl;
            // Subscribe tiling aggregator to frontend
            app_resources->frontend->subscribe_to_stream(
                s.id,
                std::static_pointer_cast<ConnectedStage>(app_resources->pipeline->get_stage_by_name(AGGREGATOR_STAGE)));
        }
        else
        {
            std::cout << "subscribing to frontend for '" << s.id << "'" << std::endl;
            // Subscribe encoder to frontend
            app_resources->frontend->subscribe_to_stream(s.id, app_resources->encoders[s.id]);
        }
    }
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

    // Add encoder/udp to pipeline
    app_resources->pipeline->add_stage(app_resources->encoders[id], StageType::SINK);
    app_resources->pipeline->add_stage(app_resources->udp_outputs[id], StageType::SINK);

    // Subscribe udp to encoder
    app_resources->encoders[id]->add_subscriber(app_resources->udp_outputs[id]);
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
    // Create and configure frontend
    // configure by media library ptr
    std::string frontend_config_string = read_string_from_file(app_resources->frontend_config.c_str());
    app_resources->media_library = std::make_shared<MediaLibrary>();
    if (app_resources->media_library->initialize(frontend_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return;
    }
    app_resources->frontend = std::make_shared<FrontendStage>(FRONTEND_STAGE);
    app_resources->pipeline->add_stage(app_resources->frontend, StageType::SOURCE);
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
        std::cout << "create encoder s.id: " << s.id << std::endl;
        if (s.id == AI_SINK)
        {
            std::cout << "skip create_encoder_and_udp" << std::endl;
            // AI pipeline does not get an encoder since it is merged into 4K
            continue;
        }
        create_encoder_and_udp(s.id, app_resources);
        std::cout << "create_encoder_and_udp" << std::endl;
    }
}

auto clip_color_selector = [](const HailoDetectionPtr &det) -> cv::Scalar {
    for (const auto &obj : det->get_objects())
    {
        if (obj->get_type() == HAILO_CLASSIFICATION)
        {
            auto cls = std::dynamic_pointer_cast<HailoClassification>(obj);
            return get_color(cls->get_class_id());
        }
    }

    return cv::Scalar(255, 255, 255);
};

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
void create_ai_pipeline(std::shared_ptr<AppResources> app_resources)
{
    // Get the input resolution from frontend
    auto streams = app_resources->frontend->get_outputs_streams();

    // Retrieve the AI vision input resolution for sanity check and which we will be using for crop
    int bbox_crop_input_width = 0;
    int bbox_crop_input_height = 0;
    if (streams.has_value())
    {
        for (const auto &stream : streams.value())
        {
            if (stream.id == AI_VISION_SINK)
            {
                bbox_crop_input_width = stream.width;
                bbox_crop_input_height = stream.height;
                break;
            }
        }
    }

    std::cout << "width " << bbox_crop_input_width << " height " << bbox_crop_input_height << std::endl;

    if (bbox_crop_input_width == 0 || bbox_crop_input_height == 0)
    {
        std::cerr << "Failed to get input resolution from frontend" << std::endl;
        throw std::runtime_error("Failed to get input resolution from frontend");
    }
    // AI Pipeline Stages
    std::shared_ptr<TillingCropStage> tilling_stage = TillingCropStageBuild::create()
                                                          .set_stage_name(TILLING_STAGE)
                                                          .set_output_pool_size(50)
                                                          .set_input_width(TILLING_INPUT_WIDTH)
                                                          .set_input_height(TILLING_INPUT_HEIGHT)
                                                          .set_output_width(TILLING_OUTPUT_WIDTH)
                                                          .set_output_height(TILLING_OUTPUT_HEIGHT)
                                                          .set_main_sub_name("")
                                                          .set_sub_sub_name(DETECTION_AI_STAGE)
                                                          .set_bbox_tiles(TILES)
                                                          .set_queue_size(5)
                                                          .set_leaky_opt(true)
                                                          .set_printfps_opt(app_resources->print_fps)
                                                          .buildptr();

    std::shared_ptr<HailortAsyncStage> detection_stage = HailortAsyncStageBuild::create()
                                                             .set_stage_name(DETECTION_AI_STAGE)
                                                             .set_hef_path(YOLO_HEF_FILE)
                                                             .set_queue_size(5)
                                                             .set_output_pool_size(50)
                                                             .set_group_id("device0")
                                                             .set_batch_size(8)
                                                             .set_job_limit(5)
                                                             .set_scheduler_threshold_opt(4)
                                                             .set_dynamic_threshold_opt(false)
                                                             .set_scheduler_timeout_opt(std::chrono::milliseconds(100))
                                                             .set_printfps_opt(app_resources->print_fps)
                                                             .buildptr();

    std::shared_ptr<PostprocessStage> detection_post_stage = PostprocessStageBuild::create()
                                                                 .set_stage_name(POST_STAGE)
                                                                 .set_so_path(YOLO_POST_SO)
                                                                 .set_function_name_opt(YOLO_FUNC_NAME)
                                                                 .set_config_path_opt("")
                                                                 .set_queue_size_opt(5)
                                                                 .set_leaky_opt(false)
                                                                 .set_printfps_opt(app_resources->print_fps)
                                                                 .buildptr();

    std::shared_ptr<AggregatorStage> agg_stage = AggregatorStageBuild::create()
                                                     .set_stage_name(AGGREGATOR_STAGE)
                                                     .set_blocking(false)
                                                     .set_static_subframes_opt(1)
                                                     .set_main_inlet_name(AI_VISION_SINK)
                                                     .set_main_queue_size(5)
                                                     .set_main_leaky(false)
                                                     .set_sub_inlet_name(POST_STAGE)
                                                     .set_sub_queue_size(10)
                                                     .set_sub_leaky(false)
                                                     .set_multiscale_opt(true)
                                                     .set_sync_opt(false)
                                                     .set_iou_threshold_opt(0.3)
                                                     .set_border_threshold_opt(0.1)
                                                     .set_printfps_opt(app_resources->print_fps)
                                                     .buildptr();

    std::shared_ptr<TrackerStage> tracker_stage =
        TrackerStageBuild::create()
            .set_stage_name(TRACKER_STAGE)
            .set_queue_size_opt(5)
            .set_leaky_opt(false)
            .set_classification_id(1)                      // Only track person
            .set_block_non_tracked_classification_id(true) // We block all non person object
            .set_printfps_opt(app_resources->print_fps)
            .buildptr();

    std::shared_ptr<TeeStage> tee_stage = TeeStageBuild::create()
                                              .set_stage_name(TEE_STAGE)
                                              .set_queue_size(5)
                                              .set_leaky_opt(true)
                                              .set_printfps_opt(app_resources->print_fps)
                                              .buildptr();

    std::shared_ptr<TrackerTrafficCtrlStage> tracker_traffic_ctrl_stage2 =
        TrackerTrafficCtrlStageBuild::create()
            .set_stage_name(TRACKER_TRAFFIC_CTRL_STAGE2)
            .set_classified_fps_to_block(30 * 2) // Let a detection frame pass every 60 classified frame
            .set_unclassified_fps_to_block(10)   // Let a detection frame pass every 10 unclassified frame
            .set_leaky_opt(false)
            .set_printfps_opt(app_resources->print_fps)
            .buildptr();

    std::shared_ptr<BBoxCropStage> bbox_crop_stage = BBoxCropStageBuild::create()
                                                         .set_stage_name(BBOX_CROP_STAGE)
                                                         .set_output_pool_size(250)
                                                         .set_input_width(bbox_crop_input_width)
                                                         .set_input_height(bbox_crop_input_height)
                                                         .set_output_width(BBOX_CROP_OUTPUT_WIDTH)
                                                         .set_output_height(BBOX_CROP_OUTPUT_HEIGHT)
                                                         .set_main_sub_name(AGGREGATOR_STAGE_2)
                                                         .set_sub_sub_name(CLIP_ZMQ_RECEIVER_STAGE)
                                                         .set_label(BBOX_CROP_LABEL)
                                                         .set_queue_size(5)
                                                         .set_leaky_opt(false)
                                                         .set_printfps_opt(app_resources->print_fps)
                                                         .buildptr();

    std::shared_ptr<ZmqCommStage> clip_zmq_receiver = ZmqCommStageBuild::create()
                                                          .set_stage_name(CLIP_ZMQ_RECEIVER_STAGE)
                                                          .set_mode(ZmqCommStage::Mode::RECEIVER)
                                                          .set_sub_address(CLIP_ZMQ_SUB_ADDR)
                                                          .set_queue_size(5)
                                                          .set_leaky(false)
                                                          .set_print_fps(false)
                                                          .buildptr();

    std::shared_ptr<HailortAsyncStage> clip_stage = HailortAsyncStageBuild::create()
                                                        .set_stage_name(CLIP_AI_STAGE)
                                                        .set_hef_path(CLIP_HEF_FILE)
                                                        .set_queue_size(20)
                                                        .set_output_pool_size(101)
                                                        .set_group_id("device0")
                                                        .set_batch_size(6)
                                                        .set_job_limit(6)
                                                        .set_scheduler_threshold_opt(4)
                                                        .set_dynamic_threshold_opt(false)
                                                        .set_scheduler_timeout_opt(std::chrono::milliseconds(100))
                                                        .set_printfps_opt(app_resources->print_fps)
                                                        .buildptr();

    std::shared_ptr<PostprocessStage> clip_post_stage = PostprocessStageBuild::create()
                                                            .set_stage_name(CLIP_POST_STAGE)
                                                            .set_so_path(CLIP_POST_SO)
                                                            .set_function_name_opt(CLIP_FUNC_NAME)
                                                            .set_config_path_opt("")
                                                            .set_queue_size_opt(50)
                                                            .set_leaky_opt(false)
                                                            .set_printfps_opt(app_resources->print_fps)
                                                            .buildptr();

    std::shared_ptr<ZmqCommStage> clip_zmq_publisher = ZmqCommStageBuild::create()
                                                           .set_stage_name(CLIP_ZMQ_PUBLISHER_STAGE)
                                                           .set_mode(ZmqCommStage::Mode::PUBLISHER)
                                                           .set_pub_address(CLIP_ZMQ_PUB_ADDR)
                                                           .set_queue_size(5)
                                                           .set_leaky(false)
                                                           .set_print_fps(false)
                                                           .buildptr();

    std::shared_ptr<AggregatorStage> agg_stage_2 = AggregatorStageBuild::create()
                                                       .set_stage_name(AGGREGATOR_STAGE_2)
                                                       .set_blocking(false)
                                                       .set_static_subframes_opt(1)
                                                       .set_main_inlet_name(TEE_STAGE)
                                                       .set_main_queue_size(5)
                                                       .set_main_leaky(false)
                                                       .set_sub_inlet_name(CLIP_POST_STAGE)
                                                       .set_sub_queue_size(30)
                                                       .set_sub_leaky(false)
                                                       .set_multiscale_opt(false)
                                                       .set_sync_opt(false)
                                                       .set_iou_threshold_opt(0.3)
                                                       .set_border_threshold_opt(0.1)
                                                       .set_printfps_opt(app_resources->print_fps)
                                                       .buildptr();

    std::shared_ptr<OverlayStage> overlay_stage = OverlayStageBuild::create()
                                                      .set_stage_name(OVERLAY_STAGE)
                                                      .set_skip_opt(false)
                                                      .set_partial_landmarks(false)
                                                      .set_class_ids_to_draw(CLIP_CLASS_IDS_TO_DRAW)
                                                      .set_color_selector(clip_color_selector)
                                                      .set_printfps_opt(app_resources->print_fps)
                                                      .buildptr();

    // Add stages to pipeline
    app_resources->pipeline->add_stage(tilling_stage);
    app_resources->pipeline->add_stage(detection_stage);
    app_resources->pipeline->add_stage(detection_post_stage);
    app_resources->pipeline->add_stage(agg_stage);
    app_resources->pipeline->add_stage(tracker_stage);
    app_resources->pipeline->add_stage(tee_stage);
    app_resources->pipeline->add_stage(tracker_traffic_ctrl_stage2);
    app_resources->pipeline->add_stage(bbox_crop_stage);
    app_resources->pipeline->add_stage(clip_zmq_receiver);
    app_resources->pipeline->add_stage(clip_stage);
    app_resources->pipeline->add_stage(clip_post_stage);
    app_resources->pipeline->add_stage(clip_zmq_publisher);
    app_resources->pipeline->add_stage(agg_stage_2);
    app_resources->pipeline->add_stage(overlay_stage);

    // Subscribe stages to each other
    tilling_stage->add_subscriber(detection_stage);
    detection_stage->add_subscriber(detection_post_stage);
    detection_post_stage->add_subscriber(agg_stage);
    agg_stage->add_subscriber(tracker_stage);

    tracker_stage->add_subscriber(tee_stage);
    tee_stage->add_subscriber(agg_stage_2);
    tee_stage->add_subscriber(tracker_traffic_ctrl_stage2);
    tracker_traffic_ctrl_stage2->add_subscriber(bbox_crop_stage);

    bbox_crop_stage->add_subscriber(clip_zmq_receiver);
    clip_zmq_receiver->add_subscriber(clip_stage);
    clip_stage->add_subscriber(clip_post_stage);
    clip_post_stage->add_subscriber(clip_zmq_publisher);
    clip_post_stage->add_subscriber(agg_stage_2);
    agg_stage_2->add_subscriber(overlay_stage);
    overlay_stage->add_subscriber(app_resources->encoders[AI_VISION_SINK]);
}

void zmq_subscriber_thread(std::shared_ptr<AppResources> app_resources)
{
    // subscriber for quit message
    zmq::context_t context(1);
    zmq::socket_t subscriber(context, ZMQ_SUB);

    // Connect to the ZeroMQ server
    subscriber.connect("tcp://10.0.0.2:6000"); // Adjust the address and port as needed
    std::cout << "connected to 10.0.0.2:6000" << std::endl;
    subscriber.set(zmq::sockopt::subscribe, "");
    // Infinite loop to receive messages
    while (true)
    {
        // Receive the message
        zmq::message_t message;
        auto recv_result = subscriber.recv(message, zmq::recv_flags::none);
        if (!recv_result)
        {
            std::cerr << "Failed to receive message." << std::endl;
            continue; // Skip processing if reception fails
        }
        // Convert the message to a string
        std::string received_message(static_cast<char *>(message.data()), message.size());

        // Print the received message
        std::cout << "Received message: " << received_message << std::endl;

        // Check if the message is "quit"
        if (received_message == "quit")
        {
            std::cout << "Quit message received. Exiting..." << std::endl;
            app_resources->pipeline->stop_pipeline();
            app_resources->clear();
            exit(0);
        }
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
        app_resources->frontend_config = FRONTEND_CONFIG_FILE;

        // register signal SIGINT and signal handler
        signal_utils::register_signal_handler([app_resources](int signal) {
            std::cout << "Stopping Pipeline..." << std::endl;
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
                app_resources->frontend_config = result["config-file-path"].as<std::string>();
                break;
            case ArgumentType::HostIP:
                app_resources->host_ip = result["host-ip"].as<std::string>();
                break;
            case ArgumentType::Error:
                return 1;
            }
        }

        // Create pipeline
        app_resources->pipeline = std::make_shared<Pipeline>();

        // Configure frontend and encoders
        configure_frontend_and_encoders(app_resources);

        // Create pipeline and stages
        create_ai_pipeline(app_resources);

        // Subscribe stages to frontend
        subscribe_to_frontend(app_resources);

        // Start pipeline
        std::cout << "Starting." << std::endl;
        app_resources->pipeline->start_pipeline();

        std::cout << "Using frontend config: " << app_resources->frontend_config << std::endl;

        std::thread zmq_thread(zmq_subscriber_thread, std::ref(app_resources));
        zmq_thread.detach();

        std::cout << "Started playing for " << timeout << " hours." << std::endl;

        // Wait
        std::this_thread::sleep_for(std::chrono::hours(timeout));

        // Stop pipeline
        std::cout << "Stopping." << std::endl;
        app_resources->pipeline->stop_pipeline();
        app_resources->clear();
    }
    return 0;
}
