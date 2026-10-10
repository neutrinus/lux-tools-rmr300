#include "snk_mower.h"
#include "protocol.h"
#include "esphome/core/log.h"
#include <esp_mac.h>

namespace esphome {
namespace snk_mower {

static const char *const TAG = "snk_mower";

// U13 drops the display link after 3 s without any frame from us (dpport
// receive timeout, 0x080706a0). The guard steps in after 1.5 s of silence.
static constexpr uint32_t LINK_GUARD_PERIOD_MS = 500;
static constexpr uint32_t LINK_GUARD_SILENCE_MS = 1500;
// Intervals measured on the original firmware (captures/02-boot-pin).
static constexpr uint32_t POLL_INTERVAL_MS = 100;
static constexpr uint32_t KEEPALIVE_INTERVAL_MS = 500;
static constexpr uint32_t WIFI_STATUS_INTERVAL_MS = 1000;
static constexpr uint32_t KEY_CONFIRM_WINDOW_MS = 3000;  // 300 ticks of 10 ms in the original
static constexpr uint32_t STATS_INTERVAL_MS = 10000;

void SnkMower::setup() {
  ESP_LOGI(TAG, "Setting up SNK mower link (230400 8N1)");
  tx_mutex_ = xSemaphoreCreateMutex();

  if (buzzer_pin_ != GPIO_NUM_NC) {
    gpio_set_direction(buzzer_pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(buzzer_pin_, 0);
  }
  if (rain_pin_ != GPIO_NUM_NC)
    setup_rain();

  setup_display();
  set_display_text("boot");
  last_activity_ms_ = millis();

  // Prebuilt so the guard task never touches ArduinoJson.
  char json[32];
  int n = snprintf(json, sizeof(json), "{\"cmd\":%lu}", (unsigned long) proto::ESP_KEEPALIVE);
  keepalive_frame_len_ = proto::encode_frame(json, n, keepalive_frame_, sizeof(keepalive_frame_));

  esp_timer_create_args_t args = {
      .callback = &SnkMower::link_guard_callback,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "snk_link_guard",
      .skip_unhandled_events = true,
  };
  if (esp_timer_create(&args, &link_guard_timer_) == ESP_OK)
    esp_timer_start_periodic(link_guard_timer_, LINK_GUARD_PERIOD_MS * 1000ULL);
  else
    ESP_LOGE(TAG, "Link guard timer create failed");

  // Same opening as the original firmware. If U13 is already running (ESP
  // restart after OTA), any normal frame from it moves us to UP.
  send_cmd(proto::ESP_BOOT);
  send_cmd(proto::ESP_KEEPALIVE);
  JsonDocument doc;
  doc["cmd"] = proto::ESP_STATE;
  doc["state"] = 0;
  send_json(doc);
  send_rain_status();
}

void SnkMower::loop() {
  uint32_t now = millis();
  read_uart();
  link_loop(now);
  if (rain_adc_ != nullptr)
    rain_loop(now);
  display_loop(now);
  if (now - last_stats_ms_ >= STATS_INTERVAL_MS)
    log_link_stats(now);
}

void SnkMower::log_link_stats(uint32_t now) {
  static const char *const LINK_NAMES[] = {"WAIT_MB", "HANDSHAKE", "UP"};
  last_stats_ms_ = now;
  if (last_rx_ms_ == 0)
    ESP_LOGD(TAG, "Link %s: nothing received yet, tx %u frames", LINK_NAMES[static_cast<int>(link_)],
             (unsigned) tx_frames_);
  else
    ESP_LOGD(TAG, "Link %s: rx %u frames (%u bad, %u bytes), last %u ms ago; tx %u frames; state=%d locked-pin=%s",
             LINK_NAMES[static_cast<int>(link_)], (unsigned) rx_frames_, (unsigned) rx_bad_, (unsigned) rx_bytes_,
             (unsigned) (now - last_rx_ms_), (unsigned) tx_frames_, state_, pin_retries_ >= 5 ? "gave up" : "ok");
}

// ── Link ──────────────────────────────────────────────────────────

void SnkMower::link_loop(uint32_t now) {
  if (link_ != Link::UP) {
    if (now - last_poll_ >= POLL_INTERVAL_MS) {
      last_poll_ = now;
      send_cmd(proto::ESP_POLL);
    }
    return;
  }

  if (now - last_keepalive_ >= KEEPALIVE_INTERVAL_MS) {
    last_keepalive_ = now;
    send_cmd(proto::ESP_KEEPALIVE);
  }
  if (now - last_wifi_status_ >= WIFI_STATUS_INTERVAL_MS) {
    last_wifi_status_ = now;
    send_wifi_status();
  }
  if (!pin_sent_)
    send_pin();
}

void SnkMower::link_up(const char *why) {
  if (link_ == Link::UP)
    return;
  ESP_LOGI(TAG, "Link up (%s)", why);
  link_ = Link::UP;
  uint32_t now = millis();
  link_up_ms_ = now;
  last_keepalive_ = now;
  last_wifi_status_ = now;
  // The original queries settings once the link is up. Bare commands are
  // queries; ESP_GET_SCHEDULE with fields would overwrite the mower's schedule.
  send_rain_status();
  send_cmd(proto::ESP_GET_SCHEDULE);
  send_cmd(proto::ESP_GET_RAIN_CFG);
  send_cmd(proto::ESP_GET_ZONES);
}

void SnkMower::link_guard_callback(void *arg) {
  auto *self = static_cast<SnkMower *>(arg);
  // loop() normally sends something every 100-500 ms. This only fires while
  // loop() is blocked, e.g. during an OTA upload.
  if (millis() - self->last_tx_ms_ < LINK_GUARD_SILENCE_MS)
    return;
  self->write_frame(self->keepalive_frame_, self->keepalive_frame_len_);
}

// ── TX ────────────────────────────────────────────────────────────

void SnkMower::write_frame(const uint8_t *data, size_t len) {
  // loop() and the link guard task both write; never interleave frames.
  xSemaphoreTake(tx_mutex_, portMAX_DELAY);
  write_array(data, len);
  last_tx_ms_ = millis();
  tx_frames_++;
  xSemaphoreGive(tx_mutex_);
}

void SnkMower::send_json(const JsonDocument &doc) {
  char json[BUF_SIZE];
  size_t n = serializeJson(doc, json, sizeof(json));
  uint8_t frame[BUF_SIZE + 3];
  size_t len = proto::encode_frame(json, n, frame, sizeof(frame));
  if (n == 0 || len == 0) {
    ESP_LOGE(TAG, "Frame too long, not sent");
    return;
  }
  write_frame(frame, len);
  uint32_t cmd = doc["cmd"] | 0;
  if (cmd == proto::ESP_POLL || cmd == proto::ESP_KEEPALIVE || cmd == proto::ESP_WIFI || cmd == proto::ESP_BT)
    ESP_LOGV(TAG, "TX %s", json);
  else
    ESP_LOGD(TAG, "TX %s", json);
}

void SnkMower::send_cmd(uint32_t cmd) {
  JsonDocument doc;
  doc["cmd"] = cmd;
  send_json(doc);
}

void SnkMower::send_esp_info() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02x-%02x-%02x-%02x-%02x-%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
  JsonDocument doc;
  doc["cmd"] = proto::ESP_INFO;
  doc["hv"] = proto::DISPLAY_HW_VERSION;
  doc["sv"] = proto::DISPLAY_SW_VERSION;
  doc["spw"] = 0;
  doc["mac"] = mac_str;
  send_json(doc);
}

void SnkMower::send_init() {
  JsonDocument doc;
  doc["cmd"] = proto::ESP_INIT;
  doc["init"] = 3;
  send_json(doc);
}

void SnkMower::send_pin() {
  JsonDocument doc;
  doc["cmd"] = proto::ESP_PIN;
  doc["pwd"] = atoi(pin_.c_str());
  send_json(doc);
  pin_sent_ = true;
}

void SnkMower::send_wifi_status() {
  // Always "no WiFi / no BT", like the original without a cloud connection.
  JsonDocument doc;
  doc["cmd"] = proto::ESP_WIFI;
  doc["wifi"] = 0;
  doc["str"] = 0;
  send_json(doc);
  doc.clear();
  doc["cmd"] = proto::ESP_BT;
  doc["bt"] = 0;
  doc["str"] = 0;
  send_json(doc);
}

void SnkMower::send_rain_status() {
  // 1 = dry, 2 = raining (U13 service_rain, 0x08039198, only reacts to 2).
  JsonDocument doc;
  doc["cmd"] = proto::ESP_RAIN;
  doc["rain"] = rain_state_;
  send_json(doc);
}

void SnkMower::send_raw_json(const std::string &json) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    ESP_LOGE(TAG, "Raw JSON parse error: %s", err.c_str());
    return;
  }
  ESP_LOGI(TAG, "Sending raw JSON: %s", json.c_str());
  send_json(doc);
}

// ── Actions and keys ──────────────────────────────────────────────

void SnkMower::start_mowing() {
  // Same as a user pressing START, then OK.
  ESP_LOGI(TAG, "Start mowing");
  send_cmd(proto::KEY_SELECT);
  set_timeout("key_confirm", 500, [this]() { send_cmd(proto::KEY_START_CONFIRM); });
}

void SnkMower::return_to_dock() {
  // Same as a user pressing HOME, then OK.
  ESP_LOGI(TAG, "Return to dock");
  send_cmd(proto::KEY_SELECT);
  set_timeout("key_confirm", 500, [this]() { send_cmd(proto::KEY_HOME_CONFIRM); });
}

void SnkMower::stop_mowing() {
  ESP_LOGI(TAG, "Stop");
  send_cmd(proto::REMOTE_STOP);
}

void SnkMower::trim_edge() {
  // U13 ignores this outside the station ("trim command, but robot not in station, ignore").
  ESP_LOGI(TAG, "Edge trim");
  send_cmd(proto::REMOTE_EDGE);
}

// Front buttons: START=GPIO22, HOME=GPIO21, OK=GPIO19, active low. Like the
// original, START/HOME sends KEY_SELECT and opens a 3 s window for OK.
void SnkMower::arm_key(uint8_t key) {
  armed_key_ = key;
  armed_at_ms_ = millis();
  buzz(20);
  send_cmd(proto::KEY_SELECT);
}

void SnkMower::key_ok() {
  bool in_window = armed_key_ != KEY_NONE && millis() - armed_at_ms_ < KEY_CONFIRM_WINDOW_MS;
  uint8_t key = armed_key_;
  armed_key_ = KEY_NONE;
  if (!in_window) {
    ESP_LOGD(TAG, "OK without START/HOME before it, ignored");
    return;
  }
  buzz(20);
  send_cmd(key == KEY_START ? proto::KEY_START_CONFIRM : proto::KEY_HOME_CONFIRM);
}

}  // namespace snk_mower
}  // namespace esphome
