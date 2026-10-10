#include "snk_mower.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace snk_mower {

static const char *const TAG = "snk_mower";

// One digit per slot, 4 slots per frame: 2 ms per digit, 125 Hz frame rate, as
// the original tube scan task (vTaskDelay(2) at 1 kHz tick).
static constexpr uint32_t DISPLAY_SLOT_US = 2000;
// Shortest lit time when dimmed; the SPI write itself takes about 15 us.
static constexpr uint32_t DISPLAY_MIN_ON_US = 50;
static constexpr uint32_t STATE_CYCLE_MS = 5000;     // state text <-> battery %

static const char *const STATE_NAMES[] = {"unknown", "idle", "mowing", "returning",
                                          "charging", "docked", "error", "locked", "trimming"};
static const char *const STATE_DISPLAY[] = {"----", "IdLE", "Mow ", "HoME", "ChAr", "dock", "Err ", "LoCK", "Cut "};

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
  // 7-segment display behind three 74HC595 on SPI2 (original TubeInit 0x400e6da0).
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

  // The original scans from its own task ("tube scan", priority 25, core 1).
  // Here the task sits above everything else on core 1 (no Wi-Fi there) and is
  // paced by a hardware timer, so every digit gets the same on-time.
#if CONFIG_FREERTOS_UNICORE
  const BaseType_t core = tskNO_AFFINITY;
#else
  const BaseType_t core = 1;
#endif
  if (xTaskCreatePinnedToCore(&SnkMower::display_task, "snk_display", 2048, this, configMAX_PRIORITIES - 1,
                              &display_task_, core) != pdPASS) {
    ESP_LOGE(TAG, "Display task create failed");
    return;
  }

  update_display_on_time();
  gptimer_config_t timer_cfg = {};
  timer_cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  timer_cfg.direction = GPTIMER_COUNT_UP;
  timer_cfg.resolution_hz = 1000000;
  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = &SnkMower::display_alarm_isr;
  gptimer_alarm_config_t alarm = {};
  alarm.alarm_count = DISPLAY_SLOT_US;
  if (gptimer_new_timer(&timer_cfg, &display_timer_) != ESP_OK ||
      gptimer_register_event_callbacks(display_timer_, &cbs, this) != ESP_OK ||
      gptimer_set_alarm_action(display_timer_, &alarm) != ESP_OK || gptimer_enable(display_timer_) != ESP_OK ||
      gptimer_start(display_timer_) != ESP_OK)
    ESP_LOGE(TAG, "Display timer setup failed");
}

bool IRAM_ATTR SnkMower::display_alarm_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata,
                                           void *arg) {
  static const uint8_t DIGIT_SELECT[DIGITS] = {0x20, 0x10, 0x08, 0x04};
  auto *self = static_cast<SnkMower *>(arg);
  uint32_t on_us = self->display_on_us_;
  uint32_t next_us;
  if (self->display_blank_next_) {
    // End of the lit part of a dimmed slot: all digits off until the next slot.
    self->display_blank_next_ = false;
    self->display_frame_ = 0;
    next_us = on_us < DISPLAY_SLOT_US ? DISPLAY_SLOT_US - on_us : DISPLAY_MIN_ON_US;
  } else {
    uint8_t digit = self->current_digit_;
    self->current_digit_ = (digit + 1) % DIGITS;
    self->display_frame_ = self->display_off_ ? 0 : (DIGIT_SELECT[digit] << 8) | self->display_segments_[digit];
    self->display_blank_next_ = on_us < DISPLAY_SLOT_US;
    next_us = self->display_blank_next_ ? on_us : DISPLAY_SLOT_US;
  }
  // Schedule from the previous alarm so the slots do not drift. If the ISR ran
  // late (interrupts are held off while the flash cache is disabled), that
  // target may already be behind the counter and would never fire; restart
  // from now instead.
  gptimer_alarm_config_t alarm = {};
  alarm.alarm_count = edata->alarm_value + next_us;
  if (alarm.alarm_count <= edata->count_value + DISPLAY_MIN_ON_US)
    alarm.alarm_count = edata->count_value + next_us;
  gptimer_set_alarm_action(timer, &alarm);

  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(self->display_task_, &woken);
  return woken == pdTRUE;
}

void SnkMower::display_task(void *arg) {
  auto *self = static_cast<SnkMower *>(arg);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint16_t frame = self->display_frame_;
    spi_transaction_t trans = {};
    trans.length = 24;
    trans.flags = SPI_TRANS_USE_TXDATA;
    trans.tx_data[0] = 0;
    trans.tx_data[1] = frame >> 8;
    trans.tx_data[2] = frame & 0xFF;
    spi_device_polling_transmit(self->spi_dev_, &trans);
  }
}

void SnkMower::update_display_on_time() {
  uint32_t on_us = DISPLAY_SLOT_US;
  if (display_night_)
    on_us = std::max<uint32_t>(DISPLAY_MIN_ON_US, DISPLAY_SLOT_US * display_night_brightness_ / 100);
  display_on_us_ = on_us;
}

void SnkMower::set_display_night(bool night) {
  display_night_ = night;
  update_display_on_time();
  ESP_LOGD(TAG, "Display night mode %s", night ? "on" : "off");
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

  bool busy = current_state_ == MowerState::MOWING || current_state_ == MowerState::TRIMMING ||
              current_state_ == MowerState::CHARGING ||
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
    is_mowing_sensor_->publish_state(state == MowerState::MOWING || state == MowerState::TRIMMING);
  if (is_charging_sensor_)
    is_charging_sensor_->publish_state(state == MowerState::CHARGING);
  if (is_docked_sensor_)
    is_docked_sensor_->publish_state(state == MowerState::DOCKED || state == MowerState::CHARGING);
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
    if (state == MowerState::MOWING || state == MowerState::TRIMMING || state == MowerState::CHARGING)
      set_display_number(battery_percent_);
    else
      set_display_text(STATE_DISPLAY[static_cast<int>(state)]);
  }
  if (changed) {
    state_display_cycle_ms_ = millis() + STATE_CYCLE_MS;
    state_show_alt_ =
        state == MowerState::MOWING || state == MowerState::TRIMMING || state == MowerState::CHARGING;
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
