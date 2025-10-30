#pragma once
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <vector>

// Loads a JSON file from the given path and returns a nlohmann::json object.
// Throws std::runtime_error if the file cannot be opened or parsed.
inline nlohmann::json load_json_file(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open JSON file: " + path);
    }
    nlohmann::json j;
    file >> j;
    return j;
}

std::vector<std::string> get_profile_names(const std::string& path)
{
    nlohmann::json j = load_json_file(path);
    if (!j.contains("profiles") || !j["profiles"].is_array())
    {
        throw std::runtime_error("Invalid JSON structure: 'profiles' array missing");
    }
    std::vector<std::string> profile_names;
    for (const auto& profile : j["profiles"])
    {
        if (profile.contains("name") && profile["name"].is_string())
        {
            profile_names.push_back(profile["name"]);
        }
        else
        {
            throw std::runtime_error("Invalid profile entry: 'name' missing or not a string");
        }
    }
    return profile_names;
}


std::vector<std::string> get_profile_files(const std::string& path)
{
    nlohmann::json j = load_json_file(path);
    if (!j.contains("profiles") || !j["profiles"].is_array())
    {
        throw std::runtime_error("Invalid JSON structure: 'profiles' array missing");
    }
    std::vector<std::string> profile_paths;
    for (const auto& profile : j["profiles"])
    {
        if (profile.contains("config_file") && profile["config_file"].is_string())
        {
            profile_paths.push_back(profile["config_file"]);
        }
        else
        {
            throw std::runtime_error("Invalid profile entry: 'config_file' missing or not a string");
        }
    }
    return profile_paths;
}
