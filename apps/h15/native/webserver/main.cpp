#include <chrono>
#include <thread>
#include <signal.h>
#include <nlohmann/json.hpp>
#include <cxxopts/cxxopts.hpp>
#include "pipeline/gst_pipeline.hpp"
#include "pipeline/cpp_pipeline.hpp"
#include "common/httplib/httplib_utils.hpp"
#include "common/logger_macros.hpp"
#include "common/common.hpp"
#include "media_library/signal_utils.hpp"

void flags_init(int argc, char *argv[], std::string &medialib_config_path)
{
  try {
    cxxopts::Options options(argv[0], "Webserver application");
    options.add_options()
      ("c,use-cpp-api", "Use C++ API instead of default GStreamer API", cxxopts::value<bool>()->default_value("false"))
      ("config", "Media library configuration path", cxxopts::value<std::string>())
      ("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
      std::cout << options.help() << std::endl;
      exit(0);
    }

    global_api_type = ApiType::GSTREAMER;
    if (result["use-cpp-api"].as<bool>()) {
      global_api_type = ApiType::CPP;
    }
    else if (!result["use-cpp-api"].as<bool>() && result.count("config")) {
        WEBSERVER_LOG_ERROR("Media library config path is required only when using gstreamer api");
        std::cout << "Media library config path is required only when using gstreamer api" << std::endl;
        exit(1);
    }

    if (result.count("config")) {
      std::string config_path = result["config"].as<std::string>();
      WEBSERVER_LOG_INFO("Using medialib config path: {}", config_path);
      medialib_config_path = config_path;
    }

    WEBSERVER_LOG_INFO("Using {} api", use_gstreamer_api() ? "gstreamer" : "cpp");
  }
  catch (const cxxopts::OptionException& e) {
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
    flags_init(argc, argv, medialib_config_path);

    std::shared_ptr<HTTPServer> svr = HTTPServer::create();
    WebServerPipeline pipeline;
    if (use_gstreamer_api()) {
        pipeline = webserver::pipeline::GStreamerPipeline::create(svr);
    }
    else {
        pipeline = webserver::pipeline::CppPipeline::create(svr, medialib_config_path);
    }
    // register error handler
    svr->set_exception_handler([](const auto& req, auto& res, std::exception_ptr ep) {
        auto fmt = "Error 500: %s";
        char buf[BUFSIZ];
        try {
          std::rethrow_exception(ep);
        } catch (std::exception &e) {
          snprintf(buf, sizeof(buf), fmt, e.what());
          WEBSERVER_LOG_ERROR("{}", buf);
        } catch (...) { // See the following NOTE
          snprintf(buf, sizeof(buf), fmt, "Unknown Exception");
          WEBSERVER_LOG_ERROR("Unknown Excpetion");
        }
        res.set_content(buf, "text/html");
        res.status = 500;
    });
    

    signal_utils::register_signal_handler([pipeline](int signal)
                                          {
                                            WEBSERVER_LOG_INFO("Received signal {} exiting", signal);
                                            pipeline->stop();
                                            exit(0); });

    pipeline->start();

    // Part of the logic involels sleeping for a second and then inspecting ISP parameters
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto isp_resource = std::static_pointer_cast<webserver::resources::IspResource>(pipeline->get_resources()->get(RESOURCE_ISP));
    isp_resource->init();

    WEBSERVER_LOG_INFO("Webserver started");
    svr->listen("0.0.0.0", 80);
}
