#include "isp.hpp"

#include "common/isp/v4l2_ctrl.hpp"
#include <functional>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

using namespace webserver::resources;
using namespace webserver::common;
using namespace webserver;

// TODO PULL min max from 3aconfig
//  not all gain values are valid in ISP, ISP rounds down to the nearest valid value, so we need to round up so we get
//  the value we want
#define ROUND_GAIN_GET_U16(gain) (uint16_t)((gain - (gain % 1024)) / 1024 + 1 * !!(gain % 1024))
#define EXCLUEDED_TUNING_PROFILES ({webserver::common::TUNING_PROFILE_MAX, webserver::common::TUNING_PROFILE_HDR_FHD})
#define ISP_FILTERS_MANUAL_STATE_IS_AUTO(state)                                                                        \
    (state == IspResource::FiltersManualState::FILTER_STATE_AUTO ||                                                    \
     state == IspResource::FiltersManualState::FILTER_STATE_FORCE_AUTO)
#define ISP_FILTERS_MANUAL_STATE_IS_MANUAL(state) (state == IspResource::FiltersManualState::FILTER_STATE_MANUAL)

IspResource::IspResource(std::shared_ptr<EventBus> event_bus, std::shared_ptr<ConfigResourceBase> config_res)
    : Resource(event_bus), m_baseline_stream_params(0, 0, 0, 0, 0), m_baseline_wdr_params(0),
      m_baseline_backlight_params(0, 0), m_isp_filters_manual_state(IspResource::FiltersManualState::FILTER_STATE_AUTO)
{
    m_default_3a_path = config_res->get_isp_default_config()["3a_config_path"].get<std::string>();

    subscribe_callback(EventType::RESET_ISP, [this](ResourceStateChangeNotification notification) {
        WEBSERVER_LOG_INFO("Received configure isp notification");
        this->init();
    });
    subscribe_callback(EventType::SWITCH_PROFILE, [this, config_res](ResourceStateChangeNotification notification) {
        auto &state = std::get<std::shared_ptr<ProfileNameState>>(notification.resource_state);
        std::string profile_name = state->value;
        WEBSERVER_LOG_DEBUG("Received switch profile notification, profile name: {}", profile_name);

        m_isp_filters_manual_state = config_res->get_denoise_default_config()["enabled"].get<bool>()
                                         ? IspResource::FiltersManualState::FILTER_STATE_FORCE_AUTO
                                         : IspResource::FiltersManualState::FILTER_STATE_AUTO;

        m_default_3a_path = config_res->get_isp_default_config()["3a_config_path"].get<std::string>();
        this->init();
    });
}

void IspResource::reset_config()
{
    this->init();
}

void IspResource::init(bool set_auto_wb)
{
    this->m_baseline_backlight_params = backlight_filter_t::get_from_json();
    WEBSERVER_LOG_INFO("ISP: Baseline backlight params: \n\tmax level: {}, \tmin level: {}",
                       m_baseline_backlight_params.max_level, m_baseline_backlight_params.min_level);

    // make sure AE is enabled
    auto ae = this->get_auto_exposure();
    if (!ae.enabled)
    {
        WEBSERVER_LOG_DEBUG("ISP: Auto exposure is disabled, enabling it");
        ae.enabled = true;
        this->set_auto_exposure(ae);
    }

    if (set_auto_wb)
    {
        // set auto white balance
        WEBSERVER_LOG_DEBUG("ISP: Setting auto white balance to auto");
        v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::AWB_MODE, 1);
    }

    m_isp_converge = false;
    WEBSERVER_LOG_DEBUG("ISP: enable 3a config auto algos, default config path: {}", m_default_3a_path);
    update_3a_config(true, m_default_3a_path);

    if (m_isp_filters_manual_state != IspResource::FiltersManualState::FILTER_STATE_MANUAL)
    {
        return; // baseline params are not needed in manual mode
    }

    wait_isp_converge(50, 1000);
    WEBSERVER_LOG_DEBUG("ISP: disable 3a config");
    update_3a_config(false, m_default_3a_path);
    m_isp_converge = true;

    uint16_t *sharpness_down = &m_baseline_stream_params.sharpness_down;
    uint16_t *sharpness_up = &m_baseline_stream_params.sharpness_up;
    v4l2_ctrl::get<uint16_t *>(v4l2_ctrl::Video0Ctrl::SHARPNESS_DOWN, sharpness_down);
    v4l2_ctrl::get<uint16_t *>(v4l2_ctrl::Video0Ctrl::SHARPNESS_UP, sharpness_up);
    v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::BRIGHTNESS, m_baseline_stream_params.brightness);
    v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::SATURATION, m_baseline_stream_params.saturation);
    v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::CONTRAST, m_baseline_stream_params.contrast);
    v4l2_ctrl::get<int16_t>(v4l2_ctrl::Video0Ctrl::WDR_CONTRAST, m_baseline_wdr_params);

    WEBSERVER_LOG_INFO("ISP: Baseline stream params: \n\tSharpness Down: {}\n\tSharpness Up: {}\n\tSaturation: "
                       "{}\n\tBrightness: {}\n\tContrast: {}\n\tWDR: {}",
                       m_baseline_stream_params.sharpness_down, m_baseline_stream_params.sharpness_up,
                       m_baseline_stream_params.saturation, m_baseline_stream_params.brightness,
                       m_baseline_stream_params.contrast, m_baseline_wdr_params);
}

bool IspResource::get_isp_converge()
{
    int converged = 0;
    bool ret = v4l2_ctrl::get<int>(v4l2_ctrl::Video0Ctrl::AE_CONVERGED, converged);
    if (!ret)
    {
        WEBSERVER_LOG_ERROR("Failed to get AE converged");
        throw std::runtime_error("Failed to get AE converged");
    }
    WEBSERVER_LOG_DEBUG("Got AE converged: {}", converged);
    if (converged != 1 && converged != 0)
    {
        WEBSERVER_LOG_ERROR("Invalid AE converged value");
        throw std::runtime_error("Invalid AE converged value");
    }
    return converged == 1;
}

void IspResource::wait_isp_converge(int polling_interval, int delay_after_polling)
{
    int watchdog_timeout = 2000;
    while (!get_isp_converge())
    {
        watchdog_timeout -= polling_interval;
        std::this_thread::sleep_for(std::chrono::milliseconds(polling_interval));
        if (watchdog_timeout <= 0)
        {
            WEBSERVER_LOG_WARN("ISP: AE did not converge");
            break;
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_after_polling));
}

void IspResource::wait_safe_to_pull()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isp_converge)
    {
        this->init(false);
        m_isp_converge = true;
    }
}

void IspResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/isp/refresh", std::function<void()>([this]() {
                 if (ISP_FILTERS_MANUAL_STATE_IS_MANUAL(m_isp_filters_manual_state))
                     m_isp_filters_manual_state =
                         IspResource::FiltersManualState::FILTER_STATE_AUTO; // reset to auto state on refresh
                 this->init();
             }));

    srv->Get("/isp/filters_manual_state", std::function<nlohmann::json()>([this]() {
                 nlohmann::json j_out;
                 j_out["auto"] = ISP_FILTERS_MANUAL_STATE_IS_AUTO(m_isp_filters_manual_state);
                 return j_out;
             }));

    srv->Post(
        "/isp/filters_manual_state",
        std::function<std::pair<nlohmann::json, int>(const nlohmann::json &req)>([this](const nlohmann::json &req) {
            std::string ret_msg;
            bool state = false;
            bool ret = json_extract_value<bool>(req, "auto", state, &ret_msg);
            if (!ret)
            {
                WEBSERVER_LOG_ERROR("Failed to extract filters manual state from JSON: {}", ret_msg);
                throw std::runtime_error(ret_msg);
            }
            if (m_isp_filters_manual_state == IspResource::FiltersManualState::FILTER_STATE_FORCE_AUTO)
            {
                WEBSERVER_LOG_WARN("Cannot set filters manual state to auto when force_auto is enabled");
                return std::pair<nlohmann::json, int>({{"auto", true}},
                                                      400); // return 400 Bad Request if force_auto is enabled
            }
            m_isp_filters_manual_state = state ? IspResource::FiltersManualState::FILTER_STATE_AUTO
                                               : IspResource::FiltersManualState::FILTER_STATE_MANUAL;
            this->init(false);
            nlohmann::json j_out;
            j_out["auto"] = state;
            return std::pair<nlohmann::json, int>(j_out, 200);
        }));

    srv->Post("/isp/powerline_frequency",
              std::function<nlohmann::json(const nlohmann::json &req)>([this](const nlohmann::json &req) {
                  std::string ret_msg;
                  powerline_frequency_t freq = POWERLINE_FREQUENCY_OFF;
                  bool ret = json_extract_value<powerline_frequency_t>(req, "powerline_freq", freq, &ret_msg);
                  if (!ret)
                  {
                      WEBSERVER_LOG_ERROR("Failed to extract powerline frequency from JSON: {}", ret_msg);
                      throw std::runtime_error(ret_msg);
                  }

                  WEBSERVER_LOG_DEBUG("Setting powerline frequency to: {}", freq);
                  ret = v4l2_ctrl::set<int>(v4l2_ctrl::Video0Ctrl::POWERLINE_FREQUENCY, (uint16_t)freq);
                  if (!ret)
                  {
                      WEBSERVER_LOG_ERROR("Failed to set powerline frequency");
                      throw std::runtime_error("Failed to set powerline frequency");
                  }
                  nlohmann::json j_out;
                  j_out["powerline_freq"] = freq;
                  return j_out;
              }));

    srv->Get("/isp/powerline_frequency", std::function<nlohmann::json()>([this]() {
                 wait_safe_to_pull();
                 int val;
                 bool ret = v4l2_ctrl::get<int>(v4l2_ctrl::Video0Ctrl::POWERLINE_FREQUENCY, val);
                 if (!ret)
                 {
                     WEBSERVER_LOG_ERROR("Failed to get powerline frequency");
                     throw std::runtime_error("Failed to get powerline frequency");
                 }
                 auto freq = (powerline_frequency_t)val;
                 nlohmann::json j_out;
                 j_out["powerline_freq"] = freq;
                 WEBSERVER_LOG_DEBUG("Got powerline frequency: {}", freq);
                 return j_out;
             }));

    srv->Post("/isp/noise_reduction", [this](const nlohmann::json &req) {
        std::string ret_msg;
        int nr = 0;
        bool ret = json_extract_value<int>(req, "noise_reduction", nr, &ret_msg);
        if (!ret)
        {
            WEBSERVER_LOG_ERROR("Failed to extract noise reduction from JSON: {}", ret_msg);
            throw std::runtime_error(ret_msg);
        }
        if (nr > 100 || nr < 0)
        {
            WEBSERVER_LOG_ERROR("Invalid noise reduction value");
            throw std::runtime_error("Invalid noise reduction value");
        }
        WEBSERVER_LOG_DEBUG("Setting noise reduction to: {}", nr);
        ret = v4l2_ctrl::set<int>(v4l2_ctrl::Video0Ctrl::NOISE_REDUCTION, nr);
        if (!ret)
        {
            WEBSERVER_LOG_ERROR("Failed to set noise reduction");
            throw std::runtime_error("Failed to set noise reduction");
        }
    });

    srv->Post("/isp/wdr", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                  if (ISP_FILTERS_MANUAL_STATE_IS_AUTO(m_isp_filters_manual_state))
                  {
                      WEBSERVER_LOG_ERROR("WDR can only be set in manual mode");
                      throw std::runtime_error("WDR can only be set in manual mode");
                  }

                  wide_dynamic_range_t wdr = j_body.get<wide_dynamic_range_t>();
                  auto val = v4l2_ctrl::calculate_value_from_precentage<int32_t>(
                      wdr.value, v4l2_ctrl::Video0Ctrl::WDR_CONTRAST, m_baseline_wdr_params);
                  WEBSERVER_LOG_INFO("Setting WDR to: {}", val);
                  bool ret = v4l2_ctrl::set<int16_t>(v4l2_ctrl::Video0Ctrl::WDR_CONTRAST, val);
                  if (!ret)
                  {
                      WEBSERVER_LOG_ERROR("Failed to set WDR");
                      throw std::runtime_error("Failed to set WDR");
                  }
                  return j_body;
              }));

    srv->Get("/isp/wdr", std::function<nlohmann::json()>([this]() {
                 if (ISP_FILTERS_MANUAL_STATE_IS_AUTO(m_isp_filters_manual_state))
                 {
                     wide_dynamic_range_t wdr;
                     wdr.value = 50; // default value for auto mode
                     nlohmann::json j_out = wdr;
                     WEBSERVER_LOG_DEBUG("WDR is in auto mode, returning default value: 50");
                     return j_out;
                 }

                 wait_safe_to_pull();
                 wide_dynamic_range_t wdr;
                 int32_t val;
                 bool ret = v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::WDR_CONTRAST, val);
                 if (!ret)
                 {
                     WEBSERVER_LOG_ERROR("Failed to get WDR");
                     throw std::runtime_error("Failed to get WDR");
                 }
                 wdr.value = v4l2_ctrl::calculate_precentage_from_value<int32_t>(
                     val, v4l2_ctrl::Video0Ctrl::WDR_CONTRAST, m_baseline_wdr_params);
                 WEBSERVER_LOG_INFO("Got WDR value: {}", wdr.value);
                 nlohmann::json j_out = wdr;
                 return j_out;
             }));

    srv->Post("/isp/awb", std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                  webserver::common::auto_white_balance_t awb;
                  try
                  {
                      awb = j_body.get<webserver::common::auto_white_balance_t>();
                  }
                  catch (const std::exception &e)
                  {
                      WEBSERVER_LOG_ERROR("Failed to cast JSON to auto_white_balance_t");
                      throw std::runtime_error("Failed to cast JSON to auto_white_balance_t");
                  }

                  if (awb.value == AUTO_WHITE_BALANCE_PROFILE_AUTO)
                  {
                      WEBSERVER_LOG_DEBUG("Setting AWB to auto");
                      v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::AWB_MODE, 1);
                  }
                  else
                  {
                      WEBSERVER_LOG_DEBUG("Setting AWB to manual with profile: {}", awb.value);
                      v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::AWB_MODE, 0);
                      v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::AWB_ILLUM_INDEX, awb.value);
                  }

                  nlohmann::json j_out = awb;
                  return j_out;
              }));

    srv->Get("/isp/awb", std::function<nlohmann::json()>([this]() {
                 wait_safe_to_pull();
                 int32_t val;
                 bool ret = v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::AWB_MODE, val);
                 if (!ret)
                 {
                     WEBSERVER_LOG_ERROR("Failed to get AWB mode");
                     throw std::runtime_error("Failed to get AWB mode");
                 }
                 if (val != 1) // manual mode, get profile
                 {
                     ret = v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::AWB_ILLUM_INDEX, val);
                     if (!ret)
                     {
                         WEBSERVER_LOG_ERROR("Failed to get AWB profile");
                         throw std::runtime_error("Failed to get AWB profile");
                     }
                 }
                 else // automatic mode
                 {
                     val = -1;
                 }
                 webserver::common::auto_white_balance_t awb{(webserver::common::auto_white_balance_profile)val};
                 nlohmann::json j_out = awb;
                 return j_out;
             }));

    srv->Get("/isp/stream_params", std::function<nlohmann::json()>([this]() {
                 if (ISP_FILTERS_MANUAL_STATE_IS_AUTO(m_isp_filters_manual_state))
                 {
                     webserver::common::stream_params_t p{50, 50, 50, 50};
                     nlohmann::json j_out = p;
                     return j_out;
                 }

                 wait_safe_to_pull();
                 stream_isp_params_t p(0, 0, 0, 0, 0);
                 uint16_t *sharpness_down = &p.sharpness_down;
                 uint16_t *sharpness_up = &p.sharpness_up;
                 v4l2_ctrl::get<uint16_t *>(v4l2_ctrl::Video0Ctrl::SHARPNESS_DOWN, sharpness_down);
                 v4l2_ctrl::get<uint16_t *>(v4l2_ctrl::Video0Ctrl::SHARPNESS_UP, sharpness_up);
                 v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::BRIGHTNESS, p.brightness);
                 v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::SATURATION, p.saturation);
                 v4l2_ctrl::get<int32_t>(v4l2_ctrl::Video0Ctrl::CONTRAST, p.contrast);
                 nlohmann::json j_out = m_baseline_stream_params.to_stream_params(p);
                 WEBSERVER_LOG_INFO("Got stream params: {}", j_out.dump());
                 return j_out;
             }));

    srv->Post("/isp/stream_params",
              std::function<nlohmann::json(const nlohmann::json &)>([this](const nlohmann::json &j_body) {
                  if (ISP_FILTERS_MANUAL_STATE_IS_AUTO(m_isp_filters_manual_state))
                  {
                      WEBSERVER_LOG_ERROR("Stream params can only be set in manual mode");
                      throw std::runtime_error("Stream params can only be set in manual mode");
                  }

                  std::string ret_msg;
                  webserver::common::stream_params_t stream_params;
                  try
                  {
                      stream_params = j_body.get<webserver::common::stream_params_t>();
                  }
                  catch (const std::exception &e)
                  {
                      throw std::runtime_error("Failed to cast JSON to stream_params_t");
                  }
                  auto isp_params = m_baseline_stream_params.from_stream_params(stream_params);

                  v4l2_ctrl::set<int32_t>(v4l2_ctrl::Video0Ctrl::SATURATION, isp_params.saturation);
                  v4l2_ctrl::set<int32_t>(v4l2_ctrl::Video0Ctrl::BRIGHTNESS,
                                          static_cast<int8_t>(isp_params.brightness));
                  v4l2_ctrl::set<int32_t>(v4l2_ctrl::Video0Ctrl::CONTRAST, isp_params.contrast);

                  v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::EE_ENABLE, 0);

                  v4l2_ctrl::set<uint16_t *>(v4l2_ctrl::Video0Ctrl::SHARPNESS_DOWN, &isp_params.sharpness_down);
                  v4l2_ctrl::set<uint16_t *>(v4l2_ctrl::Video0Ctrl::SHARPNESS_UP, &isp_params.sharpness_up);

                  v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::EE_ENABLE, 1);

                  // cast out to json
                  nlohmann::json j_out = stream_params;
                  return j_out;
              }));

    srv->Post("/isp/auto_exposure",
              std::function<nlohmann::json(const nlohmann::json &)>(
                  [this](const nlohmann::json &j_body) { return this->set_auto_exposure(j_body); }));

    srv->Patch("/isp/auto_exposure", [this](const nlohmann::json &j_body) {
        auto params = this->get_auto_exposure();
        nlohmann::json j_params = params;
        j_params.merge_patch(j_body);
        return this->set_auto_exposure(j_params);
    });

    srv->Get("/isp/auto_exposure", std::function<nlohmann::json()>([this]() {
                 wait_safe_to_pull();
                 auto params = this->get_auto_exposure();
                 nlohmann::json j_out = params;
                 return j_out;
             }));

    srv->Get("/isp/ranges/auto_exposure", std::function<nlohmann::json()>([this]() {
                 nlohmann::json j_out = get_auto_exposure_ranges();
                 return j_out;
             }));

    srv->Get("/isp/safe_to_pull", std::function<nlohmann::json()>([this]() {
                 nlohmann::json j_out;
                 j_out["safe_to_pull"] = get_isp_converge();
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 return j_out;
             }));

    srv->Get("/isp/sensor_model", std::function<nlohmann::json()>([this]() {
                 nlohmann::json j_out;
                 j_out["sensor_model"] = get_sensor_type();
                 j_out["available_resolutions"] = webserver::common::sensor_resolutions_for_user.at(get_sensor_type());
                 WEBSERVER_LOG_DEBUG("Got sensor model: {}", j_out["sensor_model"]);
                 return j_out;
             }));
}

ae_ranges_t IspResource::get_auto_exposure_ranges()
{
    ae_ranges_t ranges;
    ranges.ae_gain = v4l2_ctrl::min_max_isp_params.at(v4l2_ctrl::Video0Ctrl::AE_GAIN);
    ranges.ae_gain.min = ROUND_GAIN_GET_U16(ranges.ae_gain.min);
    ranges.ae_gain.max = ROUND_GAIN_GET_U16(ranges.ae_gain.max);
    ranges.ae_integration_time = v4l2_ctrl::min_max_isp_params.at(v4l2_ctrl::Video0Ctrl::AE_INTEGRATION_TIME);
    return ranges;
}

auto_exposure_t IspResource::get_auto_exposure()
{
    uint16_t enabled = 0;
    uint16_t integration_time = 0;
    uint32_t gain = 0;
    v4l2_ctrl::get<uint16_t>(v4l2_ctrl::Video0Ctrl::AE_ENABLE, enabled);
    v4l2_ctrl::get<uint32_t>(v4l2_ctrl::Video0Ctrl::AE_GAIN, gain);
    v4l2_ctrl::get<uint16_t>(v4l2_ctrl::Video0Ctrl::AE_INTEGRATION_TIME, integration_time);

    WEBSERVER_LOG_DEBUG("Got auto exposure: enabled: {}, gain: {}, integration_time: {}", enabled, gain,
                        integration_time);

    backlight_filter_t current = backlight_filter_t::get_from_json();
    uint16_t backlight = m_baseline_backlight_params.to_precentage(current);

    return auto_exposure_t{(bool)enabled, ROUND_GAIN_GET_U16(gain), integration_time, backlight};
}

nlohmann::json IspResource::set_auto_exposure(const nlohmann::json &req)
{
    webserver::common::auto_exposure_t ae;
    try
    {
        ae = req.get<webserver::common::auto_exposure_t>();
    }
    catch (const std::exception &e)
    {
        WEBSERVER_LOG_ERROR("Failed to cast JSON to auto_exposure_t");
        throw std::runtime_error("Failed to cast JSON to auto_exposure_t");
    }

    if (!set_auto_exposure(ae))
    {
        WEBSERVER_LOG_ERROR("Failed to set auto exposure");
        throw std::runtime_error("Failed to set auto exposure");
    }

    // cast out to json
    nlohmann::json j_out = get_auto_exposure();
    return j_out;
}

bool IspResource::set_auto_exposure(auto_exposure_t &ae)
{
    uint32_t gain = (uint32_t)ae.gain * 1024;
    WEBSERVER_LOG_DEBUG("Setting auto exposure enabled: {}", ae.enabled);
    v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::AE_ENABLE, ae.enabled);
    if (ae.enabled)
    {
        // sleep so auto exposure values will be updated
        std::this_thread::sleep_for(std::chrono::seconds(1));
        backlight_filter_t current = m_baseline_backlight_params.from_precentage(ae.backlight);
        nlohmann::json j_3a = get_3a_config();

        std::optional<std::reference_wrapper<nlohmann::json>> ae_class_opt =
            get_3a_config_class(j_3a, ISP_CLASSNAME_AUTO_EXPOSURE);
        if (!ae_class_opt.has_value())
        {
            WEBSERVER_LOG_ERROR("Failed to get AE class from 3a config");
            return false;
        }
        nlohmann::json &ae_class = ae_class_opt.value().get();
        ae_class["wdrContrast.max"] = current.max_level;
        ae_class["wdrContrast.min"] = current.min_level;
        update_3a_config(j_3a);
    }
    else
    {
        WEBSERVER_LOG_DEBUG("AutoExposure is on manual mode, setting gain {} and integration time {}", gain,
                            ae.integration_time);
        bool ret = v4l2_ctrl::set<uint32_t>(v4l2_ctrl::Video0Ctrl::AE_GAIN, gain);
        if (!ret)
        {
            return false;
        }
        ret = v4l2_ctrl::set<uint16_t>(v4l2_ctrl::Video0Ctrl::AE_INTEGRATION_TIME, ae.integration_time);
        if (!ret)
        {
            return false;
        }
    }

    return true;
}
