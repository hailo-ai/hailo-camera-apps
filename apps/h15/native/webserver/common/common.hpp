#pragma once
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>

#define V4L2_DEVICE_NAME "/dev/video0"


template <typename T>
inline bool json_extract_value(const nlohmann::json &json, const std::string &key, T &out, std::string *return_msg = nullptr)
{
    if (json.find(key) == json.end())
    {
        if (return_msg)
        {
            *return_msg = "Missing " + key + " in JSON";
        }
        return false;
    }

    try
    {
        out = json[key].get<T>();
    }
    catch (nlohmann::json::exception &e)
    {
        if (return_msg)
        {
            *return_msg = "Failed to extract " + key + " from JSON: " + e.what();
        }
        return false;
    }
    return true;
}