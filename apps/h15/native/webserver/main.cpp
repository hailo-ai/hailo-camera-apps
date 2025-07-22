#include <chrono>
#include <thread>
#include <signal.h>
#include <nlohmann/json.hpp>
#include <cxxopts/cxxopts.hpp>
#include "pipeline/pipeline.hpp"
#include "common/httplib/httplib_utils.hpp"
#include "common/logger_macros.hpp"
#include "common/common.hpp"
#include "media_library/signal_utils.hpp"

void flags_init(int argc, char *argv[], std::string &medialib_config_path)
{
    try
    {
        cxxopts::Options options(argv[0], "Webserver application");
        options.add_options()("config", "Media library configuration path",
                              cxxopts::value<std::string>())("h,help", "Print usage");

        auto result = options.parse(argc, argv);

        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            exit(0);
        }

        if (result.count("config"))
        {
            std::string config_path = result["config"].as<std::string>();
            WEBSERVER_LOG_INFO("Using medialib config path: {}", config_path);
            medialib_config_path = config_path;
        }
    }
    catch (const cxxopts::OptionException &e)
    {
        WEBSERVER_LOG_ERROR("Error parsing options: {}", e.what());
        std::cout << "Error parsing options: " << e.what() << std::endl;
        std::cout << "Use --help to see valid options" << std::endl;
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    WEBSERVER_LOG_INFO("Starting webserver");

    std::string medialib_config_path = "";
    Architecture arch = get_hailo_architecture();
    flags_init(argc, argv, medialib_config_path);

    std::shared_ptr<HTTPServer> svr = HTTPServer::create();
    // register error handler
    svr->set_exception_handler([](const auto &req, auto &res, std::exception_ptr ep) {
        auto fmt = "Error 500: %s";
        char buf[BUFSIZ];
        try
        {
            std::rethrow_exception(ep);
        }
        catch (std::exception &e)
        {
            snprintf(buf, sizeof(buf), fmt, e.what());
            WEBSERVER_LOG_ERROR("{}", buf);
        }
        catch (...)
        { // See the following NOTE
            snprintf(buf, sizeof(buf), fmt, "Unknown Exception");
            WEBSERVER_LOG_ERROR("Unknown Excpetion");
        }
        res.set_content(buf, "text/html");
        res.status = 500;
    });

    WebServerPipeline pipeline;
    pipeline = webserver::pipeline::CppPipeline::create(svr, medialib_config_path, arch);

    signal_utils::register_signal_handler([pipeline](int signal) {
        WEBSERVER_LOG_INFO("Received signal {} exiting", signal);
        pipeline->stop();
        exit(0);
    });

    pipeline->start();

    WEBSERVER_LOG_INFO("Webserver started");
    svr->listen("0.0.0.0", 80);
}
