#include "snk_mower.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/application.h"

namespace esphome {
namespace snk_mower {

static const char *const TAG = "snk_mower";

static const uint32_t CMD_POWER_ON       = 0x20000001;
static const uint32_t CMD_POWER_READY    = 0x20000004;
static const uint32_t CMD_RAIN           = 0x22000000;
static const uint32_t CMD_ESP_KEEPALIVE  = 0x30000005;
static const uint32_t CMD_SETTING_MODE   = 0x30000006;
static const uint32_t CMD_SETTING_APPLY  = 0x30000007;
static const uint32_t CMD_ESP_STATE      = 0x30000028;
static const uint32_t CMD_ESP_WIFI       = 0x30000021;
static const uint32_t CMD_ESP_BT         = 0x30000022;
static const uint32_t CMD_ESP_POLL       = 0x300000A1;
static const uint32_t CMD_ESP_TRIM       = 0x300000A6;
static const uint32_t CMD_ESP_RAIN_CFG   = 0x300000A7;
static const uint32_t CMD_ESP_MULTIZONE  = 0x300000A8;
static const uint32_t CMD_SETTING_START  = 0x31000016;
static const uint32_t CMD_SETTING_SUB    = 0x31000017;
static const uint32_t CMD_PIN_RESULT     = 0x33000021;
static const uint32_t CMD_PIN_RESULT2    = 0x33000022;
static const uint32_t CMD_STATUS         = 0x330000A0;
static const uint32_t CMD_DEVICE_INFO    = 0x330000A1;
static const uint32_t CMD_HW_VERSIONS    = 0x330000A2;
static const uint32_t CMD_SCHEDULE       = 0x330000A6;
static const uint32_t CMD_RAIN_CFG_RSP   = 0x330000A7;
static const uint32_t CMD_MULTIZONE_RSP  = 0x330000A8;
static const uint32_t CMD_SCHEDULE_END   = 0x330000AA;
static const uint32_t CMD_MAP_CFG        = 0x330000B0;
static const uint32_t CMD_ESP_BOOT       = 0x40000004;
static const uint32_t CMD_ESP_INIT       = 0x40000001;
static const uint32_t CMD_ESP_INFO       = 0x40000006;
static const uint32_t CMD_BOOT_INIT      = 0x40000008;
static const uint32_t CMD_BOOT_HEART     = 0x40000009;
static const uint32_t CMD_RTC            = 0x40000011;
static const uint32_t CMD_START_TIME_Q   = 0x40000012;
static const uint32_t CMD_CUT_TIME_Q     = 0x40000013;
static const uint32_t CMD_UNKNOWN_14     = 0x40000014;
static const uint32_t CMD_LIGHT          = 0x40000020;
static const uint32_t CMD_BOOT_ACK       = 0x40000021;
static const uint32_t CMD_LOCK           = 0x41000002;
static const uint32_t CMD_EXEC_ACTION    = 0x41000003;
static const uint32_t CMD_ERROR_NOTIFY   = 0x41000004;
static const uint32_t CMD_PIN_SEND       = 0x41000005;
static const uint32_t CMD_SHUTDOWN       = 0x41000008;
static const uint32_t CMD_START_ACK      = 0x41000020;
static const uint32_t CMD_BATTERY        = 0x50000021;
static const uint32_t CMD_RETURN_HOME    = 0x10000001;
static const uint32_t CMD_ERR_ACK2       = 0x10000002;
static const uint32_t CMD_ERR_ACK7       = 0x10000007;

static const uint32_t CMD_SETTING_ACK_BASE = 0x33000000;

static const char *const STATE_NAMES[] = {
    "unknown", "idle",    "mowing", "returning",
    "charging", "docked", "error",  "locked",
};

static const char *const STATE_DISPLAY[] = {
    "IdLE", "IdLE", "Mow ", "HoME",
    "ChAr", "IdLE", "Err ", "LoCK",
};

static const float VOLTAGE_LUT[101] = {
    15.0f, 15.05f, 15.1f, 15.15f, 15.2f, 15.25f, 15.3f, 15.35f, 15.4f, 15.5f,
    15.6f, 15.7f,  15.8f, 15.85f, 15.9f, 16.0f,  16.1f, 16.15f, 16.2f, 16.25f,
    16.3f, 16.4f,  16.5f, 16.55f, 16.6f, 16.65f, 16.7f, 16.75f, 16.8f, 16.85f,
    16.9f, 16.95f, 17.0f, 17.05f, 17.1f, 17.15f, 17.2f, 17.25f, 17.3f, 17.35f,
    17.4f, 17.45f, 17.5f, 17.55f, 17.6f, 17.65f, 17.7f, 17.75f, 17.8f, 17.85f,
    17.9f, 17.95f, 18.0f, 18.05f, 18.1f, 18.15f, 18.2f, 18.25f, 18.3f, 18.35f,
    18.4f, 18.45f, 18.5f, 18.55f, 18.6f, 18.65f, 18.7f, 18.75f, 18.8f, 18.85f,
    18.9f, 18.95f, 19.0f, 19.05f, 19.1f, 19.15f, 19.2f, 19.25f, 19.3f, 19.35f,
    19.4f, 19.45f, 19.5f, 19.55f, 19.6f, 19.65f, 19.7f, 19.75f, 19.8f, 19.85f,
    19.9f, 19.95f, 20.0f, 20.05f, 20.1f, 20.15f, 20.2f, 20.25f, 20.3f, 20.35f,
    20.4f,
};

static int voltage_to_percent(float v) {
  if (v >= VOLTAGE_LUT[100]) return 100;
  if (v <= VOLTAGE_LUT[0]) return 0;
  for (int i = 0; i < 100; i++) {
    if (v >= VOLTAGE_LUT[i] && v < VOLTAGE_LUT[i + 1]) return i;
  }
  return 50;
}

static void shift24(gpio_num_t clk, gpio_num_t mosi, gpio_num_t cs,
                    uint8_t b0, uint8_t b1, uint8_t b2) {
  gpio_set_level(cs, 0);
  uint8_t bytes[] = {b0, b1, b2};
  for (int b = 0; b < 3; b++) {
    for (int i = 7; i >= 0; i--) {
      gpio_set_level(mosi, (bytes[b] >> i) & 1);
      gpio_set_level(clk, 1);
      gpio_set_level(clk, 0);
    }
  }
  gpio_set_level(cs, 1);
}

SnkMower::SnkMower(const std::string &pin) : pin_(pin) {}

void SnkMower::setup() {
  ESP_LOGI(TAG, "SNK Mower starting (PIN: %s, JSON at 230400)", pin_.c_str());
  last_activity_ms_ = millis();

  if (buzzer_pin_ != GPIO_NUM_NC) {
    gpio_set_direction(buzzer_pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(buzzer_pin_, 0);
    ESP_LOGI(TAG, "Buzzer on GPIO%d", (int)buzzer_pin_);
  }

  if (rain_pin_ != GPIO_NUM_NC) {
    gpio_set_direction(rain_pin_, GPIO_MODE_INPUT);
    ESP_LOGI(TAG, "Rain sensor on GPIO%d", (int)rain_pin_);
  }

  setup_display();
  set_display_text("----");
}

void SnkMower::set_display_pins(uint8_t clk, uint8_t mosi, uint8_t cs) {
  display_clk_ = (gpio_num_t)clk;
  display_mosi_ = (gpio_num_t)mosi;
  display_cs_ = (gpio_num_t)cs;
}

void SnkMower::setup_display() {
  if (display_clk_ == GPIO_NUM_NC) {
    ESP_LOGW(TAG, "Display pins not configured, skipping display init");
    return;
  }
  gpio_set_direction(display_clk_, GPIO_MODE_OUTPUT);
  gpio_set_direction(display_mosi_, GPIO_MODE_OUTPUT);
  gpio_set_direction(display_cs_, GPIO_MODE_OUTPUT);
  gpio_set_level(display_clk_, 0);
  gpio_set_level(display_mosi_, 0);
  gpio_set_level(display_cs_, 1);
  ESP_LOGI(TAG, "Display initialized (CLK=%d, MOSI=%d, CS=%d)",
           (int)display_clk_, (int)display_mosi_, (int)display_cs_);
}

void SnkMower::refresh_display() {
  if (display_clk_ == GPIO_NUM_NC) return;
  if (display_off_) return;

  uint32_t now = millis();
  if (now - last_display_ms_ < DISPLAY_REFRESH_MS) return;
  last_display_ms_ = now;

  uint8_t seg = display_segments_[current_digit_];

  if (current_state_ == MowerState::CHARGING && current_digit_ == 0) {
    if (now - last_charging_frame_ms_ >= CHG_FRAME_MS) {
      last_charging_frame_ms_ = now;
      charging_frame_ = (charging_frame_ + 1) % 3;
    }
    seg = CHG_FRAMES[charging_frame_];
  }

  uint8_t dig = 1 << current_digit_;
  current_digit_ = (current_digit_ + 1) % DIGITS;

  shift24(display_clk_, display_mosi_, display_cs_,
          0x00,
          display_colon_ | dig,
          seg);
}

void SnkMower::set_display_text(const char *text, bool colon) {
  for (uint8_t i = 0; i < DIGITS; i++) {
    char c = text[i] ? text[i] : ' ';
    display_segments_[i] = char_to_segments_(c);
  }
  display_colon_ = colon ? 0b00110000 : 0;
}

void SnkMower::set_display_battery(int percent) {
  char buf[5];
  percent = std::min(100, std::max(0, percent));
  if (percent == 100) {
    buf[0] = 'b', buf[1] = '1', buf[2] = '0', buf[3] = '0';
  } else {
    buf[0] = 'b', buf[1] = '0' + (percent / 10);
    buf[2] = '0' + (percent % 10), buf[3] = ' ';
  }
  set_display_text(buf);
}

void SnkMower::set_charging_display(int percent) {
  display_segments_[0] = 0;
  percent = std::min(100, std::max(0, percent));
  if (percent == 100) {
    display_segments_[1] = char_to_segments_('1');
    display_segments_[2] = char_to_segments_('0');
    display_segments_[3] = char_to_segments_('0');
  } else {
    display_segments_[1] = char_to_segments_('0' + (percent / 10));
    display_segments_[2] = char_to_segments_('0' + (percent % 10));
    display_segments_[3] = char_to_segments_(' ');
  }
}

uint8_t SnkMower::char_to_segments_(char c) {
  switch (c) {
    case ' ': return 0b00000000;
    case '-': return 0b01000000;
    case '_': return 0b00001000;
    case '0': return 0b00111111;
    case '1': return 0b00000110;
    case '2': return 0b01011011;
    case '3': return 0b01001111;
    case '4': return 0b01100110;
    case '5': return 0b01101101;
    case '6': return 0b01111101;
    case '7': return 0b00000111;
    case '8': return 0b01111111;
    case '9': return 0b01101111;
    case 'A': return 0b01110111;
    case 'b': return 0b01111100;
    case 'C': return 0b00111001;
    case 'c': return 0b01011000;
    case 'd': return 0b01011110;
    case 'E': return 0b01111001;
    case 'F': return 0b01110001;
    case 'H': return 0b01110110;
    case 'h': return 0b01110100;
    case 'I': return 0b00110000;
    case 'J': return 0b00011110;
    case 'L': return 0b00111000;
    case 'n': return 0b01010100;
    case 'o': return 0b01011100;
    case 'P': return 0b01110011;
    case 'r': return 0b01010000;
    case 'S': return 0b01101101;
    case 't': return 0b01111000;
    case 'U': return 0b00111110;
    case 'u': return 0b00011100;
    default: return 0;
  }
}

void SnkMower::set_buzzer_pin(uint8_t pin) {
  buzzer_pin_ = (gpio_num_t)pin;
}

void SnkMower::set_rain_pin(uint8_t pin) {
  rain_pin_ = (gpio_num_t)pin;
}

void SnkMower::set_display_off_timeout(uint32_t minutes) {
  display_off_timeout_ms_ = minutes * 60000UL;
  ESP_LOGI(TAG, "Display auto-off: %u min (%u ms)",
           (unsigned)minutes, (unsigned)display_off_timeout_ms_);
}

void SnkMower::buzz(int duration_ms) {
  if (buzzer_pin_ == GPIO_NUM_NC) return;
  gpio_set_level(buzzer_pin_, 1);
  delay(duration_ms);
  gpio_set_level(buzzer_pin_, 0);
}

void SnkMower::send_json(const JsonDocument &doc) {
  size_t n = serializeJson(doc, tx_buf_, BUF_SIZE);
  if (n > 0) {
    write_array((const uint8_t *)tx_buf_, n);
    ESP_LOGD(TAG, "TX: %s", tx_buf_);
  }
}

void SnkMower::send_boot() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = CMD_ESP_BOOT;
  send_json(doc);
}

void SnkMower::send_init() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = CMD_ESP_INIT;
  doc["init"] = 3;
  send_json(doc);
}

void SnkMower::send_pin() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = CMD_PIN_SEND;
  doc["pwd"] = atoi(pin_.c_str());
  send_json(doc);
  pin_sent_ = true;
}

void SnkMower::send_keepalive() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = CMD_ESP_KEEPALIVE;
  send_json(doc);
}

void SnkMower::send_wifi_status() {
  StaticJsonDocument<64> doc;
  bool wifi_connected = (WiFi.status() == WL_CONNECTED);
  int wifi_str = wifi_connected ? 1 : 0;

  doc["cmd"] = CMD_ESP_WIFI;
  doc["wifi"] = wifi_str;
  doc["str"] = wifi_str;
  send_json(doc);

  doc.clear();
  doc["cmd"] = CMD_ESP_BT;
  doc["bt"] = 0;
  doc["str"] = 0;
  send_json(doc);
}

void SnkMower::send_esp_info() {
  StaticJsonDocument<128> doc;
  doc["cmd"] = CMD_ESP_INFO;
  doc["hv"] = 60400;
  doc["sv"] = 30202;
  doc["spw"] = 0;
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02x-%02x-%02x-%02x-%02x-%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  doc["mac"] = mac_str;
  send_json(doc);
}

void SnkMower::send_error_ack() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = CMD_ERR_ACK7;
  send_json(doc);
}

void SnkMower::send_return_home() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = CMD_RETURN_HOME;
  send_json(doc);
}

void SnkMower::send_esp_state(int state) {
  StaticJsonDocument<64> doc;
  doc["cmd"] = CMD_ESP_STATE;
  doc["state"] = state;
  send_json(doc);
}

void SnkMower::send_rain_status(int rain) {
  StaticJsonDocument<64> doc;
  doc["cmd"] = CMD_RAIN;
  doc["rain"] = rain;
  send_json(doc);
}

void SnkMower::read_rain_sensor() {
  if (rain_pin_ == GPIO_NUM_NC) return;
  int rain = gpio_get_level(rain_pin_);
  send_rain_status(rain);
}

void SnkMower::loop() {
  uint32_t now = millis();

  while (available() > 0) {
    uint8_t byte;
    read_byte(&byte);

    if (byte == '{') {
      rx_index_ = 0;
      rx_in_json_ = true;
    }

    if (rx_in_json_) {
      if (rx_index_ < BUF_SIZE - 1) {
        rx_buf_[rx_index_++] = (char)byte;
      }

      if (byte == '}') {
        rx_buf_[rx_index_] = '\0';
        rx_in_json_ = false;

        StaticJsonDocument<512> doc;
        DeserializationError err = deserializeJson(doc, rx_buf_);
        if (!err && doc.containsKey("cmd")) {
          handle_json(doc);
        }
      }
    }
  }

  if (!boot_sent_ && now > 100) {
    boot_sent_ = true;
    send_boot();
    delay(50);
    send_init();
    delay(50);
    send_esp_info();
    send_esp_state(0);
  }

  if (!pin_sent_ && boot_sent_ && power_ready_ && now > 2000) {
    send_pin();
  }

  if (now - last_keepalive_ > 200) {
    last_keepalive_ = now;
    send_keepalive();
  }

  if (now - last_wifi_status_ > 5000) {
    last_wifi_status_ = now;
    send_wifi_status();
  }

  if (now - last_esp_info_ > 30000) {
    last_esp_info_ = now;
    send_esp_info();
  }

  if (now - last_esp_state_ > 10000) {
    last_esp_state_ = now;
    send_esp_state(state_);
  }

  if (now - last_rain_read_ > 60000) {
    last_rain_read_ = now;
    read_rain_sensor();
  }

  if (display_off_timeout_ms_ > 0 && !display_off_ &&
      current_state_ != MowerState::MOWING &&
      current_state_ != MowerState::CHARGING &&
      current_state_ != MowerState::RETURNING &&
      current_state_ != MowerState::ERROR_STATE &&
      current_state_ != MowerState::LOCKED &&
      now - last_activity_ms_ > display_off_timeout_ms_) {
    ESP_LOGD(TAG, "Display auto-off (idle %ums)", (unsigned)(now - last_activity_ms_));
    display_off_ = true;
  }

  refresh_display();
}

void SnkMower::handle_json(const JsonDocument &doc) {
  uint32_t cmd = doc["cmd"];

  switch (cmd) {
    case CMD_PIN_RESULT:       handle_pin_result(doc); break;
    case CMD_PIN_RESULT2:      handle_pin_result2(doc); break;
    case CMD_STATUS:           handle_status(doc); break;
    case CMD_ERROR_NOTIFY:     handle_error_notify(doc); break;
    case CMD_RTC:              handle_rtc(doc); break;
    case CMD_DEVICE_INFO:      handle_device_info(doc); break;
    case CMD_HW_VERSIONS:      handle_hw_versions(doc); break;
    case CMD_BATTERY:          handle_battery_info(doc); break;
    case CMD_MAP_CFG:          handle_map_cfg(doc); break;
    case CMD_SCHEDULE:         handle_schedule(doc); break;
    case CMD_SCHEDULE_END:     handle_schedule_end(doc); break;
    case CMD_RAIN_CFG_RSP:     handle_rain_cfg(doc); break;
    case CMD_MULTIZONE_RSP:    handle_multizone(doc); break;
    case CMD_LIGHT:            handle_light(doc); break;
    case CMD_POWER_ON:         handle_power_on(doc); break;
    case CMD_POWER_READY:      handle_power_ready(doc); break;
    case CMD_BOOT_HEART:       handle_boot_heart(doc); break;
    case CMD_BOOT_INIT:        handle_boot_init(doc); break;
    case CMD_LOCK:             handle_lock(doc); break;
    case CMD_START_ACK:        handle_start_ack(doc); break;
    case CMD_EXEC_ACTION:      handle_exec_action(doc); break;
    case CMD_SHUTDOWN:         handle_shutdown(doc); break;
    case CMD_BOOT_ACK:         ESP_LOGD(TAG, "Boot ACK"); break;
    case CMD_START_TIME_Q:     handle_start_time_query(doc); break;
    case CMD_CUT_TIME_Q:       handle_cut_time_query(doc); break;
    case CMD_UNKNOWN_14:       ESP_LOGV(TAG, "Unknown 0x40000014"); break;
    default:
      if ((cmd & 0xFFFFFF00) == CMD_SETTING_ACK_BASE && (cmd & 0xFF) >= 0x09 && (cmd & 0xFF) <= 0x27) {
        handle_setting_ack(doc, cmd);
      } else {
        ESP_LOGD(TAG, "RX: 0x%08lX", (unsigned long)cmd);
      }
      break;
  }
}

void SnkMower::handle_status(const JsonDocument &doc) {
  if (doc.containsKey("state")) {
    state_ = doc["state"];
  }
  if (doc.containsKey("error")) {
    error_code_ = doc["error"];
    if (error_code_sensor_)
      error_code_sensor_->publish_state(error_code_);
  }
  if (doc.containsKey("bat_lv")) {
    bat_lv_ = doc["bat_lv"];
    if (bat_level_bars_sensor_)
      bat_level_bars_sensor_->publish_state(bat_lv_);
  }
  if (doc.containsKey("bat_per")) {
    bat_per_ = doc["bat_per"];
    last_battery_percent_ = bat_per_;
    if (battery_level_sensor_)
      battery_level_sensor_->publish_state(bat_per_);
  }
  if (doc.containsKey("rain_delay")) {
    rain_delay = doc["rain_delay"];
    if (rain_delay_sensor_)
      rain_delay_sensor_->publish_state(rain_delay);
  }
  if (doc.containsKey("rain_state")) {
    rain_state = doc["rain_state"];
  }
  if (doc.containsKey("bat_health")) {
    bat_health = doc["bat_health"];
    if (bat_health_sensor_)
      bat_health_sensor_->publish_state(bat_health);
  }
  if (doc.containsKey("work_area")) {
    work_area_ = doc["work_area"];
    if (work_area_sensor_)
      work_area_sensor_->publish_state(work_area_);
  }
  if (doc.containsKey("cut_area")) {
    cut_area_ = doc["cut_area"];
    if (cut_area_sensor_)
      cut_area_sensor_->publish_state(cut_area_);
  }
  if (doc.containsKey("total_minutes")) {
    total_minutes_ = doc["total_minutes"];
    if (total_minutes_sensor_)
      total_minutes_sensor_->publish_state(total_minutes_);
  }
  if (doc.containsKey("on_minutes")) {
    on_minutes_ = doc["on_minutes"];
    if (on_minutes_sensor_)
      on_minutes_sensor_->publish_state(on_minutes_);
  }
  if (doc.containsKey("bat_ctime")) {
    bat_ctime_ = doc["bat_ctime"];
  }
  if (doc.containsKey("bat_dtime")) {
    bat_dtime_ = doc["bat_dtime"];
  }
  if (doc.containsKey("cur_minutes")) {
    cur_minutes_ = doc["cur_minutes"];
  }
  if (doc.containsKey("bat_min_temp")) {
    bat_min_temp_ = doc["bat_min_temp"];
  }

  station_ = doc["station"] | false;

  MowerState s;
  if (station_ && (state_ == 0 || state_ == 1)) {
    s = MowerState::DOCKED;
  } else {
    switch (state_) {
      case 2:  s = MowerState::MOWING; break;
      case 6:  s = MowerState::RETURNING; break;
      case 7:  s = MowerState::ERROR_STATE; break;
      case 11: s = MowerState::CHARGING; break;
      default: s = MowerState::IDLE; break;
    }
  }

  if (s != current_state_) {
    if (s == MowerState::ERROR_STATE)
      buzz(300);
    if (s == MowerState::MOWING)
      buzz(100);
  }

  publish_mower_state(s);

  ESP_LOGD(TAG, "Status: state=%d bat_lv=%d bat_per=%d error=%d station=%d area=%d",
           state_, bat_lv_, bat_per_, error_code_, station_, work_area_);
}

void SnkMower::handle_pin_result(const JsonDocument &doc) {
  bool ok = doc["result"] | false;
  if (ok) {
    ESP_LOGI(TAG, "PIN accepted");
    pin_ok_ = true;
    pin_retries_ = 0;
    buzz(80);
    publish_mower_state(MowerState::IDLE);
  } else {
    ESP_LOGW(TAG, "PIN rejected (attempt %d/5)", pin_retries_);
    if (++pin_retries_ >= 5) {
      ESP_LOGE(TAG, "PIN failed after 5 retries");
      publish_mower_state(MowerState::LOCKED);
    } else {
      pin_sent_ = false;
    }
  }
}

void SnkMower::handle_pin_result2(const JsonDocument &doc) {
  bool ok = doc["result"] | false;
  ESP_LOGD(TAG, "PIN result2: %s", ok ? "OK" : "FAIL");
}

void SnkMower::handle_error_notify(const JsonDocument &doc) {
  if (doc.containsKey("err")) {
    error_code_ = doc["err"];
    ESP_LOGW(TAG, "Error code: %d", error_code_);
    if (error_code_sensor_)
      error_code_sensor_->publish_state(error_code_);
  }
  buzz(300);
  send_error_ack();
  publish_mower_state(MowerState::ERROR_STATE);
}

void SnkMower::handle_rtc(const JsonDocument &doc) {
  if (doc.containsKey("rtc")) {
    uint32_t rtc = doc["rtc"];
    ESP_LOGV(TAG, "RTC: %u", (unsigned)rtc);
  }
}

void SnkMower::handle_device_info(const JsonDocument &doc) {
  if (doc.containsKey("name")) {
    const char *name = doc["name"];
    const char *model = doc["model"] | "";
    const char *sn = doc["sn"] | "";
    int version = doc["version"] | 0;
    int pwd_en = doc["pwd_en"] | 0;

    ESP_LOGI(TAG, "Device: %s (%s) S/N=%s v=%d pwd_en=%d",
             name, model, sn, version, pwd_en);

    if (device_name_sensor_)
      device_name_sensor_->publish_state(name);
    if (model_sensor_)
      model_sensor_->publish_state(model);
    if (serial_sensor_)
      serial_sensor_->publish_state(sn);
    if (firmware_version_sensor_) {
      char ver_str[16];
      snprintf(ver_str, sizeof(ver_str), "%d", version);
      firmware_version_sensor_->publish_state(ver_str);
    }
    if (doc.containsKey("bat_name") && battery_name_sensor_) {
      battery_name_sensor_->publish_state(doc["bat_name"] | "");
    }
  }
}

void SnkMower::handle_hw_versions(const JsonDocument &doc) {
  int mb_hv = doc["mb_hv"] | 0;
  int mb_sv = doc["mb_sv"] | 0;
  int bb_hv = doc["bb_hv"] | 0;
  int bb_sv = doc["bb_sv"] | 0;
  int db_hv = doc["db_hv"] | 0;
  int db_sv = doc["db_sv"] | 0;
  int mblt_sv = doc["mblt_sv"] | 0;

  ESP_LOGI(TAG, "HW: MB hv=%d sv=%d, BB hv=%d sv=%d, DB hv=%d sv=%d, MBLT sv=%d",
           mb_hv, mb_sv, bb_hv, bb_sv, db_hv, db_sv, mblt_sv);
}

void SnkMower::handle_battery_info(const JsonDocument &doc) {
  if (doc.containsKey("bat")) {
    int bars = doc["bat"];
    ESP_LOGD(TAG, "Battery bars: %d", bars);
    if (bat_level_bars_sensor_)
      bat_level_bars_sensor_->publish_state(bars);
  }
}

void SnkMower::handle_map_cfg(const JsonDocument &doc) {
  int area = doc["area"] | 0;
  int map_sn = doc["map_sn"] | 0;
  ESP_LOGI(TAG, "Map: area=%d m², map_sn=%d", area, map_sn);
  work_area_ = area;
  if (work_area_sensor_)
    work_area_sensor_->publish_state(area);
}

void SnkMower::handle_schedule(const JsonDocument &doc) {
  int trim = doc["trim"] | 0;
  bool auto_mode = doc["auto"] | false;
  int pause = doc["pause"] | 0;

  ESP_LOGI(TAG, "Schedule: trim=%d auto=%d pause=%d", trim, auto_mode, pause);

  const char *days[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
  for (int i = 0; i < 7; i++) {
    char st_key[8], len_key[8];
    snprintf(st_key, sizeof(st_key), "%s_st", days[i]);
    snprintf(len_key, sizeof(len_key), "%s_len", days[i]);
    if (doc.containsKey(st_key)) {
      int st = doc[st_key];
      int len = doc[len_key] | 0;
      ESP_LOGD(TAG, "  %s: start=%d min, len=%d min", days[i], st, len);
    }
  }
}

void SnkMower::handle_schedule_end(const JsonDocument &doc) {
  ESP_LOGD(TAG, "Schedule block end");
}

void SnkMower::handle_rain_cfg(const JsonDocument &doc) {
  bool rain_en = doc["rain_en"] | false;
  int delay = doc["rain_delay"] | 0;
  ESP_LOGI(TAG, "Rain config: enabled=%d, delay=%d min", rain_en, delay);
  rain_delay = delay;
  if (rain_delay_sensor_)
    rain_delay_sensor_->publish_state(delay);
}

void SnkMower::handle_multizone(const JsonDocument &doc) {
  bool mul_en = doc["mul_en"] | false;
  bool mul_auto = doc["mul_auto"] | false;
  ESP_LOGI(TAG, "Multizone: enabled=%d, auto=%d", mul_en, mul_auto);

  for (int i = 1; i <= 4; i++) {
    char z_key[8], per_key[8], meter_key[8];
    snprintf(z_key, sizeof(z_key), "mul_z%d", i);
    snprintf(per_key, sizeof(per_key), "per_z%d", i);
    snprintf(meter_key, sizeof(meter_key), "meter_z%d", i);
    if (doc.containsKey(z_key)) {
      ESP_LOGD(TAG, "  Zone %d: start=%d%%, per=%d%%, meter=%d",
               i, doc[z_key] | 0, doc[per_key] | 0, doc[meter_key] | 0);
    }
  }
}

void SnkMower::handle_light(const JsonDocument &doc) {
  if (doc.containsKey("lv")) {
    light_lv_ = doc["lv"];
    ESP_LOGD(TAG, "Light level: %d", light_lv_);
    if (light_level_sensor_)
      light_level_sensor_->publish_state(light_lv_);
  }
}

void SnkMower::handle_signal_level(const JsonDocument &doc) {
  if (doc.containsKey("lv")) {
    signal_lv_ = doc["lv"];
    ESP_LOGD(TAG, "Signal level: %d", signal_lv_);
    if (signal_level_sensor_)
      signal_level_sensor_->publish_state(signal_lv_);
  }
}

void SnkMower::handle_power_on(const JsonDocument &doc) {
  int action = doc["action"] | 0;
  ESP_LOGI(TAG, "Power ON (action=%d)", action);
}

void SnkMower::handle_power_ready(const JsonDocument &doc) {
  ESP_LOGI(TAG, "Power READY");
  power_ready_ = true;
}

void SnkMower::handle_boot_heart(const JsonDocument &doc) {
  ESP_LOGV(TAG, "Boot heartbeat");
}

void SnkMower::handle_boot_init(const JsonDocument &doc) {
  ESP_LOGD(TAG, "Boot init");
}

void SnkMower::handle_lock(const JsonDocument &doc) {
  lock_ = doc["lock"] | 0;
  ESP_LOGD(TAG, "Lock: %d", lock_);
  if (is_locked_sensor_)
    is_locked_sensor_->publish_state(lock_ != 0);
}

void SnkMower::handle_start_ack(const JsonDocument &doc) {
  int result = doc["result"] | 0;
  ESP_LOGI(TAG, "START ACK: result=%d", result);
}

void SnkMower::handle_exec_action(const JsonDocument &doc) {
  ESP_LOGD(TAG, "Exec action");
}

void SnkMower::handle_shutdown(const JsonDocument &doc) {
  ESP_LOGI(TAG, "Shutdown requested");
  publish_mower_state(MowerState::IDLE);
}

void SnkMower::handle_start_time_query(const JsonDocument &doc) {
  int hour = doc["hour"] | 0;
  int minute = doc["minute"] | 0;
  ESP_LOGI(TAG, "Start time query: %02d:%02d", hour, minute);
}

void SnkMower::handle_cut_time_query(const JsonDocument &doc) {
  int len = doc["len"] | 0;
  ESP_LOGI(TAG, "Cut time query: %d min", len);
}

void SnkMower::handle_setting_ack(const JsonDocument &doc, uint32_t cmd) {
  bool result = doc["result"] | false;
  uint8_t sub = cmd & 0xFF;
  ESP_LOGD(TAG, "Setting ACK 0x%02X: %s", sub, result ? "OK" : "FAIL");
}

void SnkMower::start_mowing() {
  ESP_LOGI(TAG, "Command: start mowing (physical button on mainboard)");
}

void SnkMower::return_to_dock() {
  ESP_LOGI(TAG, "Command: return to dock");
  send_return_home();
}

void SnkMower::publish_mower_state(MowerState state) {
  current_state_ = state;
  last_activity_ms_ = millis();
  display_off_ = false;
  int idx = static_cast<int>(state);

  if (is_mowing_sensor_)
    is_mowing_sensor_->publish_state(state == MowerState::MOWING);
  if (is_charging_sensor_)
    is_charging_sensor_->publish_state(state == MowerState::CHARGING);
  if (is_docked_sensor_)
    is_docked_sensor_->publish_state(state == MowerState::DOCKED ||
                                      state == MowerState::CHARGING);
  if (has_error_sensor_)
    has_error_sensor_->publish_state(state == MowerState::ERROR_STATE ||
                                      state == MowerState::LOCKED);
  if (is_returning_sensor_)
    is_returning_sensor_->publish_state(state == MowerState::RETURNING);

  if (mower_state_sensor_)
    mower_state_sensor_->publish_state(STATE_NAMES[idx]);

  if (state == MowerState::MOWING) {
    set_display_battery(last_battery_percent_);
  } else if (state == MowerState::CHARGING) {
    set_charging_display(last_battery_percent_);
  } else {
    set_display_text(STATE_DISPLAY[idx]);
  }
}

void SnkMower::set_battery_level_sensor(sensor::Sensor *s) { battery_level_sensor_ = s; }
void SnkMower::set_battery_voltage_sensor(sensor::Sensor *s) { battery_voltage_sensor_ = s; }
void SnkMower::set_error_code_sensor(sensor::Sensor *s) { error_code_sensor_ = s; }
void SnkMower::set_light_level_sensor(sensor::Sensor *s) { light_level_sensor_ = s; }
void SnkMower::set_signal_level_sensor(sensor::Sensor *s) { signal_level_sensor_ = s; }
void SnkMower::set_work_area_sensor(sensor::Sensor *s) { work_area_sensor_ = s; }
void SnkMower::set_cut_area_sensor(sensor::Sensor *s) { cut_area_sensor_ = s; }
void SnkMower::set_total_minutes_sensor(sensor::Sensor *s) { total_minutes_sensor_ = s; }
void SnkMower::set_on_minutes_sensor(sensor::Sensor *s) { on_minutes_sensor_ = s; }
void SnkMower::set_bat_health_sensor(sensor::Sensor *s) { bat_health_sensor_ = s; }
void SnkMower::set_bat_level_bars_sensor(sensor::Sensor *s) { bat_level_bars_sensor_ = s; }
void SnkMower::set_rain_delay_sensor(sensor::Sensor *s) { rain_delay_sensor_ = s; }

void SnkMower::set_is_mowing_sensor(binary_sensor::BinarySensor *s) { is_mowing_sensor_ = s; }
void SnkMower::set_is_charging_sensor(binary_sensor::BinarySensor *s) { is_charging_sensor_ = s; }
void SnkMower::set_is_docked_sensor(binary_sensor::BinarySensor *s) { is_docked_sensor_ = s; }
void SnkMower::set_has_error_sensor(binary_sensor::BinarySensor *s) { has_error_sensor_ = s; }
void SnkMower::set_is_locked_sensor(binary_sensor::BinarySensor *s) { is_locked_sensor_ = s; }
void SnkMower::set_is_returning_sensor(binary_sensor::BinarySensor *s) { is_returning_sensor_ = s; }

void SnkMower::set_device_name_sensor(text_sensor::TextSensor *s) { device_name_sensor_ = s; }
void SnkMower::set_model_sensor(text_sensor::TextSensor *s) { model_sensor_ = s; }
void SnkMower::set_serial_sensor(text_sensor::TextSensor *s) { serial_sensor_ = s; }
void SnkMower::set_firmware_version_sensor(text_sensor::TextSensor *s) { firmware_version_sensor_ = s; }
void SnkMower::set_battery_name_sensor(text_sensor::TextSensor *s) { battery_name_sensor_ = s; }
void SnkMower::set_mower_state_sensor(text_sensor::TextSensor *s) { mower_state_sensor_ = s; }

}  // namespace snk_mower
}  // namespace esphome
