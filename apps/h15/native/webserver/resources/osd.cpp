#include "osd_res.hpp"
#include <iostream>
#include <httplib.h>
#include <regex>

#define IMAGE_PATH "/home/root/apps/webserver/resources/configs/"
#define FONT_PATH "/usr/share/fonts/ttf/"
#define CATEGORIES {"image", "dateTime", "text"}
#define VALID_EXTENSIONS {".png", ".jpeg", ".jpg", ".bmp"}

using namespace webserver::resources;

OsdResource::OsdResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<ConfigResource> configs) : Resource(event_bus)
{
    auto default_config = configs->get_osd_and_encoder_default_config();
    m_default_config = default_config.dump(4);

    reset_config();

    subscribe_callback(STREAM_CONFIG, [this](ResourceStateChangeNotification notification)
                       {
                           WEBSERVER_LOG_INFO("Received STREAM_CONFIG notification");
                           auto state = std::static_pointer_cast<StreamConfigResourceState>(notification.resource_state);

                           if (state->resolutions[0].framerate_changed) // true only if only one field is changed at a time in state
                               return;

                           auto new_height = state->resolutions[0].height;
                           auto new_width = state->resolutions[0].width;

                           // Log new resolution
                           WEBSERVER_LOG_INFO("New resolution: {}x{}", new_width, new_height);

                           // Calculate new size for OSD
                           m_config = map_overlays(m_config, [this, new_width](nlohmann::json conf)
                                                                {
                                                                    return relative_font_size_to_absolut(
                                                                            absolut_font_size_to_relative(conf, m_resolution_conf.width),
                                                                            new_width);
                                                                 });

                           m_resolution_conf.height = new_height;
                           m_resolution_conf.width = new_width;
                       });
}

void OsdResource::reset_config()
{
    auto config = nlohmann::json::parse(m_default_config);
    m_config = from_medialib_config_to_osd_config(config["osd"]);
    m_resolution_conf.width = config["encoding"]["input_stream"]["width"];
    m_resolution_conf.height = config["encoding"]["input_stream"]["height"];
    WEBSERVER_LOG_INFO("OSD configuration reset to default");
}

nlohmann::json OsdResource::from_medialib_config_to_osd_config(nlohmann::json config)
{
    nlohmann::json osd_config;
    osd_config["global_enable"] = true;
    osd_config["image"]["global_enable"] = true;
    osd_config["dateTime"]["global_enable"] = true;
    osd_config["text"]["global_enable"] = true;
    osd_config["image"]["items"] = {};
    osd_config["dateTime"]["items"] = {};
    osd_config["text"]["items"] = {};
    for (auto entry = config.begin(); entry != config.end(); ++entry) {
        if (entry.key() == "image"){
            for (auto &entry : entry.value()){
                nlohmann::json item;
                item["type"] = "image";
                item["enabled"] = true;
                item["name"] = "Image";
                item["params"]["id"] = entry["id"];
                item["params"]["image_path"] = entry["image_path"];
                item["params"]["width"] = entry["width"];
                item["params"]["height"] = entry["height"];
                item["params"]["x"] = entry["x"];
                item["params"]["y"] = entry["y"];
                item["params"]["z-index"] = entry["z-index"];
                item["params"]["angle"] = entry["angle"];
                item["params"]["rotation_policy"] = entry["rotation_policy"];
                osd_config["image"]["items"].push_back(item);
            }
        }
        else if (entry.key() == "dateTime"){
            for (auto &entry : entry.value()){
                nlohmann::json item;
                item["type"] = "dateTime";
                item["enabled"] = true;
                item["name"] = "Date & Time";
                item["params"]["id"] = entry["id"];
                item["params"]["font_path"] = entry["font_path"];
                item["params"]["font_size"] = entry["font_size"];
                item["params"]["text_color"] = entry["rgb"];
                item["params"]["x"] = entry["x"];
                item["params"]["y"] = entry["y"];
                item["params"]["z-index"] = entry["z-index"];
                item["params"]["angle"] = entry["angle"];
                item["params"]["rotation_policy"] = entry["rotation_policy"];
                osd_config["dateTime"]["items"].push_back(item);
            }
        }
        else if (entry.key() == "text"){
            for (auto &entry : entry.value()){
                nlohmann::json item;
                item["type"] = "text";
                item["enabled"] = true;
                item["name"] = "Label";
                item["params"]["id"] = entry["id"];
                item["params"]["label"] = entry["label"];
                item["params"]["font_path"] = entry["font_path"];
                item["params"]["font_size"] = entry["font_size"];
                item["params"]["text_color"] = entry["rgb"];
                item["params"]["x"] = entry["x"];
                item["params"]["y"] = entry["y"];
                item["params"]["z-index"] = entry["z-index"];
                item["params"]["angle"] = entry["angle"];
                item["params"]["rotation_policy"] = entry["rotation_policy"];
                osd_config["text"]["items"].push_back(item);
            }
        }
        else{
            WEBSERVER_LOG_ERROR("Unknown overlay type: {}", entry.key());
        }
    }
    return osd_config;
}

OsdResource::OsdResourceState::OsdResourceState(nlohmann::json config, std::vector<std::string> overlays_ids) : overlays_to_delete(overlays_ids) {
    for (const char* category : CATEGORIES)
    {
        for (auto &entry : config[category]["items"])
        {
            auto j_config = entry["params"];
            if (j_config == nullptr)
                continue;

            if (static_cast<std::string>(category) == "text")
            {
                bool enabled = entry["enabled"] && config["global_enable"].get<bool>() && config[category]["global_enable"];
                text_overlays.push_back(OsdResourceConfig<osd::TextOverlay>(enabled, j_config));
            }
            else if (static_cast<std::string>(category) == "image")
            {
                bool enabled = entry["enabled"] && config["global_enable"].get<bool>() && config[category]["global_enable"];
                image_overlays.push_back(OsdResourceConfig<osd::ImageOverlay>(enabled, j_config));
            }
            else if (static_cast<std::string>(category) == "dateTime")
            {
                bool enabled = entry["enabled"] && config["global_enable"].get<bool>() && config[category]["global_enable"];
                datetime_overlays.push_back(OsdResourceConfig<osd::DateTimeOverlay>(enabled, j_config));
            }
            else
            {
                WEBSERVER_LOG_ERROR("Unknown overlay type: {}", entry["type"]);
            }
        }
    }
}

nlohmann::json OsdResource::map_overlays(nlohmann::json config, std::function<nlohmann::json(nlohmann::json)> transform_osd)
{
    for (const char *category : CATEGORIES)
    {
        if (!config.contains(category) || !config[category].contains("items"))
            continue;

        for (auto &entry : config[category]["items"])
        {
            entry = transform_osd(entry);
        }
    }
    return config;
}

nlohmann::json OsdResource::unmap_paths(nlohmann::json osd_entry)
{
    if (osd_entry["type"] == "image")
    {
        osd_entry["params"]["image_path"] = osd_entry["params"]["image_path"].get<std::string>().substr(std::string(IMAGE_PATH).size());
    }
    if ((osd_entry["type"] == "text" || osd_entry["type"] == "dateTime") && osd_entry["params"].contains("font_path"))
    {
        osd_entry["params"]["font_path"] = osd_entry["params"]["font_path"].get<std::string>().substr(std::string(FONT_PATH).size());
    }
    return osd_entry;
}

nlohmann::json OsdResource::map_paths(nlohmann::json osd_entry)
{
    if (osd_entry["type"] == "image")
    {
        osd_entry["params"]["image_path"] = std::string(IMAGE_PATH) + osd_entry["params"]["image_path"].get<std::string>();
    }
    if ((osd_entry["type"] == "text" || osd_entry["type"] == "dateTime") && osd_entry["params"].contains("font_path"))
    {
        osd_entry["params"]["font_path"] = std::string(FONT_PATH) + osd_entry["params"]["font_path"].get<std::string>();
    }
    return osd_entry;
}

nlohmann::json OsdResource::relative_font_size_to_absolut(nlohmann::json osd_entry, uint32_t width)
{
    if ((osd_entry["type"] == "text" || osd_entry["type"] == "dateTime") && osd_entry["params"].contains("font_size"))
    {
        osd_entry["params"]["font_size"] = relative_font_size_to_absolut(osd_entry["params"]["font_size"].get<float>(), width);
    }
    return osd_entry;
}

nlohmann::json OsdResource::absolut_font_size_to_relative(nlohmann::json osd_entry, uint32_t width)
{
    if ((osd_entry["type"] == "text" || osd_entry["type"] == "dateTime") && osd_entry["params"].contains("font_size"))
    {
        osd_entry["params"]["font_size"] = absolut_font_size_to_relative(osd_entry["params"]["font_size"].get<int>(), width);
    }
    return osd_entry;
}

int OsdResource::relative_font_size_to_absolut(float font_size, uint32_t width)
{
    if (font_size > 1 || font_size < 0)
        WEBSERVER_LOG_ERROR("Font size must be between 0 and 1");
    if (width == 0)
        width = m_resolution_conf.width;
    return static_cast<int>(static_cast<float>(font_size) * static_cast<float>(width));
}

float OsdResource::absolut_font_size_to_relative(int font_size, uint32_t width)
{
    if (width == 0)
        width = m_resolution_conf.width;
    return static_cast<float>(font_size) / static_cast<float>(m_resolution_conf.width);
}
nlohmann::json OsdResource::get_encoder_osd_config()
{
    nlohmann::json current_config(nlohmann::json::object());

    for (const char* category : CATEGORIES)
    {
        if (!m_config.contains(category) ||
            !m_config[category]["global_enable"].get<bool>() ||
            !m_config["global_enable"].get<bool>())
            continue;

        for (auto &entry : m_config[category]["items"])
        {
            if (!entry["enabled"])
                continue;

            auto j_config = entry["params"];
            if (j_config == nullptr)
                continue;
            current_config[category].push_back(j_config);
        }
    }

    return current_config;
}

std::vector<std::string> OsdResource::get_overlays_to_delete(nlohmann::json previous_config, nlohmann::json new_config)
{
    std::vector<std::string> overlays_to_delete;
    std::regex whole_object_path(R"(^\/\d+$)");
    for (const char* category : CATEGORIES)
    {
        if (!previous_config.contains(category) || !new_config.contains(category))
            continue;
        auto diff = nlohmann::json::diff(previous_config[category]["items"], new_config[category]["items"]);
        for (auto &entry : diff)
        {
            std::string path = entry["path"];
            if (entry["op"] == "remove" && std::regex_match(path, whole_object_path))
            {
                size_t index = std::stoi(path.substr(1));
                overlays_to_delete.push_back(previous_config[category]["items"][index]["params"]["id"]);
            }
        }
    }
    return overlays_to_delete;
}

void OsdResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/osd", std::function<nlohmann::json()>([this]()
                                                     { return map_overlays(m_config, [this](nlohmann::json entry)
                                                                           { return absolut_font_size_to_relative(unmap_paths(entry), m_resolution_conf.width); }); }));

    srv->Get("/osd/formats", std::function<nlohmann::json()>([this]() {
        std::vector<std::string> formats;

        for (const auto& entry : std::filesystem::directory_iterator(FONT_PATH)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ttf") {
                std::string format = entry.path().filename().string();
                formats.push_back(format);
            }
        }
        return formats;
    }));

    srv->Get("/osd/images", std::function<nlohmann::json()>([this]() {
        std::vector<std::string> images;

        for (const auto& entry : std::filesystem::directory_iterator(IMAGE_PATH)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::string extension = entry.path().extension();
            if (std::find(std::begin(VALID_EXTENSIONS), std::end(VALID_EXTENSIONS), extension) != std::end(VALID_EXTENSIONS)) {
                std::string image = entry.path().filename().string();
                images.push_back(image);
            }
        }
        return images;
    }));

    srv->Patch("/osd", [this](const nlohmann::json &partial_config)
               {
        nlohmann::json previouse_config = m_config;
        m_config.merge_patch(map_overlays(partial_config, [this](nlohmann::json entry) { return relative_font_size_to_absolut(map_paths(entry), m_resolution_conf.width); }));
        auto result = this->m_config;
        auto state = std::make_shared<OsdResource::OsdResourceState>(OsdResourceState(m_config, get_overlays_to_delete(previouse_config, m_config)));
        on_resource_change(EventType::CHANGED_RESOURCE_OSD, state);
        return map_overlays(result, [this](nlohmann::json conf) { return absolut_font_size_to_relative(unmap_paths(conf), m_resolution_conf.width); }); });

    srv->Put("/osd", [this](const nlohmann::json &config)
             {
        nlohmann::json previouse_config = m_config;
        auto partial_config = nlohmann::json::diff(m_config, map_overlays(config, [this](nlohmann::json entry) { return relative_font_size_to_absolut(map_paths(entry), m_resolution_conf.width); }));
        m_config = m_config.patch(partial_config);
        auto result = this->m_config;
        auto state = std::make_shared<OsdResource::OsdResourceState>(OsdResourceState(m_config, get_overlays_to_delete(previouse_config, m_config)));
        on_resource_change(EventType::CHANGED_RESOURCE_OSD, state);
        return map_overlays(result, [this](nlohmann::json conf) { return absolut_font_size_to_relative(unmap_paths(conf), m_resolution_conf.width); }); });

    srv->Post("/osd/upload", [](const httplib::MultipartFormData& file) {
        std::string extension = file.filename.substr(file.filename.find_last_of("."));
        if (std::find(std::begin(VALID_EXTENSIONS), std::end(VALID_EXTENSIONS), extension) == std::end(VALID_EXTENSIONS)) {
            WEBSERVER_LOG_ERROR("Invalid file extension: {}", file.filename);
            return false;
        }

        if (!std::filesystem::exists(IMAGE_PATH)) {
            WEBSERVER_LOG_ERROR("Image path does not exist: {}", IMAGE_PATH);
        }

        std::string file_path = std::string(IMAGE_PATH) + file.filename;
        try {
            std::ofstream ofs(file_path, std::ios::binary);
            ofs.write(file.content.c_str(), file.content.size());
            ofs.close();
            WEBSERVER_LOG_INFO("File saved to: {}", file_path);
            return true;
        } catch (const std::exception &e) {
            WEBSERVER_LOG_WARN("Failed to save file: {}", e.what());
            return false;
        }
    });
}
