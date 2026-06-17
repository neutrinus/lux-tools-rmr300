#include "snk_mower.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace snk_mower {

static const char *const TAG = "snk_mower";

static const uint8_t SYNC0 = 0xAA;
static const uint8_t SYNC1 = 0x55;

static const uint8_t CMD_PWD_VERIFY = 0x0B;
static const uint8_t CMD_PWD_RESULT = 0x0C;
static const uint8_t CMD_STATUS_REQ = 0x0D;
static const uint8_t CMD_STATUS_RSP = 0x0E;
static const uint8_t CMD_MOW_START = 0x0F;
static const uint8_t CMD_CHARGE_RET = 0x10;
static const uint8_t CMD_DISPLAY_OFF = 0x11;
static const uint8_t CMD_ERROR_INFO = 0x12;
static const uint8_t CMD_BAT_INFO_REQ = 0x14;
static const uint8_t CMD_BAT_INFO_RSP = 0x15;

static const char *const STATUS_NAMES[] = {
    "unknown", "idle",    "mowing", "returning",
    "charging", "docked", "error",  "locked",
};

static const char *const STATUS_DISPLAY[] = {
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

// ── 7-segment character map ──────────────────────────────────
// Layout: bit 0= A (top), 1= B (top-right), 2= C (bot-right),
//         3= D (bot), 4= E (bot-left), 5= F (top-left),
//         6= G (middle), 7= DP
static const uint8_t SEG_CHARS[256] = {
    [' '] = 0b00000000,
    ['-'] = 0b01000000,
    ['_'] = 0b00001000,
    ['0'] = 0b00111111,
    ['1'] = 0b00000110,
    ['2'] = 0b01011011,
    ['3'] = 0b01001111,
    ['4'] = 0b01100110,
    ['5'] = 0b01101101,
    ['6'] = 0b01111101,
    ['7'] = 0b00000111,
    ['8'] = 0b01111111,
    ['9'] = 0b01101111,
    ['A'] = 0b01110111,
    ['b'] = 0b01111100,
    ['C'] = 0b00111001,
    ['c'] = 0b01011000,
    ['d'] = 0b01011110,
    ['E'] = 0b01111001,
    ['F'] = 0b01110001,
    ['H'] = 0b01110110,
    ['h'] = 0b01110100,
    ['I'] = 0b00110000,
    ['J'] = 0b00011110,
    ['L'] = 0b00111000,
    ['n'] = 0b01010100,
    ['o'] = 0b01011100,
    ['P'] = 0b01110011,
    ['r'] = 0b01010000,
    ['S'] = 0b01101101,
    ['t'] = 0b01111000,
    ['U'] = 0b00111110,
    ['u'] = 0b00011100,
};

// ── Constructor ──────────────────────────────────────────────
SnkMower::SnkMower(const std::string &pin)
    : pin_(pin),
      display_clk_(GPIO_NUM_NC),
      display_mosi_(GPIO_NUM_NC),
      display_cs_(GPIO_NUM_NC) {}

// ── setup ────────────────────────────────────────────────────
void SnkMower::setup() {
  ESP_LOGI(TAG, "SNK Mower starting (PIN: %s)", pin_.c_str());
  pin_sent_ = false;
  pin_ok_ = false;
  pin_retries_ = 0;
  expecting_response_ = false;
  rx_state_ = -1;
  current_state_ = MowerState::UNKNOWN;
  current_digit_ = 0;
  last_display_ms_ = 0;
  charging_frame_ = 0;
  last_charging_frame_ms_ = 0;

  setup_display();
  set_display_text("----");
}

void SnkMower::set_display_pins(uint8_t clk, uint8_t mosi, uint8_t cs) {
  display_clk_ = ISRInternalGPIOPin(clk);
  display_mosi_ = ISRInternalGPIOPin(mosi);
  display_cs_ = ISRInternalGPIOPin(cs);
}

void SnkMower::setup_display() {
  if (display_clk_.get_pin() == GPIO_NUM_NC) {
    ESP_LOGW(TAG, "Display pins not configured, skipping display init");
    return;
  }
  display_clk_.pin_mode(gpio::FLAG_OUTPUT);
  display_mosi_.pin_mode(gpio::FLAG_OUTPUT);
  display_cs_.pin_mode(gpio::FLAG_OUTPUT);
  display_clk_.digital_write(false);
  display_mosi_.digital_write(false);
  display_cs_.digital_write(true);  // latch inactive
  ESP_LOGI(TAG, "Display initialized (CLK=%d, MOSI=%d, CS=%d)",
           display_clk_.get_pin(), display_mosi_.get_pin(), display_cs_.get_pin());
}

// ── Bit-bang SPI: shift out 24 bits (3 bytes) ───────────────
static void shift24(ISRInternalGPIOPin &clk, ISRInternalGPIOPin &mosi,
                    ISRInternalGPIOPin &cs, uint8_t b0, uint8_t b1,
                    uint8_t b2) {
  cs.digital_write(false);
  for (int i = 7; i >= 0; i--) {
    mosi.digital_write((b0 >> i) & 1);
    clk.digital_write(true);
    clk.digital_write(false);
  }
  for (int i = 7; i >= 0; i--) {
    mosi.digital_write((b1 >> i) & 1);
    clk.digital_write(true);
    clk.digital_write(false);
  }
  for (int i = 7; i >= 0; i--) {
    mosi.digital_write((b2 >> i) & 1);
    clk.digital_write(true);
    clk.digital_write(false);
  }
  cs.digital_write(true);
}

void SnkMower::refresh_display() {
  if (display_clk_.get_pin() == GPIO_NUM_NC) return;

  uint32_t now = millis();
  if (now - last_display_ms_ < DISPLAY_REFRESH_MS) return;
  last_display_ms_ = now;

  uint8_t seg = display_segments_[current_digit_];

  // Charging animation on digit 0: override segment with animation frame
  if (current_state_ == MowerState::CHARGING && current_digit_ == 0) {
    if (now - last_charging_frame_ms_ >= CHG_FRAME_MS) {
      last_charging_frame_ms_ = now;
      charging_frame_ = (charging_frame_ + 1) % 3;
    }
    seg = CHG_FRAMES[charging_frame_];
  }

  uint8_t dig = 1 << current_digit_;
  current_digit_ = (current_digit_ + 1) % DIGITS;

  // 3 bytes: chip3 (unused), chip2 (digit selects + colon), chip1 (segments)
  shift24(display_clk_, display_mosi_, display_cs_,
          0x00,                     // chip 3 – unused
          display_colon_ | dig,     // chip 2 – colon + digit select
          seg);                     // chip 1 – segments
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
    buf[0] = ' ', buf[1] = '1', buf[2] = '0', buf[3] = '0';
  } else {
    buf[0] = ' ', buf[1] = '0' + (percent / 10);
    buf[2] = '0' + (percent % 10), buf[3] = ' ';
  }
  // Add 'b' prefix on first digit (battery indicator)
  buf[0] = 'b';
  set_display_text(buf);
}

void SnkMower::set_charging_display(int percent) {
  // Digit 0: blank (animation overrides in refresh_display)
  display_segments_[0] = 0;
  // Digits 1-3: battery percentage
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

uint8_t SnkMower::char_to_segments_(char c) const {
  uint8_t v = SEG_CHARS[static_cast<uint8_t>(c)];
  return v;
}

// ── UART ─────────────────────────────────────────────────────
void SnkMower::send_frame(uint8_t cmd, const uint8_t *payload, size_t len) {
  uint8_t cs = cmd;
  for (size_t i = 0; i < len; i++) cs ^= payload[i];

  write_byte(SYNC0);
  write_byte(SYNC1);
  write_byte(cmd);
  if (len > 0) write_array(payload, len);
  write_byte(cs);

  expecting_response_ = true;

  ESP_LOGD(TAG, "TX: AA 55 %02X [%zuB] CS=%02X", cmd, len, cs);
}

void SnkMower::send_pin() {
  ESP_LOGI(TAG, "Sending PIN to mainboard");
  send_frame(CMD_PWD_VERIFY, reinterpret_cast<const uint8_t *>(pin_.data()), 4);
  pin_sent_ = true;
}

void SnkMower::poll_status() { send_frame(CMD_STATUS_REQ, nullptr, 0); }
void SnkMower::poll_battery() { send_frame(CMD_BAT_INFO_REQ, nullptr, 0); }

void SnkMower::start_mowing() {
  ESP_LOGI(TAG, "Command: start mowing");
  send_frame(CMD_MOW_START, nullptr, 0);
}

void SnkMower::return_to_dock() {
  ESP_LOGI(TAG, "Command: return to dock");
  send_frame(CMD_CHARGE_RET, nullptr, 0);
}

// ── loop ─────────────────────────────────────────────────────
void SnkMower::loop() {
  // RX parser
  while (available() > 0) {
    uint8_t byte;
    read_byte(&byte);

    if (rx_state_ == -1) {
      if (byte == SYNC0) {
        rx_state_ = 0;
        rx_index_ = 0;
      }
      continue;
    }
    if (rx_state_ == 0) {
      rx_state_ = (byte == SYNC1) ? 1 : -1;
      continue;
    }
    if (rx_state_ == 1) {
      rx_cmd_ = byte;
      rx_len_ = 0;
      rx_index_ = 0;
      rx_state_ = 2;
      continue;
    }
    if (rx_state_ == 2) {
      rx_buf_[rx_index_++] = byte;
      rx_len_ = rx_index_;

      uint8_t cs = rx_cmd_;
      for (size_t i = 0; i < rx_len_ - 1; i++) cs ^= rx_buf_[i];

      if (cs == byte) {
        handle_response(rx_cmd_, rx_buf_, rx_len_ - 1);
        rx_state_ = -1;
      } else if (rx_index_ >= 20) {
        ESP_LOGW(TAG, "RX frame too long, discarding");
        rx_state_ = -1;
      }
    }
  }

  // Periodic polling
  if (!expecting_response_ && millis() - last_poll_ > 2000) {
    last_poll_ = millis();

    if (!pin_sent_) {
      send_pin();
    } else if (!pin_ok_) {
      if (++pin_retries_ < 5) {
        pin_sent_ = false;
      } else {
        ESP_LOGE(TAG, "PIN failed after 5 retries");
        publish_mower_state(MowerState::LOCKED);
      }
    } else {
      static uint8_t phase = 0;
      phase = (phase + 1) % 4;
      if (phase % 2 == 0)
        poll_status();
      else
        poll_battery();
    }
  }

  // Display refresh
  refresh_display();
}

// ── Response handlers ────────────────────────────────────────
void SnkMower::handle_response(uint8_t cmd, const uint8_t *data,
                                size_t len) {
  expecting_response_ = false;

  ESP_LOGD(TAG, "RX: 0x%02X [%zu] %s", cmd, len,
           format_hex_pretty(data, len).c_str());

  switch (cmd) {
    case CMD_PWD_RESULT:
      handle_pin_response(data, len);
      break;
    case CMD_STATUS_RSP:
      handle_status_response(data, len);
      break;
    case CMD_BAT_INFO_RSP:
      handle_battery_response(data, len);
      break;
    case CMD_ERROR_INFO:
      handle_error_info(data, len);
      break;
    default:
      ESP_LOGD(TAG, "Unhandled response cmd 0x%02X", cmd);
      break;
  }
}

void SnkMower::handle_pin_response(const uint8_t *data, size_t len) {
  if (len >= 1) {
    if (data[0] == 0x00) {
      ESP_LOGI(TAG, "PIN accepted");
      pin_ok_ = true;
      pin_retries_ = 0;
      publish_mower_state(MowerState::IDLE);
    } else {
      ESP_LOGW(TAG, "PIN rejected (attempt %d/5)", pin_retries_);
      pin_retries_++;
      pin_sent_ = false;
    }
  }
}

void SnkMower::handle_status_response(const uint8_t *data, size_t len) {
  if (len < 1) return;
  uint8_t flags = data[0];

  MowerState s;
  if (flags & 0x20)
    s = MowerState::LOCKED;
  else if (flags & 0x10)
    s = MowerState::ERROR_STATE;
  else if (flags & 0x01)
    s = MowerState::MOWING;
  else if (flags & 0x02)
    s = MowerState::RETURNING;
  else if (flags & 0x04)
    s = MowerState::CHARGING;
  else if (flags & 0x08)
    s = MowerState::DOCKED;
  else
    s = MowerState::IDLE;

  publish_mower_state(s);
  ESP_LOGD(TAG, "Status 0x%02X → %s", flags, STATUS_NAMES[static_cast<int>(s)]);
}

void SnkMower::handle_battery_response(const uint8_t *data, size_t len) {
  if (len < 2) return;
  uint16_t mv = (static_cast<uint16_t>(data[0]) << 8) | data[1];
  float volts = mv / 1000.0f;
  int pct = voltage_to_percent(volts);
  last_battery_percent_ = pct;

  if (battery_voltage_sensor_) battery_voltage_sensor_->publish_state(volts);
  if (battery_level_sensor_) battery_level_sensor_->publish_state(pct);

  // Update display with battery
  if (current_state_ == MowerState::MOWING)
    set_display_battery(pct);
  else if (current_state_ == MowerState::CHARGING)
    set_charging_display(pct);

  ESP_LOGD(TAG, "Battery: %dmV → %.2fV → %d%%", mv, volts, pct);
}

void SnkMower::handle_error_info(const uint8_t *data, size_t len) {
  if (len >= 2) {
    uint16_t code = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    ESP_LOGW(TAG, "Error code: 0x%04X", code);
    if (error_code_sensor_) error_code_sensor_->publish_state(code);
  }
  publish_mower_state(MowerState::ERROR_STATE);
}

void SnkMower::publish_mower_state(MowerState state) {
  current_state_ = state;
  int idx = static_cast<int>(state);

  if (status_text_sensor_) status_text_sensor_->publish_state(STATUS_NAMES[idx]);

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

  // Display
  if (state == MowerState::MOWING) {
    set_display_battery(last_battery_percent_);
  } else if (state == MowerState::CHARGING) {
    set_charging_display(last_battery_percent_);
  } else {
    set_display_text(STATUS_DISPLAY[idx]);
  }
}

// ── Sensor setters ───────────────────────────────────────────
void SnkMower::set_battery_level_sensor(sensor::Sensor *s) { battery_level_sensor_ = s; }
void SnkMower::set_battery_voltage_sensor(sensor::Sensor *s) { battery_voltage_sensor_ = s; }
void SnkMower::set_error_code_sensor(sensor::Sensor *s) { error_code_sensor_ = s; }
void SnkMower::set_status_text_sensor(text_sensor::TextSensor *s) { status_text_sensor_ = s; }
void SnkMower::set_status_message_sensor(text_sensor::TextSensor *s) { status_message_sensor_ = s; }
void SnkMower::set_is_mowing_sensor(binary_sensor::BinarySensor *s) { is_mowing_sensor_ = s; }
void SnkMower::set_is_charging_sensor(binary_sensor::BinarySensor *s) { is_charging_sensor_ = s; }
void SnkMower::set_is_docked_sensor(binary_sensor::BinarySensor *s) { is_docked_sensor_ = s; }
void SnkMower::set_has_error_sensor(binary_sensor::BinarySensor *s) { has_error_sensor_ = s; }

}  // namespace snk_mower
}  // namespace esphome
