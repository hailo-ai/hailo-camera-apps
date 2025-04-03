#include "configs.hpp"

nlohmann::json webserver::resources::ConfigResourceBase::get_frontend_default_config()
{
    if (m_frontend_default_config.empty())
    {
        throw std::runtime_error("Failed to get default frontend config");
    }
    return m_frontend_default_config;
}

nlohmann::json webserver::resources::ConfigResourceBase::get_encoder_default_config()
{
    if (m_encoder_osd_default_config["encoding"].empty())
    {
        throw std::runtime_error("Failed to get default encoder config");
    }
    return m_encoder_osd_default_config["encoding"];
}

nlohmann::json webserver::resources::ConfigResourceBase::get_osd_default_config()
{
    if (m_encoder_osd_default_config["osd"].empty())
    {
        throw std::runtime_error("Failed to get default osd config");
    }
    return m_encoder_osd_default_config["osd"];
}

nlohmann::json webserver::resources::ConfigResourceBase::get_osd_and_encoder_default_config()
{
    if (m_encoder_osd_default_config.empty())
    {
        throw std::runtime_error("Failed to get default osd config");
    }
    return m_encoder_osd_default_config;
}

nlohmann::json webserver::resources::ConfigResourceBase::get_hdr_default_config()
{
    if (m_frontend_default_config["hdr"].empty())
    {
        throw std::runtime_error("Failed to get default hdr config");
    }
    return m_frontend_default_config["hdr"];
}

nlohmann::json webserver::resources::ConfigResourceBase::get_denoise_default_config()
{
    if (m_frontend_default_config["denoise"].empty())
    {
        throw std::runtime_error("Failed to get default denoise config");
    }
    return m_frontend_default_config["denoise"];
}
