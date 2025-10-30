#pragma once
#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <tl/expected.hpp>
#include <signal.h>
#include <cxxopts/cxxopts.hpp>
#include <signal.h>
#include <condition_variable>
#include <mutex>
#include "media_library/media_library.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/signal_utils.hpp"
#include "pipeline.hpp"
#include "udp_stage.hpp"
#include "encoder_stage.hpp"
#include "frontend_stage.hpp"
#include "reference_camera_logger.hpp"
#include "pipeline_builder.hpp"


#define FRONTEND_STAGE "frontend_stage"
// Macro that turns coverts stream ids to port #s
#define PORT_FROM_ID(id) std::to_string(5000 + std::stoi(id.substr(4)) * 2)
#define HOST_IP "10.0.0.2"
// Frontend Params
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/case_studies/single_stream_medialib_config.json"
#define NO_PROFILE_SELECTED ""


enum class TestScenarios
{
    TotalRestart,
    EncoderStop,
    Segfault,
    ReconfigureFrontend,
    ReconfigureEncoder,
    SwithcProfilesRandom,
    None
};

TestScenarios scenario_from_string(const std::string& str) {
    if (str == "TotalRestart") return TestScenarios::TotalRestart;
    if (str == "EncoderStop") return TestScenarios::EncoderStop;
    if (str == "Segfault") return TestScenarios::Segfault;
    if (str == "ReconfigureFrontend") return TestScenarios::ReconfigureFrontend;
    if (str == "ReconfigureEncoder") return TestScenarios::ReconfigureEncoder;
    if (str == "SwithcProfilesRandom") return TestScenarios::SwithcProfilesRandom;
    if (str == "None") return TestScenarios::None;
    throw std::invalid_argument("Unknown scenario: " + str);
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
    std::string medialib_config_path;
    std::string profile_name;
    std::string host_ip = HOST_IP;
    TestScenarios test_scenario = TestScenarios::None;

    void clear()
    {
        frontend = nullptr;
        pipeline = nullptr;
        encoders.clear();
        udp_outputs.clear();
        medialib_config_path = "";
        media_library = nullptr;
        profile_name = NO_PROFILE_SELECTED;
        host_ip = HOST_IP;
        test_scenario = TestScenarios::None;
    }

    ~AppResources()
    {
        clear();
    }
};

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
    
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        throw std::runtime_error("Failed to create media library");
    }
    app_resources->media_library = media_lib_expected.value();
    if (app_resources->media_library->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        throw std::runtime_error("Failed to initialize media library");
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
        PipelineBuilder pip_builder;

        pip_builder.add_stage(app_resources->frontend, StageType::SOURCE);

        // Add encoder and udp to stage (except AI_SINK)
        for (auto s : streams.value())
        {
            // Add encoder/udp to pipeline stage
            pip_builder.add_stage(app_resources->encoders[s.id], StageType::SINK)
                .add_stage(app_resources->udp_outputs[s.id], StageType::SINK);
        }

        /*
            Subscribe stages of the pipeline to each other
        */

        // Connect encoders to frontend
        for (auto s : streams.value())
        {
            pip_builder.connect_frontend(FRONTEND_STAGE, s.id, app_resources->encoders[s.id]->get_name());
        }

        // Connect UDP to encoders
        for (auto s : streams.value())
        {
            pip_builder.connect(app_resources->encoders[s.id]->get_name(),
                                app_resources->udp_outputs[s.id]->get_name());
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
