

// general includes
#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
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
#include "tracker_stage.hpp"
#include "persist_stage.hpp"
#include "aggregator_stage.hpp"
#include "reference_camera_logger.hpp"

// Frontend Params
#define FRONTEND_STAGE "frontend_stage"
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/ai_example_medialib_config.json"

#define OVERLAY_STAGE "overlay"
#define TRACKER_STAGE "tracker"
#define UDP_0_STAGE "udp_0"
#define HOST_IP "10.0.0.2"

// AI Pipeline Params
#define AI_VISION_SINK "sink0" // The streamid from frontend to 4K stream that shows vision results 
#define AI_SINK "sink2" // The streamid from frontend to AI
// Detection AI Params
#define YOLO_HEF_FILE "/home/root/apps/ai_example_app/resources/yolov5s_personface_nv12.hef"
#define DETECTION_AI_STAGE "yolo_detection"
// Detection Postprocess Params
#define POST_STAGE "yolo_post"
#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libdebug.so"
#define YOLO_FUNC_NAME "generate_fixed_detections"
// Aggregator Params
#define RESULTS_AGGREGATOR_STAGE "results_aggregator"
#define AGGREGATOR_STAGE "aggregator"
#define AGGREGATOR_STAGE_2 "aggregator2"
// Tee Params
#define TEE_STAGE "vision_tee"

// Tilling Params
#define TILLING_STAGE "tilling"
#define TILLING_INPUT_WIDTH 1920
#define TILLING_INPUT_HEIGHT 1080
#define TILLING_OUTPUT_WIDTH 640
#define TILLING_OUTPUT_HEIGHT 640
std::vector<HailoBBox> TILES = {{0.0,0.0,0.6,0.6},  {0.4,0,0.6,0.6},  
                                {0, 0.4, 0.6, 0.6},  {0.4, 0.4, 0.6, 0.6}, 
                                {0.0, 0.0, 1.0, 1.0}};

// Bbox crop Parms
#define BBOX_CROP_STAGE "bbox_crops"
#define BBOX_CROP_LABEL "face"
#define BBOX_CROP_INPUT_WIDTH 3840
#define BBOX_CROP_INPUT_HEIGHT 2160
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

// Fake Detections
#define FAKE_DETECTIONS_STAGE "fake_detections"

// Macro that turns coverts stream ids to port #s
#define PORT_FROM_ID(id) std::to_string(5000 + std::stoi(id.substr(4)) * 2)

enum class ArgumentType {
    Help,
    PrintFPS,
    PrintLatency,
    Timeout,
    Config,
    SkipDrawing,
    FullLandmarks,
    Error
};

void print_help(const cxxopts::Options &options) {
    std::cout << options.help() << std::endl;
}

cxxopts::Options build_arg_parser()
{
  cxxopts::Options options("AI pipeline app");
  options.add_options()
  ("h, help", "Show this help")
  ("t, timeout", "Time to run", cxxopts::value<int>()->default_value("60"))
  ("p, print-fps", "Print FPS",  cxxopts::value<bool>()->default_value("false"))
  ("l, print-latency", "Print Latency", cxxopts::value<bool>()->default_value("false"))
  ("c, config-file-path", "media library Configuration Path", cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH))
  ("s, skip-drawing", "Skip drawing", cxxopts::value<bool>()->default_value("false"))
  ("f, full-landmarks", "Draw all landmarks (default draws only eyes for face landmarks)", cxxopts::value<bool>()->default_value("false"));
  return options;
}

std::vector<ArgumentType> handle_arguments(const cxxopts::ParseResult &result, const cxxopts::Options &options) {
    std::vector<ArgumentType> arguments;

    if (result.count("help")) {
        print_help(options);
        arguments.push_back(ArgumentType::Help);
    }

    if (result.count("print-fps")) {
        arguments.push_back(ArgumentType::PrintFPS);
    }

    if (result.count("timeout")) {
        arguments.push_back(ArgumentType::Timeout);
    }

    if (result.count("print-latency")) {
        arguments.push_back(ArgumentType::PrintLatency);
    }

    if (result.count("config-file-path")) {
        arguments.push_back(ArgumentType::Config);
    }

    if (result.count("skip-drawing")) {
        arguments.push_back(ArgumentType::SkipDrawing);
    }

    if (result.count("full-landmarks")) {
        arguments.push_back(ArgumentType::FullLandmarks);
    }

    // Handle unrecognized options
    for (const auto &unrecognized : result.unmatched()) {
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
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)),
                            std::istreambuf_iterator<char>());
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

    // Subscribe to frontend
    for (auto s : streams.value())
    {
        if (s.id == AI_SINK)
        {
            std::cout << "subscribing ai pipeline to frontend for '" << s.id << "'" << std::endl;
            // Subscribe tiling to frontend
            app_resources->frontend->subscribe_to_stream(s.id, 
                std::static_pointer_cast<ConnectedStage>(app_resources->pipeline->get_stage_by_name(TILLING_STAGE)));
        }
        else if (s.id == AI_VISION_SINK)
        {
            std::cout << "subscribing to frontend for '" << s.id << "'" << std::endl;
            // Subscribe tiling aggregator to frontend
            app_resources->frontend->subscribe_to_stream(s.id, 
                std::static_pointer_cast<ConnectedStage>(app_resources->pipeline->get_stage_by_name(TEE_STAGE)));
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
void create_encoder_and_udp(const std::string& id, std::shared_ptr<AppResources> app_resources)
{
    // Create and configure encoder
    std::string enc_name = "enc_" + id;
    std::cout << "Creating encoder " << enc_name << std::endl;
    std::shared_ptr<EncoderStage> encoder_stage = std::make_shared<EncoderStage>(enc_name);
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
    std::shared_ptr<UdpStage> udp_stage = std::make_shared<UdpStage>(udp_name);
    app_resources->udp_outputs[id] = udp_stage;
    AppStatus udp_config_status = udp_stage->configure(HOST_IP, PORT_FROM_ID(id), EncodingType::H264);
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
    std::string medialib_config_string = read_string_from_file(app_resources->medialib_config_path.c_str());
    app_resources->media_library = std::make_shared<MediaLibrary>();
    if (app_resources->media_library->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return;
    }
    // Create and configure frontend
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
        if (s.id == AI_SINK)
        {
            // AI pipeline does not get an encoder since it is merged into 4K
            continue;
        }
        create_encoder_and_udp(s.id, app_resources);
    }
}

void generate_fifty_detections(BufferPtr input_buffer)
{
    HailoROIPtr roi = input_buffer->get_roi();
    // Get the bounding limits for the new boxes
    HailoBBox bbox_limit = roi->get_bbox();
    float xmin_limit = bbox_limit.xmin();
    float ymin_limit = bbox_limit.ymin();
    // Calculate fixed dimensions for each detection
    float r_confidence = 1.0;
    int num_rows = 5;
    int num_cols = 10;
    float detection_width = bbox_limit.width() / num_cols;
    float detection_height = bbox_limit.height() / num_rows;
    for (int i = 0; i < num_rows; i++)
    {
        for (int j=0; j < num_cols; j++)
        {
            // add a person
            float r_xmin = xmin_limit + (j * detection_width);  // Arrange in 10 columns
            float r_ymin = ymin_limit + (i * detection_height); // Arrange in 5 rows
            HailoBBox person_bbox = HailoBBox(r_xmin, r_ymin, detection_width * 0.9, detection_height * 0.9);
            hailo_common::add_detection(roi, person_bbox, "person", r_confidence);

            // add a face
            HailoBBox face_bbox = HailoBBox(r_xmin + (detection_width * 0.25), r_ymin + (detection_height * 0.2),
                                            detection_width * 0.5, detection_height * 0.3);
            hailo_common::add_detection(roi, face_bbox, "face", r_confidence);
        }
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
void create_ai_pipeline(std::shared_ptr<AppResources> app_resources)
{
    // AI Pipeline Stages
    std::shared_ptr<TeeStage> tee_stage = std::make_shared<TeeStage>(TEE_STAGE, 2, false, app_resources->print_fps);
    std::shared_ptr<TillingCropStage> tilling_stage = std::make_shared<TillingCropStage>(TILLING_STAGE,50, TILLING_INPUT_WIDTH, TILLING_INPUT_HEIGHT,
                                                                                        TILLING_OUTPUT_WIDTH, TILLING_OUTPUT_HEIGHT,
                                                                                        "", AGGREGATOR_STAGE, TILES,
                                                                                        5, true, app_resources->print_fps, StagePoolMode::BLOCKING);
    std::shared_ptr<AggregatorStage> agg_stage = std::make_shared<AggregatorStage>(AGGREGATOR_STAGE, false, 5,
                                                                                   TEE_STAGE, 2, true,
                                                                                   TILLING_STAGE, 15, false,
                                                                                   true, false, 0.3, 0.1,
                                                                                   app_resources->print_fps);
    std::shared_ptr<CallbackStage> fake_detection_stage = std::make_shared<CallbackStage>(FAKE_DETECTIONS_STAGE, 3, false, generate_fifty_detections, app_resources->print_fps);
    std::shared_ptr<BBoxCropStage> bbox_crop_stage = std::make_shared<BBoxCropStage>(BBOX_CROP_STAGE, 150, BBOX_CROP_INPUT_WIDTH, BBOX_CROP_INPUT_HEIGHT,
                                                                                    BBOX_CROP_OUTPUT_WIDTH, BBOX_CROP_OUTPUT_HEIGHT,
                                                                                    AGGREGATOR_STAGE_2, LANDMARKS_POST_STAGE, BBOX_CROP_LABEL, 1, false, 
                                                                                    app_resources->print_fps, StagePoolMode::BLOCKING);
    std::shared_ptr<PostprocessStage> landmarks_post_stage = std::make_shared<PostprocessStage>(LANDMARKS_POST_STAGE, LANDMARKS_POST_SO, LANDMARKS_FUNC_NAME, "", 100, false, app_resources->print_fps);
    std::shared_ptr<AggregatorStage> agg_stage_2 = std::make_shared<AggregatorStage>(AGGREGATOR_STAGE_2, true, 
                                                                                     BBOX_CROP_STAGE, 3, false,
                                                                                     LANDMARKS_POST_STAGE, 100, false,
                                                                                     false, false, 0.3, 0.1,
                                                                                     app_resources->print_fps);
    
    std::shared_ptr<AggregatorStage> results_agg_stage = std::make_shared<AggregatorStage>(RESULTS_AGGREGATOR_STAGE, false, 1,
                                                                                    TEE_STAGE, 2, true,
                                                                                    AGGREGATOR_STAGE_2, 2, false,
                                                                                    false, false, 0.3, 0.1,
                                                                                    app_resources->print_fps);
    std::shared_ptr<PersistStage> tracker_stage = std::make_shared<PersistStage>(TRACKER_STAGE, 3, 1, false, app_resources->print_fps);
    std::shared_ptr<OverlayStage> overlay_stage = std::make_shared<OverlayStage>(OVERLAY_STAGE, app_resources->skip_drawing, !app_resources->full_landmarks, LANDMARKS_RANGE_MIN, LANDMARKS_RANGE_MAX, 1, false, app_resources->print_fps);

    // Add stages to pipeline
    app_resources->pipeline->add_stage(tee_stage);
    app_resources->pipeline->add_stage(results_agg_stage);
    app_resources->pipeline->add_stage(tracker_stage);
    app_resources->pipeline->add_stage(overlay_stage);
    
    app_resources->pipeline->add_stage(agg_stage);
    app_resources->pipeline->add_stage(tilling_stage);
    app_resources->pipeline->add_stage(fake_detection_stage);
    app_resources->pipeline->add_stage(bbox_crop_stage);
    app_resources->pipeline->add_stage(landmarks_post_stage);
    app_resources->pipeline->add_stage(agg_stage_2);

    // Subscribe stages to each other  
    // AI Pipeline stages  
    tee_stage->add_subscriber(agg_stage);
    tilling_stage->add_subscriber(agg_stage);
    agg_stage->add_subscriber(fake_detection_stage);
    fake_detection_stage->add_subscriber(bbox_crop_stage);
    bbox_crop_stage->add_subscriber(agg_stage_2);
    bbox_crop_stage->add_subscriber(landmarks_post_stage);
    landmarks_post_stage->add_subscriber(agg_stage_2);
    agg_stage_2->add_subscriber(results_agg_stage);

    // Vision Pipeline stages
    tee_stage->add_subscriber(results_agg_stage);
    results_agg_stage->add_subscriber(tracker_stage);
    tracker_stage->add_subscriber(overlay_stage);
    overlay_stage->add_subscriber(app_resources->encoders[AI_VISION_SINK]);
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
        signal_utils::register_signal_handler([app_resources](int signal)
        { 
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
        int timeout  = result["timeout"].as<int>();

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
            case ArgumentType::SkipDrawing:
                app_resources->skip_drawing = true;
                break;
            case ArgumentType::FullLandmarks:
                app_resources->full_landmarks = true;
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
        REFERENCE_CAMERA_LOG_INFO("Starting.");
        app_resources->pipeline->start_pipeline();

        REFERENCE_CAMERA_LOG_INFO("Started playing for {} seconds.", timeout);

        // Wait
        std::this_thread::sleep_for(std::chrono::seconds(timeout));

        // Stop pipeline
        std::cout << "Stopping." << std::endl;
        REFERENCE_CAMERA_LOG_INFO("Stopping.");
        app_resources->pipeline->stop_pipeline();
        app_resources->clear();
    }
    return 0;
}
