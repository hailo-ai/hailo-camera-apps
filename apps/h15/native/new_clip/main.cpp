
// general includes
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include "media_library/signal_utils.hpp"
#include "utils/clip_app_config_parser.hpp"
#include "apps/webserver.hpp"
#include "utils/common_utils.hpp"

constexpr const char* CLIP_APP_CONFIG_FILE = "/home/root/apps/new_clip/resources/configs/clip_app_config.yaml";
constexpr const char* CLIP_STORAGE_MEMORY_MOUNT_POINT = "/var/volatile";
constexpr double SAVE_TO_MEM_MIN_GB = 3.0;

std::function<void(int)> g_signal_callback;

void signal_handler_func(int signal)
{
    if (g_signal_callback)
    {
        g_signal_callback(signal);
    }
}

int main()
{

    ClipAppConfigParser config_parser;
    if (!config_parser.parse_from_file(CLIP_APP_CONFIG_FILE))
    {
        std::cerr << "Unable to load " << CLIP_APP_CONFIG_FILE << std::endl;
        return -1;
    }

    ClipAppConfig config = config_parser.get_config();

    // Check system memory requirement if saving clip data to memory
    if (config.storage_config.mount_location.find(CLIP_STORAGE_MEMORY_MOUNT_POINT) != std::string::npos)
    {
        if (SystemUtils::getTotalMemoryGB() < SAVE_TO_MEM_MIN_GB)
        {
            std::cout << "Warning: Your system memory is less than 3GB. "
                         "The application by default saved clip data to memory and require at least 3GB, "
                         "You can still use clip app on this system but you will need to change the clip data "
                         "storage path to SD card, SD card minimum requirement is A2 class, for instruction "
                         "please follow README.rst from this app" << std::endl;
            return -1;
        }
    }

    // register signal SIGINT and signal handler
    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler(signal_handler_func);

    // Start Application Server
    auto server_result = IntegratedWebServer::create(config);
    if (!server_result)
    {
        std::cerr << "Failed to instantiate IntegratedWebServer" << std::endl;
        return -1;
    }

    std::shared_ptr<IntegratedWebServer> server = server_result.value();

    g_signal_callback = [&server](int sig) {
        std::cout << "Stopping Pipeline..." << std::endl;
        // Stop Application
        server->stop();
        // terminate program
        exit(0);
    };

    std::cout << "Application starting..." << std::endl;

    std::thread server_thread([&server, &config]() { server->start(config.server_info.host, config.server_info.port); });

    std::string command;
    while (true)
    {
        std::cout << "Enter command (quit): ";
        std::cin >> command;

        if (command == "quit")
        {
            break;
        }
    }

    std::cout << "Stopping Application..." << std::endl;
    server->stop();

    if (server_thread.joinable())
    {
        server_thread.join();
    }

    return 0;
}
