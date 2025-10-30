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
#include "media_library/signal_utils.hpp"
#include "reference_camera_logger.hpp"
#include "scenarios/generate_pipeline.hpp"
#include "scenarios/parse_and_update_json.hpp"
#include "scenarios/recovery_scenarios.hpp"
#include "scenarios/robustness_scenarios.hpp"


enum class ArgumentType
{
    Help,
    Timeout,
    Config,
    Profile,
    Scenario,
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
    ("c,config-file-path", "Media library configuration path", 
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH))
    ("a,profile", "Profile name", 
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("s,scenario", "Choose testing scenario(s) (comma separated)", 
        cxxopts::value<std::vector<std::string>>()->default_value("None"))
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

    if (result.count("timeout"))
    {
        arguments.push_back(ArgumentType::Timeout);
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

    // Handle scenario argument: fetch vector of strings and map to TestScenarios
    if (result.count("scenario"))
    {
        try
        {
            auto scenario_strings = result["scenario"].as<std::vector<std::string>>();
            for (const auto& s : scenario_strings)
            {
                // This will throw if the scenario string is invalid
                scenario_from_string(s);
            }
            arguments.push_back(ArgumentType::Scenario);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
            return {ArgumentType::Error};
        }
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
std::mutex g_stop_mutex;
std::condition_variable g_stop_cv;
std::atomic<bool> g_stop_requested{false};

int main(int argc, char *argv[])
{
    uint loop_number = 0;
    // App resources
    std::shared_ptr<AppResources> app_resources = std::make_shared<AppResources>();
    app_resources->medialib_config_path = MEDIALIB_CONFIG_PATH;

    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler([](int signal) {
        std::cout << "Stopping Pipeline..." << std::endl;
        REFERENCE_CAMERA_LOG_INFO("Stopping Pipeline...");
        g_stop_requested.store(true);
        g_stop_cv.notify_all();
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
        case ArgumentType::Config:
            app_resources->medialib_config_path = result["config-file-path"].as<std::string>();
            break;
        case ArgumentType::Profile:
            app_resources->profile_name = result["profile"].as<std::string>();
            break;
        case ArgumentType::HostIP:
            app_resources->host_ip = result["host-ip"].as<std::string>();
            break;
        case ArgumentType::Scenario:
        {
            // Fetch scenario strings and convert to TestScenarios enum
            auto scenario_strings = result["scenario"].as<std::vector<std::string>>();
            std::vector<TestScenarios> scenarios;
            for (const auto& s : scenario_strings)
            {
                scenarios.push_back(scenario_from_string(s));
            }
            // Store the scenarios in app_resources (add a member if needed)
            // Example: app_resources->test_scenarios = scenarios;
            // If you only support one scenario, you can do:
            if (!scenarios.empty())
                app_resources->test_scenario = scenarios.front();
            break;
        }
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

    if (app_resources->test_scenario == TestScenarios::None)
    {
        REFERENCE_CAMERA_LOG_INFO("Started playing for {} seconds.", timeout);
        // Wait for either timeout or signal
        std::unique_lock<std::mutex> lk(g_stop_mutex);
        g_stop_cv.wait_for(lk, std::chrono::seconds(timeout));
    }
    else if (app_resources->test_scenario == TestScenarios::TotalRestart)
    {
        REFERENCE_CAMERA_LOG_INFO("Restarting pipeline every {} seconds.", timeout);
        while (true)
        {
            std::cout << "Now running scenario: TotalRestart, loop " << loop_number << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(timeout));
            restart_from_scratch(app_resources);
            loop_number++;
            if (g_stop_requested)
            {
                std::this_thread::sleep_for(std::chrono::seconds(timeout));
                break;
            }
        }
    }
    else if (app_resources->test_scenario == TestScenarios::Segfault)
    {
        std::cout << "Now running scenario: Segfault" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(timeout));
        cause_segfault();
    }
    else if (app_resources->test_scenario == TestScenarios::EncoderStop)
    {
        std::cout << "Now running scenario: EncoderStop" << std::endl;
        REFERENCE_CAMERA_LOG_INFO("Stopping/Starting encoders every {} seconds.", timeout);
        while (true)
        {
            std::cout << "Stopping/Starting encoders, at loop " << loop_number << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(timeout));
            stop_encoders(app_resources);
            std::this_thread::sleep_for(std::chrono::seconds(10));
            start_encoders(app_resources);
            loop_number++;
            if (g_stop_requested)
            {
                std::this_thread::sleep_for(std::chrono::seconds(timeout));
                break;
            }
        }
    }
    else if (app_resources->test_scenario == TestScenarios::ReconfigureFrontend)
    {
        while (true)
        {
            std::cout << "Now running scenario: ReconfigureFrontend, loop number " << loop_number << std::endl;
            configure_sensor_frontend_randomly(app_resources, timeout, g_stop_requested);
            loop_number++;
            if (g_stop_requested)
            {
                break;
            }
        }
    }
    else if (app_resources->test_scenario == TestScenarios::ReconfigureEncoder)
    {
        REFERENCE_CAMERA_LOG_INFO("Reconfiguring encoders every {} seconds.", timeout);
        while (true)
        {
            std::cout << "Now running scenario: ReconfigureEncoder, loop number " << loop_number << std::endl;
            configure_encoder_randomly(app_resources, timeout, g_stop_requested);
            loop_number++;
            if (g_stop_requested)
            {
                break;
            }
        }
    }
    else if (app_resources->test_scenario == TestScenarios::SwithcProfilesRandom)
    {
        while (true)
        {
            std::cout << "Now running scenario: SwithcProfilesRandom, loop number " << loop_number << std::endl;
            switch_profiles_randomly(app_resources, timeout, g_stop_requested);
            if (g_stop_requested)
            {
                break;
            }
        }
    }
    else
    {
        std::cerr << "Scenario not implemented yet" << std::endl;
    }
    

    // Stop pipeline
    std::cout << "Stopping." << std::endl;
    REFERENCE_CAMERA_LOG_INFO("Stopping.");
    app_resources->media_library->stop_pipeline();
    app_resources->pipeline->stop_pipeline();
    return 0;
}
