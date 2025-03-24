#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <iostream>
#include <fstream>
#include <regex>
#include <cxxopts/cxxopts.hpp>
#include "apps_common.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>

const char *BASIC_VISION_CONFIG = "/home/root/apps/basic_security_camera_streaming/resources/configs/vision_config1.json";
const char *ENCODER_CONFIG_FILE = "/home/root/apps/internals/validation_apps_configs/resources/configs/encoder_config.json";
static uint counter = 0;
int num_outputs = 0;
int frame_to_change = 1;
bool config_file_path = false;
std::string vision_config = "";

struct Resolution {
    int width;
    int height;
    int framerate;
};

std::vector<Resolution> output_resolutions;
struct ProbeData {
    GstElement *pipeline;
    std::vector<std::string> encoder_configs;
    std::vector<std::string> encoder_file_paths;
};

std::string read_string_from_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (file.is_open()) {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        return content;
    } else {
        std::cerr << "Error: Unable to open file: " << file_path << std::endl;
        return "";
    }
}

void write_string_to_file(const std::string &file_path, const std::string &content) {
    std::ofstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + file_path);
    }
    file << content;
}


void read_and_update_encoder_configs(std::vector<std::string>& temp_file_paths, std::vector<std::string>& encoder_configs_strings)
{
    for (int i = 0; i < num_outputs; ++i) {
        std::string content = read_string_from_file(ENCODER_CONFIG_FILE);
        nlohmann::ordered_json encoder_config_json = nlohmann::ordered_json::parse(content);
        encoder_config_json["encoding"]["input_stream"]["width"] = output_resolutions[i].width;
        encoder_config_json["encoding"]["input_stream"]["height"] = output_resolutions[i].height;
        encoder_config_json["encoding"]["input_stream"]["framerate"] = output_resolutions[i].framerate;
        write_string_to_file(temp_file_paths[i], encoder_config_json.dump(4));
        encoder_configs_strings[i] = read_string_from_file(temp_file_paths[i]);
    }
}

static GstPadProbeReturn change_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    ProbeData *data = static_cast<ProbeData*>(user_data);
    GstElement *pipeline = data->pipeline;
    std::vector<std::string> &encoder_configs = data->encoder_configs;
    std::vector<std::string> encoder_file_paths = data->encoder_file_paths;

    counter++;
    GstElement *frontend_element = gst_bin_get_by_name(GST_BIN(pipeline), "frontend");

    std::vector<GstElement*> osd_gst_element(num_outputs);
    for (int i = 0; i < num_outputs; ++i)
        osd_gst_element[i] = gst_bin_get_by_name(GST_BIN(pipeline), ("osd" + std::to_string(i)).c_str());

    if ((counter % frame_to_change) == 0) {
        if (frontend_element) {
            vision_config = read_string_from_file(BASIC_VISION_CONFIG);
            GST_INFO("Setting config-string for frontend_element");
            if (config_file_path)
                g_object_set(frontend_element, "config-file-path", BASIC_VISION_CONFIG, NULL);
            else
                g_object_set(frontend_element, "config-string", vision_config.c_str(), NULL);

            read_and_update_encoder_configs(encoder_file_paths, encoder_configs);
            for (int i = 0; i < num_outputs; ++i) {
                if (osd_gst_element[i]) {
                    if (config_file_path) {
                        GST_INFO("Setting config-file-path for osd_gst_element[%d]", i);
                        g_object_set(osd_gst_element[i], "config-file-path", encoder_file_paths[i].c_str(), NULL);
                    } else {
                        GST_INFO("Setting config-string for osd_gst_element[%d]", i);
                        g_object_set(osd_gst_element[i], "config-string", encoder_configs[i].c_str(), NULL);
                    }
                } else
                    GST_ERROR("osd_gst_element[%d] is NULL, cannot set config-string", i);
            }
        } else 
            GST_ERROR("frontend_element is NULL, cannot set config-string");
        for (int i = 0; i < num_outputs; ++i) 
        {
            if (osd_gst_element[i]) {
                gst_object_unref(osd_gst_element[i]);
            }
        }
    }
    gst_object_unref(frontend_element);
    return GST_PAD_PROBE_OK;
}

/**
 * Appsink's new_sample callback
 *
 * @param[in] appsink               The appsink object.
 * @param[in] callback_data         user data.
 * @return GST_FLOW_OK
 * @note Example only - only mapping the buffer to a GstMapInfo, than unmapping.
 */
static GstFlowReturn appsink_new_sample(GstAppSink *appsink, gpointer callback_data)
{
    GstSample *sample;
    GstBuffer *buffer;
    GstMapInfo mapinfo;

    sample = gst_app_sink_pull_sample(appsink);
    buffer = gst_sample_get_buffer(sample);
    gst_buffer_map(buffer, &mapinfo, GST_MAP_READ);

    GST_INFO_OBJECT(appsink, "Got Buffer from appsink: %p", mapinfo.data);
    // Do Logic

    gst_buffer_unmap(buffer, &mapinfo);
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

/**
 * Create the gstreamer pipeline as string
 *
 * @return A string containing the gstreamer pipeline.
 * @note prints the return value to the stdout.
 */
std::string create_dynamic_pipeline_string_config_string(std::string codec, std::string vision_config_string, 
                                           const std::vector<std::string>& encoder_config_strings,
                                           int num_outputs)
{
    std::string pipeline = "hailofrontendbinsrc config-string='" + vision_config_string + "' name=frontend ";
    for (int i = 0; i < num_outputs; ++i) {
        pipeline += "frontend. ! "
                    "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                    "hailoencodebin config-string='" + encoder_config_strings[i] + "' name=osd" + std::to_string(i) + " ! video/x-h264 ! "
                    "tee name=stream" + std::to_string(i) + "_tee "
                    "stream" + std::to_string(i) + "_tee. ! "
                    "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                    "rtph264pay ! application/x-rtp, media=(string)video, encoding-name=(string)H264 ! "
                    "udpsink host=10.0.0.2 sync=false port=" + std::to_string(5000 + i * 2) + " "
                    "stream" + std::to_string(i) + "_tee. ! "
                    "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                    "fpsdisplaysink fps-update-interval=2000 name=display_sink" + std::to_string(i + 1) + " text-overlay=false "
                    "video-sink=\"appsink max-buffers=1 name=hailo_sink" + std::to_string(i + 1) + "\" sync=true signal-fps-measurements=true ";
    }
    return pipeline;
}

std::string create_dynamic_pipeline_string_config_file_path(std::string codec,
                                           const std::vector<std::string>& encoder_config_paths,
                                           int num_outputs)
{
    std::string pipeline = "hailofrontendbinsrc config-file-path=" + std::string(BASIC_VISION_CONFIG) + " name=frontend ";
    for (int i = 0; i < num_outputs; ++i) {
        pipeline += "frontend. ! "
                    "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                    "hailoencodebin config-file-path=" + encoder_config_paths[i] + " name=osd" + std::to_string(i) + " ! video/x-h264 ! "
                    "tee name=stream" + std::to_string(i) + "_tee "
                    "stream" + std::to_string(i) + "_tee. ! "
                    "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                    "rtph264pay ! application/x-rtp, media=(string)video, encoding-name=(string)H264 ! "
                    "udpsink host=10.0.0.2 sync=false port=" + std::to_string(5000 + i * 2) + " "
                    "stream" + std::to_string(i) + "_tee. ! "
                    "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
                    "fpsdisplaysink fps-update-interval=2000 name=display_sink" + std::to_string(i + 1) + " text-overlay=false "
                    "video-sink=\"appsink max-buffers=1 name=hailo_sink" + std::to_string(i + 1) + "\" sync=true signal-fps-measurements=true ";
    }
    std::cout << "Pipeline: " << pipeline << std::endl;
    return pipeline;
}

/**
 * Set the callbacks
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @param[in] print_fps       To print FPS or not.
 * @note Sets the new_sample and propose_allocation callbacks, without callback user data (NULL).
 */
void set_callbacks(GstElement *pipeline, int num_outputs) {
    GstAppSinkCallbacks callbacks = {NULL};
    callbacks.new_sample = appsink_new_sample;
    
    for (int i = 1; i <= num_outputs; ++i) {
        std::string sink_name = "hailo_sink" + std::to_string(i);
        std::cout << "Setting callbacks for sink: " << sink_name << std::endl;
        GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), sink_name.c_str());
        gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, NULL, NULL);
        gst_object_unref(appsink);
    }
}

/**
 * Set the pipeline's probes
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @note Sets a probe to the encoder sinkpad.
 */
void set_probes(GstElement *pipeline, std::vector<std::string> encoder_configs_strings,
                std::vector<std::string> encoder_file_paths , bool print_fps) 
{
    // extract elements from pipeline
    GstElement *frontendbinsrc = gst_bin_get_by_name(GST_BIN(pipeline), "frontend");
    GstElement *hailofrontend = gst_bin_get_by_name(GST_BIN(frontendbinsrc), "hailofrontendelement");
    GstPad *pad_frontend = gst_element_get_static_pad(hailofrontend, "sink");

    // Create ProbeData struct
    ProbeData *data = new ProbeData{pipeline, encoder_configs_strings, encoder_file_paths};

    std::cout << "Setting change probe" << std::endl;
    gst_pad_add_probe(pad_frontend, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)change_probe_callback, data, [](gpointer data) { 
        std::cout << "Deleting change probe data" << std::endl;
        delete static_cast<ProbeData*>(data); 
    }); 
    
    if (print_fps) {
        for (int i = 1; i <= num_outputs; ++i) {
            std::string display_sink_name = "display_sink" + std::to_string(i);
            GstElement *display_sink = gst_bin_get_by_name(GST_BIN(pipeline), display_sink_name.c_str());
            g_signal_connect(display_sink, "fps-measurements", G_CALLBACK(fps_measurements_callback), NULL);
            gst_object_unref(display_sink);
        }
    }
    gst_object_unref(frontendbinsrc);
    gst_object_unref(hailofrontend);
}

std::vector<std::string> create_temp_paths(int num_outputs) {
    std::vector<std::string> temp_file_paths;
    for (int i = 0; i < num_outputs; ++i) {
        std::string temp_file_path = "/tmp/encoder_config_" + std::to_string(i) + ".json";
        temp_file_paths.push_back(temp_file_path);
    }
    return temp_file_paths;
}

void delete_temp_files(const std::vector<std::string> &temp_file_paths) {
    for (const auto &file_path : temp_file_paths) {
        std::remove(file_path.c_str());
    }
}

int main(int argc, char *argv[])
{
    std::string src_pipeline_string;
    GstFlowReturn ret;
    add_sigint_handler();
    std::string codec = "h264";
    bool print_fps = false;

    // Parse user arguments
    cxxopts::Options options = build_arg_parser();
    options.add_options()
        ("frames", "Number of frames between every change", cxxopts::value<int>()->default_value("90"));
    options.add_options()
        ("config-file-path", "When true using config file path in gst pipeline, when false using config string in gst pipeline (default: false)", cxxopts::value<bool>()->default_value("false"));    
    auto result = options.parse(argc, argv);
    frame_to_change = result["frames"].as<int>();
    config_file_path = result["config-file-path"].as<bool>();

    std::vector<ArgumentType> argument_handling_results = handle_arguments(result, options, codec);

    for (ArgumentType argument : argument_handling_results)
    {
        switch (argument)
        {
        case ArgumentType::Help:
            return 0;
        case ArgumentType::Codec:
            break;
        case ArgumentType::PrintFPS:
            print_fps = true;
            break;
        case ArgumentType::Error:
            return 1;
        }
    }

    gst_init(&argc, &argv);
    vision_config = read_string_from_file(BASIC_VISION_CONFIG);
    nlohmann::json config_json = nlohmann::json::parse(vision_config);
    num_outputs = config_json["output_video"]["resolutions"].size();

    std::cout << "Number of output streams: " << num_outputs << std::endl;
    std::vector<std::string> encoder_configs_strings(num_outputs);

    bool rotationFlag = false;
    if (config_json["rotation"]["enabled"] == true)
        if ((config_json["rotation"]["angle"] == "ROTATION_ANGLE_270") || (config_json["rotation"]["angle"] == "ROTATION_ANGLE_90"))
            rotationFlag = true;

    // Populate output resolutions
    for (const auto& res : config_json["output_video"]["resolutions"]) {
        Resolution resolution;
        resolution.width = res["width"];
        resolution.height = res["height"];
        if (rotationFlag)
        {
            resolution.width = res["height"];
            resolution.height = res["width"];
        }
        resolution.framerate = res["framerate"];
        output_resolutions.push_back(resolution);
    }
    std::vector<std::string> temp_file_paths = create_temp_paths(num_outputs);
    read_and_update_encoder_configs(temp_file_paths, encoder_configs_strings);
    // Create dynamic pipeline string
    std::string pipeline_string;
    if (config_file_path)
        pipeline_string = create_dynamic_pipeline_string_config_file_path(codec, temp_file_paths, num_outputs);
    else
        pipeline_string = create_dynamic_pipeline_string_config_string(codec, vision_config, encoder_configs_strings, num_outputs);
    std::cout << "Created pipeline string." << std::endl;

    // Parse the pipeline
    GstElement *pipeline = gst_parse_launch(pipeline_string.c_str(), NULL);
    std::cout << "Parsed pipeline." << std::endl;

    // Set probes and callbacks
    std::cout << "Set probes and callbacks." << std::endl;
    set_callbacks(pipeline, num_outputs);
    set_probes(pipeline, encoder_configs_strings, temp_file_paths, print_fps);

    // Set pipeline state to playing
    std::cout << "Setting state to playing." << std::endl;
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // Wait for the end of the pipeline
    ret = wait_for_end_of_pipeline(pipeline);

    std::cout << "Pipeline ended." << std::endl;
    // Clean up
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    delete_temp_files(temp_file_paths);
    gst_deinit();

    return ret;
}
