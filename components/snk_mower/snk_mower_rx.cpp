#include "snk_mower.h"
#include "protocol.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace snk_mower {

static const char *const TAG = "snk_mower";

void SnkMower::read_uart() {
  // Frames are `&{json}<crc>#`; only the JSON object is needed, so collect
  // from "&{" to the matching '}' outside strings. A lone '{' can be a CRC byte.
  int budget = 256;
  uint8_t byte;
  while (budget-- > 0 && available() > 0 && read_byte(&byte)) {
    rx_bytes_++;
    if (!rx_in_json_) {
      bool start = rx_after_amp_ && byte == '{';
      rx_after_amp_ = byte == '&';
      if (!start)
        continue;
      rx_index_ = 0;
      rx_in_string_ = false;
      rx_in_json_ = true;
    }
    if (byte == '"' && (rx_index_ == 0 || rx_buf_[rx_index_ - 1] != '\\'))
      rx_in_string_ = !rx_in_string_;
    if (rx_index_ < BUF_SIZE - 1)
      rx_buf_[rx_index_++] = (char) byte;
    if (rx_in_string_ || byte != '}')
      continue;

    rx_buf_[rx_index_] = '\0';
    rx_in_json_ = false;
    JsonDocument doc;
    if (deserializeJson(doc, rx_buf_) != DeserializationError::Ok || !doc["cmd"].is<uint32_t>()) {
      rx_bad_++;
      ESP_LOGW(TAG, "RX unparsable: %s", rx_buf_);
      trace("RX?", rx_buf_);
      continue;
    }
    rx_frames_++;
    last_rx_ms_ = millis();
    uint32_t cmd = doc["cmd"];
    if (proto::is_esp_command(cmd, doc["pwd"].is<int>())) {
      ESP_LOGD(TAG, "RX echo of our own frame, ignored: %s", rx_buf_);
      trace("RX echo", rx_buf_);
      continue;
    }
    if (cmd == proto::MB_RTC || cmd == proto::MB_WIFI_ACK || cmd == proto::MB_BT_ACK)
      ESP_LOGV(TAG, "RX %s", rx_buf_);
    else {
      ESP_LOGD(TAG, "RX %s", rx_buf_);
      trace("RX", rx_buf_);
    }
    handle_json(doc);
  }
}

void SnkMower::handle_json(const JsonDocument &doc) {
  uint32_t cmd = doc["cmd"];

  switch (cmd) {
    // Boot handshake. U13 waits up to 2.5 s for ESP_INFO and then 1 s for
    // ESP_INIT (0x080446e4). Without them it marks the display board as
    // missing, loops on 0x20000002 without feeding its hardware watchdog and
    // resets after ~16 s, which cuts power.
    case proto::MB_POWER_ON:
      // Sent once when U13 boots, before the handshake. U13 also repeats it
      // right after the handshake, which must not drop the link again.
      if (link_ != Link::UP) {
        ESP_LOGI(TAG, "MB powered on, waiting for its boot handshake");
        link_ = Link::WAIT_MB;
        pin_sent_ = false;
      }
      return;
    case proto::MB_BOOT_HEART:
      if (link_ == Link::WAIT_MB)
        ESP_LOGI(TAG, "MB boot handshake started");
      link_ = Link::HANDSHAKE;
      pin_sent_ = false;
      send_esp_info();
      return;
    case proto::MB_BOOT_INIT:
      link_ = Link::HANDSHAKE;
      send_init();
      return;
    case proto::MB_LINK_UP:
      // U13 sends this twice after the handshake, and again whenever it got
      // no frame from us for 3 s (0x08044164).
      if (link_ == Link::UP && millis() - link_up_ms_ > 1000)
        ESP_LOGW(TAG, "MB reports no frames from us for 3 s (link timeout)");
      link_up("MB handshake done");
      return;
    case proto::MB_INIT_ERROR:
      ESP_LOGE(TAG, "MB init failed, error=0x%lX (bit 0x4: display board missing). It will reset itself.",
               (unsigned long) (doc["error"] | 0UL));
      return;
    default:
      break;
  }

  // Any other frame means U13 is running. After an ESP-only restart there is
  // no handshake, so this is how we resume.
  if (link_ == Link::WAIT_MB)
    link_up("MB already running");

  switch (cmd) {
    case proto::MB_STATUS:
      handle_status(doc);
      break;
    case proto::MB_DEVICE_INFO:
      handle_device_info(doc);
      break;
    case proto::MB_PIN_ACK:
      handle_pin_result(doc);
      break;
    case proto::MB_ERROR_NOTIFY:
      handle_error_notify(doc);
      break;
    case proto::MB_LOCK: {
      bool locked = (doc["lock"] | 0) != 0;
      // U13 asks for the PIN; the original sends it once the user has typed it.
      if (locked && pin_retries_ < 5) {
        ESP_LOGI(TAG, "MB locked, sending PIN");
        send_pin();
      }
      break;
    }
    case proto::MB_MAP_CFG:
      if (work_area_sensor_ && doc["area"].is<int>())
        work_area_sensor_->publish_state(doc["area"].as<int>());
      break;
    case proto::MB_RAIN_CFG:
      if (rain_delay_sensor_ && doc["rain_delay"].is<int>())
        rain_delay_sensor_->publish_state(doc["rain_delay"].as<int>());
      break;
    case proto::MB_LIGHT:
      if (light_level_sensor_ && doc["lv"].is<int>())
        light_level_sensor_->publish_state(doc["lv"].as<int>());
      break;
    case proto::MB_HW_VERSIONS:
      ESP_LOGI(TAG, "Versions: MB hv=%d sv=%d, BB hv=%d sv=%d, DB hv=%d sv=%d", doc["mb_hv"] | 0,
               doc["mb_sv"] | 0, doc["bb_hv"] | 0, doc["bb_sv"] | 0, doc["db_hv"] | 0, doc["db_sv"] | 0);
      break;
    case proto::MB_WIFI_ACK:
    case proto::MB_BT_ACK:
      if (!doc["result"].as<bool>())
        ESP_LOGW(TAG, "MB rejected our %s status: %s", cmd == proto::MB_WIFI_ACK ? "WiFi" : "BT", rx_buf_);
      break;
    case proto::MB_LOG:
      ESP_LOGI(TAG, "MB log: %s", doc["log"] | "");
      break;
    case proto::MB_SHUTDOWN:
      ESP_LOGI(TAG, "MB shutting down");
      set_display_text("byE ");
      shutdown_pending_ = true;
      shutdown_start_ms_ = millis();
      break;
    default:
      break;
  }
}

void SnkMower::handle_status(const JsonDocument &doc) {
  // Fields arrive in partial updates; only publish what is present.
  auto publish = [&doc](const char *key, sensor::Sensor *s) {
    if (s != nullptr && doc[key].is<int>())
      s->publish_state(doc[key].as<int>());
  };
  publish("error", error_code_sensor_);
  publish("bat_per", battery_level_sensor_);
  publish("rain_delay", rain_delay_sensor_);
  publish("bat_health", bat_health_sensor_);
  publish("work_area", work_area_sensor_);
  publish("cut_area", cut_area_sensor_);
  publish("total_minutes", total_minutes_sensor_);
  publish("on_minutes", on_minutes_sensor_);

  if (doc["state"].is<int>())
    state_ = doc["state"];
  if (doc["error"].is<int>()) {
    error_code_ = doc["error"];
  } else if (doc["state"].is<int>() && state_ != 7) {
    // U13 only sends "error" with state 7, so any other state means no
    // error. Publish 0 so the sensor is never left unknown.
    error_code_ = 0;
    if (error_code_sensor_ && (!error_code_sensor_->has_state() || error_code_sensor_->state != 0))
      error_code_sensor_->publish_state(0);
  }
  if (doc["bat_per"].is<int>())
    battery_percent_ = doc["bat_per"];
  if (doc["station"].is<bool>())
    station_ = doc["station"];

  MowerState s;
  // In the station and not doing anything: before the PIN (0, 1), right after
  // it (2) or stopped (6).
  if (station_ && (state_ == 0 || state_ == 1 || state_ == 2 || state_ == 6)) {
    s = MowerState::DOCKED;
  } else {
    switch (state_) {
      // 2 is not mowing: in every capture and on the mower it appears only
      // right after the PIN result, followed by 0x41000003 and state 6.
      case 8:  // mowing, sent right after 0x41000005 (departure)
        s = MowerState::MOWING;
        break;
      case 16:  // edge trim: 0x10000015 from the station, answered by 0x41000013
        s = MowerState::TRIMMING;
        break;
      case 9:  // returning (captures/10-dock-charge)
        s = MowerState::RETURNING;
        break;
      case 10:
        s = MowerState::CHARGING;
        break;
      case 7:
        s = MowerState::ERROR_STATE;
        break;
      case 11:
        s = MowerState::UNKNOWN;
        break;
      default:
        s = MowerState::IDLE;
        break;
    }
  }

  if (s != current_state_ && (s == MowerState::ERROR_STATE || s == MowerState::MOWING || s == MowerState::TRIMMING))
    buzz(s == MowerState::ERROR_STATE ? 300 : 100);
  publish_mower_state(s);

  ESP_LOGD(TAG, "Status: state=%d bat=%d%% error=%d station=%d", state_, battery_percent_, error_code_, station_);
}

void SnkMower::handle_device_info(const JsonDocument &doc) {
  const char *name = doc["name"] | "";
  ESP_LOGI(TAG, "Device: %s (%s) S/N=%s v=%d", name, doc["model"] | "", doc["sn"] | "", doc["version"] | 0);
  if (device_name_sensor_)
    device_name_sensor_->publish_state(name);
  if (model_sensor_)
    model_sensor_->publish_state(doc["model"] | "");
  if (serial_sensor_)
    serial_sensor_->publish_state(doc["sn"] | "");
  if (firmware_version_sensor_)
    firmware_version_sensor_->publish_state(to_string(doc["version"] | 0));
  if (battery_name_sensor_ && doc["bat_name"].is<const char *>())
    battery_name_sensor_->publish_state(doc["bat_name"].as<const char *>());
}

void SnkMower::handle_pin_result(const JsonDocument &doc) {
  // {"result":1}; as<bool>() also accepts true.
  if (doc["result"].as<bool>()) {
    ESP_LOGI(TAG, "PIN accepted");
    pin_retries_ = 0;
    publish_mower_state(MowerState::IDLE);
    return;
  }
  if (++pin_retries_ >= 5) {
    ESP_LOGE(TAG, "PIN rejected %d times, giving up", pin_retries_);
    publish_mower_state(MowerState::LOCKED);
    return;
  }
  ESP_LOGW(TAG, "PIN rejected (attempt %d), retrying", pin_retries_);
  pin_sent_ = false;
}

void SnkMower::handle_error_notify(const JsonDocument &doc) {
  if (doc["err"].is<int>()) {
    error_code_ = doc["err"];
    ESP_LOGW(TAG, "Error code %d", error_code_);
    if (error_code_sensor_)
      error_code_sensor_->publish_state(error_code_);
  }
  buzz(300);
  publish_mower_state(MowerState::ERROR_STATE);
}

}  // namespace snk_mower
}  // namespace esphome
