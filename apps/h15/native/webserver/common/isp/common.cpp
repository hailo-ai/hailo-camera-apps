#include "common.hpp"
#include <cstring>
#include <stdio.h>
#include "common/logger_macros.hpp"

#include "resources/common/v4l2_ctrl.hpp"

using namespace webserver::common;

static constexpr const char* TRIPLE_A_CONFIG_PATH = "/usr/bin/3aconfig.json";

void webserver::common::update_3a_config(bool enabled)
{
    // Read JSON file
    std::ifstream file(TRIPLE_A_CONFIG_PATH);
    if (!file.is_open())
    {
        WEBSERVER_LOG_ERROR("Failed to open JSON file");
        return;
    }

    nlohmann::json config;
    file >> config;
    file.close();

    // Change values to enable=value on objects with "classname": "Aeev1" and "classname": "ACproc"
    auto root = config["root"];

    for (auto &obj : config["root"])
    {
        if (obj["classname"] == "Aeev1" || obj["classname"] == "ACproc" || obj["classname"] == "AWdrv4")
        {
            obj["enable"] = enabled;
            obj["disable"] = false;
        }
    }

    // Write updated JSON back to file
    std::ofstream outFile(TRIPLE_A_CONFIG_PATH);
    if (!outFile.is_open())
    {
        WEBSERVER_LOG_ERROR("Failed to open output file");
        return;
    }

    outFile << config.dump();
    outFile.close();

}

void webserver::common::update_3a_config(nlohmann::json config)
{
    // Write updated JSON back to file
    std::ofstream outFile(TRIPLE_A_CONFIG_PATH);
    if (!outFile.is_open())
    {
        WEBSERVER_LOG_ERROR("Failed to open output file");
        return;
    }

    outFile << config.dump();
    outFile.close();
}

nlohmann::json webserver::common::get_3a_config()
{
    nlohmann::json config;
    // Read JSON file
    std::ifstream file(TRIPLE_A_CONFIG_PATH);
    if (!file.is_open())
    {
        WEBSERVER_LOG_ERROR("Failed to open JSON file");
        return NULL;
    }

    file >> config;
    file.close();
    return config;
}

void webserver::common::from_json(const nlohmann::json &json, webserver::common::stream_isp_params_t &params)
{
    json.at("saturation").get_to(params.saturation);
    json.at("brightness").get_to(params.brightness);
    json.at("contrast").get_to(params.contrast);
    json.at("sharpness_up").get_to(params.sharpness_up);
    json.at("sharpness_down").get_to(params.sharpness_down);
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::stream_isp_params_t &params)
{
    json = nlohmann::json{{"saturation", params.saturation},
                          {"brightness", params.brightness},
                          {"contrast", params.contrast},
                          {"sharpness_up", params.sharpness_up},
                          {"sharpness_down", params.sharpness_down}};
}

void webserver::common::from_json(const nlohmann::json &json, webserver::common::stream_params_t &params)
{
    json.at("saturation").get_to(params.saturation);
    json.at("brightness").get_to(params.brightness);
    json.at("contrast").get_to(params.contrast);
    json.at("sharpness").get_to(params.sharpness);
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::stream_params_t &params)
{
    json = nlohmann::json{{"saturation", params.saturation},
                          {"brightness", params.brightness},
                          {"contrast", params.contrast},
                          {"sharpness", params.sharpness}};
}

void webserver::common::from_json(const nlohmann::json &json, webserver::common::auto_exposure_t &params)
{
    json.at("enabled").get_to(params.enabled);
    json.at("gain").get_to(params.gain);
    json.at("integration_time").get_to(params.integration_time);
    json.at("backlight").get_to(params.backlight);
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::auto_exposure_t &params)
{
    json = nlohmann::json{{"enabled", params.enabled},
                          {"gain", params.gain},
                          {"integration_time", params.integration_time},
                          {"backlight", params.backlight}};
}

void webserver::common::from_json(const nlohmann::json &json, webserver::common::wide_dynamic_range_t &params)
{
    json.at("value").get_to(params.value);
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::wide_dynamic_range_t &params)
{
    json = nlohmann::json{{"value", params.value}};
}

void webserver::common::from_json(const nlohmann::json &json, webserver::common::auto_white_balance_t &params)
{
    json.at("value").get_to(params.value);
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::auto_white_balance_t &params)
{
    json = nlohmann::json{{"value", params.value}};
}

void webserver::common::from_json(const nlohmann::json &json, webserver::common::tuning_t &params)
{
    json.at("value").get_to(params.value);
}

void webserver::common::to_json(nlohmann::json &json, const webserver::common::tuning_t &params)
{
    json = nlohmann::json{{"value", params.value}};
}

stream_isp_params_t::stream_isp_params_t(int32_t saturation, int32_t brightness, int32_t contrast, uint16_t sharpness_down, uint16_t sharpness_up) : saturation(saturation), brightness(brightness), contrast(contrast), sharpness_down(sharpness_down), sharpness_up(sharpness_up) {}

stream_isp_params_t stream_isp_params_t::from_stream_params(const stream_params_t &params)
{
    int32_t v_brightness = v4l2_ctrl::calculate_value_from_precentage<int32_t>(params.brightness, v4l2_ctrl::Video0Ctrl::BRIGHTNESS, brightness);
    int32_t v_contrast = v4l2_ctrl::calculate_value_from_precentage<int32_t>(params.contrast, v4l2_ctrl::Video0Ctrl::CONTRAST, contrast);
    int32_t v_saturation = v4l2_ctrl::calculate_value_from_precentage<int32_t>(params.saturation, v4l2_ctrl::Video0Ctrl::SATURATION, saturation);
    int32_t v_sharpness_down = v4l2_ctrl::calculate_value_from_precentage<int32_t>(params.sharpness, v4l2_ctrl::Video0Ctrl::SHARPNESS_DOWN, sharpness_down);
    int32_t v_sharpness_up = v4l2_ctrl::calculate_value_from_precentage<int32_t>(params.sharpness, v4l2_ctrl::Video0Ctrl::SHARPNESS_UP, sharpness_up);

    return stream_isp_params_t(v_saturation, v_brightness, v_contrast, v_sharpness_down, v_sharpness_up);
}

stream_params_t stream_isp_params_t::to_stream_params(const stream_isp_params_t &params)
{
    uint16_t p_brightness = v4l2_ctrl::calculate_precentage_from_value<int32_t>(params.brightness, v4l2_ctrl::Video0Ctrl::BRIGHTNESS, brightness);
    uint16_t p_contrast = v4l2_ctrl::calculate_precentage_from_value<int32_t>(params.contrast, v4l2_ctrl::Video0Ctrl::CONTRAST, contrast);
    uint16_t p_saturation = v4l2_ctrl::calculate_precentage_from_value<int32_t>(params.saturation, v4l2_ctrl::Video0Ctrl::SATURATION, saturation);
    uint16_t p_sharpness = v4l2_ctrl::calculate_precentage_from_value<uint16_t>(params.sharpness_down, v4l2_ctrl::Video0Ctrl::SHARPNESS_DOWN, sharpness_down);

    return stream_params_t{p_saturation, p_brightness, p_contrast, p_sharpness};
}

backlight_filter_t::backlight_filter_t(uint16_t max, uint16_t min) : max(max), min(min) {}

backlight_filter_t backlight_filter_t::from_precentage(uint16_t precentage)
{
  auto new_max = v4l2_ctrl::calculate_value_from_precentage<int32_t>(precentage, v4l2_ctrl::Video0Ctrl::AE_WDR_VALUES, max);
  auto new_min = v4l2_ctrl::calculate_value_from_precentage<int32_t>(precentage, v4l2_ctrl::Video0Ctrl::AE_WDR_VALUES, min);
  return backlight_filter_t(new_max, new_min);
}

uint16_t backlight_filter_t::to_precentage(const backlight_filter_t &filter)
{
  return v4l2_ctrl::calculate_precentage_from_value<int32_t>(filter.max, v4l2_ctrl::Video0Ctrl::AE_WDR_VALUES, max);
}

backlight_filter_t backlight_filter_t::get_from_json()
{
  uint16_t max;
  uint16_t min;
  nlohmann::json config = get_3a_config();

  auto root = config["root"];
  for (auto &obj : config["root"])
  {
      if (obj["classname"] == "AdaptiveAe")
      {
          max = obj["wdrContrast.max"];
          min = obj["wdrContrast.min"];
          return backlight_filter_t(max, min);
      }
  }
  return backlight_filter_t(0, 0);
}
