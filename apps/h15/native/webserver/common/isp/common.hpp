#pragma once

#include <malloc.h>
#include "common/common.hpp"
#include <nlohmann/json.hpp>
#include <thread>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <cstring>


namespace webserver
{
    namespace common
    {
        void update_3a_config(bool enabled);
        void update_3a_config(nlohmann::json config);
        nlohmann::json get_3a_config();


        struct stream_params_t
        {
            uint16_t saturation;
            uint16_t brightness;
            uint16_t contrast;
            uint16_t sharpness;
        };

        class stream_isp_params_t
        {
        public:
            int32_t saturation = 0;
            int32_t brightness = 0;
            int32_t contrast = 0;
            uint16_t sharpness_down = 0;
            uint16_t sharpness_up = 0;

            stream_isp_params_t(int32_t saturation, int32_t brightness, int32_t contrast, uint16_t sharpness_down, uint16_t sharpness_up);

            stream_isp_params_t from_stream_params(const stream_params_t &params);
            stream_params_t to_stream_params(const stream_isp_params_t &params);
        };

        class backlight_filter_t
        {
        public:
            uint16_t max;
            uint16_t min;

            backlight_filter_t(uint16_t max, uint16_t min);

            backlight_filter_t from_precentage(uint16_t precentage);
            uint16_t to_precentage(const backlight_filter_t &filter);
            static backlight_filter_t get_from_json();
        };

        enum binning_mode_t
        {
            BINNING_MODE_OFF = 0,
            BINNING_MODE_4_BY_4 = 1,
            BINNING_MODE_9_BY_9 = 2,
            BINNING_MODE_16_BY_16 = 3,
            BINNING_MODE_MAX
        };

        enum tuning_profile_t
        {
            TUNING_PROFILE_DEFAULT = 0,
            TUNING_PROFILE_DENOISE = 1,
            TUNING_PROFILE_BACKLIGHT_COMPENSATION = 2,
            TUNING_PROFILE_HDR_FHD = 3,
            TUNING_PROFILE_HDR_4K = 4,
            TUNING_PROFILE_MAX
        };

        struct tuning_t
        {
            tuning_profile_t value;
        };

        enum powerline_frequency_t
        {
            POWERLINE_FREQUENCY_OFF = 0,
            POWERLINE_FREQUENCY_50 = 1,
            POWERLINE_FREQUENCY_60 = 2,
            POWERLINE_FREQUENCY_MAX
        };

        struct auto_exposure_t
        {
            bool enabled;
            uint16_t gain;
            uint16_t integration_time;
            uint16_t backlight;
        };

        struct wide_dynamic_range_t
        {
            uint16_t value;
        };

        enum auto_white_balance_profile
        {
            AUTO_WHITE_BALANCE_PROFILE_AUTO = -1,

            AUTO_WHITE_BALANCE_PROFILE_A = 0,
            AUTO_WHITE_BALANCE_PROFILE_D50 = 1,
            AUTO_WHITE_BALANCE_PROFILE_D65 = 2,
            AUTO_WHITE_BALANCE_PROFILE_D75 = 3,
            AUTO_WHITE_BALANCE_PROFILE_TL84 = 4,
            AUTO_WHITE_BALANCE_PROFILE_F12 = 5,
            AUTO_WHITE_BALANCE_PROFILE_CWF = 6,
            AUTO_WHITE_BALANCE_PROFILE_MAX
        };

        struct auto_white_balance_t
        {
            auto_white_balance_profile value;
        };

        NLOHMANN_JSON_SERIALIZE_ENUM(tuning_profile_t, {
                                                           {TUNING_PROFILE_DEFAULT, "Default"},
                                                           {TUNING_PROFILE_DENOISE, "Denoise"},
                                                           {TUNING_PROFILE_BACKLIGHT_COMPENSATION, "Backlight Compensation"},
                                                           {TUNING_PROFILE_HDR_FHD, "HDR (FHD)"},
                                                           {TUNING_PROFILE_HDR_4K, "HDR (4K)"},
                                                       })

        NLOHMANN_JSON_SERIALIZE_ENUM(powerline_frequency_t, {
                                                                {POWERLINE_FREQUENCY_OFF, "Off"},
                                                                {POWERLINE_FREQUENCY_50, "50"},
                                                                {POWERLINE_FREQUENCY_60, "60"},
                                                            })

        NLOHMANN_JSON_SERIALIZE_ENUM(binning_mode_t, {
                                                         {BINNING_MODE_OFF, "off"},
                                                         {BINNING_MODE_4_BY_4, "4x4"},
                                                         {BINNING_MODE_9_BY_9, "9x9"},
                                                         {BINNING_MODE_16_BY_16, "16x16"},
                                                     })
        NLOHMANN_JSON_SERIALIZE_ENUM(auto_white_balance_profile, {
                                                                     {AUTO_WHITE_BALANCE_PROFILE_AUTO, "auto"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_A, "A"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_D50, "D50"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_D65, "D65"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_D75, "D75"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_TL84, "TL84"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_F12, "F12"},
                                                                     {AUTO_WHITE_BALANCE_PROFILE_CWF, "CWF"},
                                                                 })

        void from_json(const nlohmann::json &json, stream_isp_params_t &params);
        void to_json(nlohmann::json &json, const stream_isp_params_t &params);
        void from_json(const nlohmann::json &json, stream_params_t &params);
        void to_json(nlohmann::json &json, const stream_params_t &params);
        void from_json(const nlohmann::json &json, auto_exposure_t &params);
        void to_json(nlohmann::json &json, const auto_exposure_t &params);
        void from_json(const nlohmann::json &json, wide_dynamic_range_t &params);
        void to_json(nlohmann::json &json, const wide_dynamic_range_t &params);
        void from_json(const nlohmann::json &json, auto_white_balance_t &params);
        void to_json(nlohmann::json &json, const auto_white_balance_t &params);
        void from_json(const nlohmann::json &json, tuning_t &params);
        void to_json(nlohmann::json &json, const tuning_t &params);

        template <typename T>
        std::vector<T> get_enum_values(T last_value, std::vector<T> excluded_values = {})
        {
            std::vector<T> values;
            for (int i = 0; i < static_cast<int>(last_value); i++)
            {
                T value = static_cast<T>(i);
                if (excluded_values.empty() || std::find(excluded_values.begin(), excluded_values.end(), value) == excluded_values.end())
                {
                    values.push_back(value);
                }
            }
            return values;
        }
    }
}
