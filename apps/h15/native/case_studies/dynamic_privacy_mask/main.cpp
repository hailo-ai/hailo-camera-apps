// general includes
#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <tl/expected.hpp>
#include <cxxopts/cxxopts.hpp>
#include <filesystem>

// medialibrary includes
#include "media_library/media_library.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/signal_utils.hpp"

// infra includes
#include "pipeline.hpp"
#include "udp_stage.hpp"
#include "analytics_db_stage.hpp"
#include "ai_stage.hpp"
#include "postprocess_stage.hpp"
#include "encoder_stage.hpp"
#include "frontend_stage.hpp"
#include "reference_camera_logger.hpp"

// Stage Params
#define FRONTEND_STAGE "frontend_stage"
#define HOST_IP "10.0.0.2"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/case_studies/dynamic_privacy_mask_medialib_config.json"

#define YOLO_HEF_FILE "/home/root/apps/dynamic_privacy_mask/resources/yolov5l_seg_1class_nv12.hef"
#define SEGMENTATION_AI_STAGE "yolo_segmentation"

#define POST_STAGE "yolo_post"
#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so"
#define YOLO_FUNC_NAME "yolov5_seg"

#define ANALYTICS_DB_STAGE "analytics_db"
#define ANALYTICS_DATA_ID "yolo_segmentation"
constexpr uint32_t AI_WIDTH = 640;
constexpr uint32_t AI_HEIGHT = 480;

// Macro that turns coverts stream ids to port #s
#define PORT_FROM_ID(id) std::to_string(5000 + std::stoi(id.substr(4)) * 2)

#define AI_ANALYTICS_SINK "sink1"
constexpr float DEFAULT_NMS_SCORE_THRESHOLD = 0.2f;

enum class ArgumentType
{
    Help,
    PrintFPS,
    PrintLatency,
    Timeout,
    Config,
    Profile,
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
    ("y,hef-file", "Path to YOLO HEF file", 
        cxxopts::value<std::string>()->default_value(YOLO_HEF_FILE))
    ("n,nms-score-threshold", "NMS score threshold",
        cxxopts::value<float>()->default_value(std::to_string(DEFAULT_NMS_SCORE_THRESHOLD)))
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
        std::cout << "subscribing to frontend for '" << s.id << "'" << std::endl;
        if (s.id == AI_ANALYTICS_SINK)
        {
            // For AI analytics stream, subscribe to segmentation stage
            app_resources->frontend->subscribe_to_stream(
                s.id, std::static_pointer_cast<ConnectedStage>(
                          app_resources->pipeline->get_stage_by_name(SEGMENTATION_AI_STAGE)));
        }
        else
        {
            // Subscribe encoder to frontend
            app_resources->frontend->subscribe_to_stream(s.id, app_resources->encoders[s.id]);
        }
    }
}

/**
 * @brief Create and configure an AI analytics stage.
 *
 * This function sets up an AI analytics stage for the application pipeline.
 * It reads configuration files and initializes the AI analytics stage accordingly.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
void create_ai_analytics_pipeline(std::shared_ptr<AppResources> app_resources, uint32_t ai_width, uint32_t ai_height,
                                  const std::string &hef_file_path, float nms_score_threshold)
{
    // AI Analytics Pipeline Stages
    std::shared_ptr<HailortAsyncStage> segmentation_stage =
        HailortAsyncStageBuild::create()
            .set_stage_name(SEGMENTATION_AI_STAGE)
            .set_hef_path(hef_file_path)
            .set_queue_size(5)
            .set_output_pool_size(50)
            .set_group_id("device0")
            .set_batch_size(5)
            .set_job_limit(10)
            .set_scheduler_threshold_opt(5)
            .set_dynamic_threshold_opt(true)
            .set_nms_score_threshold(nms_score_threshold)
            .set_scheduler_timeout_opt(std::chrono::milliseconds(100))
            .set_printfps_opt(app_resources->print_fps)
            .set_pool_mode_opt(StagePoolMode::LEAKY)
            .buildptr();

    std::shared_ptr<PostprocessStage> postprocess_stage = PostprocessStageBuild::create()
                                                              .set_stage_name(POST_STAGE)
                                                              .set_so_path(YOLO_POST_SO)
                                                              .set_function_name_opt(YOLO_FUNC_NAME)
                                                              .set_queue_size_opt(5)
                                                              .set_leaky_opt(true)
                                                              .set_printfps_opt(app_resources->print_fps)
                                                              .buildptr();

    std::shared_ptr<AnalyticsDBStage> analytics_db_stage = AnalyticsDBStageBuild::create()
                                                               .set_stage_name(ANALYTICS_DB_STAGE)
                                                               .set_media_library(app_resources->media_library)
                                                               .set_queue_size(5)
                                                               .set_leaky_opt(true)
                                                               .set_printfps_opt(app_resources->print_fps)
                                                               .set_analytics_data_id(ANALYTICS_DATA_ID)
                                                               .buildptr();

    // Add stages to pipeline
    app_resources->pipeline->add_stage(segmentation_stage);
    app_resources->pipeline->add_stage(postprocess_stage);
    app_resources->pipeline->add_stage(analytics_db_stage);

    // Subscribe stages to each other
    segmentation_stage->add_subscriber(postprocess_stage);
    postprocess_stage->add_subscriber(analytics_db_stage);
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

    // Create and configure UDP
    std::string udp_name = "udp_" + id;
    std::cout << "Creating udp " << udp_name << std::endl;
    std::shared_ptr<UdpStage> udp_stage = UdpStageBuild::create().set_stage_name(udp_name).buildptr();
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

    // Subscribe UDP to encoder
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
void configure_frontend_and_encoders(std::shared_ptr<AppResources> app_resources, const std::string &hef_file,
                                     float nms_score_threshold)
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
        if (s.id == AI_ANALYTICS_SINK)
        {
            create_ai_analytics_pipeline(app_resources, s.width, s.height, hef_file, nms_score_threshold);
        }
        else
        {
            create_encoder_and_udp(s.id, app_resources);
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
        app_resources->medialib_config_path = MEDIALIB_CONFIG_PATH;

        // register signal SIGINT and signal handler
        signal_utils::register_signal_handler([app_resources](int signal) {
            std::cout << "Stopping Pipeline..." << std::endl;
            REFERENCE_CAMERA_LOG_INFO("Stopping Pipeline...");
            // Stop pipeline
            app_resources->pipeline->stop_pipeline();
            if (app_resources->media_library != nullptr && app_resources->media_library->get_pipeline_state() ==
                                                               media_library_pipeline_state_t::PIPELINE_STATE_RUNNING)
            {
                app_resources->media_library->stop_pipeline();
            }
            app_resources->clear();
            // terminate program
            exit(0);
        });

        // Parse user arguments
        cxxopts::Options options = build_arg_parser();
        auto result = options.parse(argc, argv);
        std::vector<ArgumentType> argument_handling_results = handle_arguments(result, options);
        int timeout = result["timeout"].as<int>();
        std::string hef_file = result["hef-file"].as<std::string>();
        float nms_score_threshold = result["nms-score-threshold"].as<float>();

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
            case ArgumentType::HostIP:
                app_resources->host_ip = result["host-ip"].as<std::string>();
                break;
            case ArgumentType::Error:
                return 1;
            }
        }

        setenv("HAILORT_YOLOV5_SEG_PP_CROP_OPT", "1", 1);

        // Create pipeline
        app_resources->pipeline = std::make_shared<Pipeline>();

        // Configure frontend and encoders
        configure_frontend_and_encoders(app_resources, hef_file, nms_score_threshold);

        // Subscribe stages to frontend
        subscribe_to_frontend(app_resources);

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
