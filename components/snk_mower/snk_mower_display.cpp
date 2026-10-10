#include "snk_mower.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace snk_mower {

static const char *const TAG = "snk_mower";

static constexpr uint32_t DISPLAY_REFRESH_US = 2000;  // one digit per tick, multiplexed
static constexpr uint32_t STATE_CYCLE_MS = 5000;     // state text <-> battery %

static const char *const STATE_NAMES[] = {"unknown", "idle", "mowing", "returning",
                                          "charging", "docked", "error", "locked"};
static const char *const STATE_DISPLAY[] = {"----", "IdLE", "Mow ", "HoME", "ChAr", "dock", "Err ", "LoCK"};

static uint8_t char_to_segments(char c) {
  switch (c) {
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
    case 'y':
    case 'Y': return 0b01100110;
    default: return 0;
  }
}

void SnkMower::set_display_pins(uint8_t clk, uint8_t mosi, uint8_t cs) {
  display_clk_ = (gpio_num_t) clk;
  display_mosi_ = (gpio_num_t) mosi;
  display_cs_ = (gpio_num_t) cs;
}

void SnkMower::setup_display() {
  // 7-segment display behind shift registers on SPI2 (original TubeInit 0x400e6da0).
  spi_bus_config_t bus_cfg = {};
  bus_cfg.mosi_io_num = display_mosi_;
  bus_cfg.miso_io_num = -1;
  bus_cfg.sclk_io_num = display_clk_;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  bus_cfg.max_transfer_sz = 4;
  if (spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_DISABLED) != ESP_OK) {
    ESP_LOGE(TAG, "Display SPI bus init failed");
    return;
  }

  spi_device_interface_config_t dev_cfg = {};
  dev_cfg.clock_speed_hz = 2000000;
  dev_cfg.mode = 0;
  dev_cfg.spics_io_num = display_cs_;
  dev_cfg.queue_size = 1;
  if (spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi_dev_) != ESP_OK) {
    ESP_LOGE(TAG, "Display SPI device add failed");
    spi_dev_ = nullptr;
    return;
  }

  esp_timer_create_args_t args = {
      .callback = &SnkMower::display_timer_callback,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "snk_display",
      .skip_unhandled_events = true,
  };
  if (esp_timer_create(&args, &display_timer_) == ESP_OK)
    esp_timer_start_periodic(display_timer_, DISPLAY_REFRESH_US);
  else
    ESP_LOGE(TAG, "Display timer create failed");
}

void SnkMower::display_timer_callback(void *arg) { static_cast<SnkMower *>(arg)->refresh_display(); }

void SnkMower::refresh_display() {
  static const uint8_t DIGIT_SELECT[DIGITS] = {0x20, 0x10, 0x08, 0x04};
  if (display_off_ || spi_dev_ == nullptr)
    return;
  uint8_t digit = current_digit_;
  current_digit_ = (digit + 1) % DIGITS;

  spi_transaction_t trans = {};
  trans.length = 24;
  trans.flags = SPI_TRANS_USE_TXDATA;
  trans.tx_data[0] = 0;
  trans.tx_data[1] = DIGIT_SELECT[digit];
  trans.tx_data[2] = display_segments_[digit];
  spi_device_polling_transmit(spi_dev_, &trans);
}

void SnkMower::set_display_text(const char *text) {
  bool ended = false;
  for (uint8_t i = 0; i < DIGITS; i++) {
    ended = ended || text[i] == '\0';
    display_segments_[i] = ended ? 0 : char_to_segments(text[i]);
  }
}

void SnkMower::set_display_number(int value) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%4d", std::min(9999, std::max(0, value)));
  set_display_text(buf);
}

void SnkMower::display_loop(uint32_t now) {
  if (shutdown_pending_ && now - shutdown_start_ms_ > 3000)
    shutdown_pending_ = false;

  bool busy = current_state_ == MowerState::MOWING || current_state_ == MowerState::CHARGING ||
              current_state_ == MowerState::RETURNING || current_state_ == MowerState::ERROR_STATE ||
              current_state_ == MowerState::LOCKED;
  if (display_off_timeout_ms_ > 0 && !display_off_ && !busy && now - last_activity_ms_ > display_off_timeout_ms_) {
    ESP_LOGD(TAG, "Display off after %u min idle", (unsigned) (display_off_timeout_ms_ / 60000));
    display_off_ = true;
  }

  // Alternate between the state text and the battery percentage.
  if (display_off_ || shutdown_pending_ || current_state_ == MowerState::UNKNOWN ||
      current_state_ == MowerState::ERROR_STATE || current_state_ == MowerState::LOCKED)
    return;
  if ((int32_t) (now - state_display_cycle_ms_) < 0)
    return;
  state_display_cycle_ms_ = now + STATE_CYCLE_MS;
  state_show_alt_ = !state_show_alt_;
  if (state_show_alt_)
    set_display_number(battery_percent_);
  else
    set_display_text(STATE_DISPLAY[static_cast<int>(current_state_)]);
}

void SnkMower::publish_mower_state(MowerState state) {
  bool changed = state != current_state_;
  current_state_ = state;
  last_activity_ms_ = millis();
  display_off_ = false;

  if (is_mowing_sensor_)
    is_mowing_sensor_->publish_state(state == MowerState::MOWING);
  if (is_charging_sensor_)
    is_charging_sensor_->publish_state(state == MowerState::CHARGING);
  if (is_docked_sensor_)
    is_docked_sensor_->publish_state(state == MowerState::DOCKED || state == MowerState::CHARGING);
  if (has_error_sensor_)
    has_error_sensor_->publish_state(state == MowerState::ERROR_STATE || state == MowerState::LOCKED);
  if (is_returning_sensor_)
    is_returning_sensor_->publish_state(state == MowerState::RETURNING);
  if (mower_state_sensor_)
    mower_state_sensor_->publish_state(STATE_NAMES[static_cast<int>(state)]);

  if (shutdown_pending_)
    return;
  if (state == MowerState::ERROR_STATE) {
    char buf[8];
    snprintf(buf, sizeof(buf), "E%-3d", std::min(999, std::max(0, error_code_)));
    set_display_text(buf);
  } else if (changed) {
    if (state == MowerState::MOWING || state == MowerState::CHARGING)
      set_display_number(battery_percent_);
    else
      set_display_text(STATE_DISPLAY[static_cast<int>(state)]);
  }
  if (changed) {
    state_display_cycle_ms_ = millis() + STATE_CYCLE_MS;
    state_show_alt_ = state == MowerState::MOWING || state == MowerState::CHARGING;
  }
}

void SnkMower::buzz(int duration_ms) {
  if (buzzer_pin_ == GPIO_NUM_NC)
    return;
  gpio_set_level(buzzer_pin_, 1);
  delay(duration_ms);
  gpio_set_level(buzzer_pin_, 0);
}

}  // namespace snk_mower
}  // namespace esphome
