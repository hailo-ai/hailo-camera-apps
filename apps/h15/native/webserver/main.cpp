#include <nlohmann/json.hpp>
#include "pipeline/pipeline.hpp"
#include "resources/common/resources.hpp"
#include "resources/common/repository.hpp"
#include "common/common.hpp"
#include "common/httplib/httplib_utils.hpp"
#include <chrono>
#include <thread>
#include <signal.h>
#include "media_library/signal_utils.hpp"
#include "common/logger_macros.hpp"

int main(int argc, char *argv[])
{
    WEBSERVER_LOG_INFO("Starting webserver");

    std::shared_ptr<HTTPServer> svr = HTTPServer::create();
    WebServerPipeline pipeline = webserver::pipeline::IPipeline::create(svr);

    // register error handler
    svr->set_exception_handler([](const auto& req, auto& res, std::exception_ptr ep) {//TODO change that to a better error handler
        auto fmt = "<h1>Error 500</h1><p>%s</p>";
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
