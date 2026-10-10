#include "snk_mower.h"
#include "esphome/core/log.h"

namespace esphome {
namespace snk_mower {

static const char *const TAG = "snk_mower.rain";

// Same timing as the original "rain detect thread" (ESP32 0x400defb8):
// 1 s in each polarity, then a burst of 5 samples 10 ms apart at the end of
// polarity 1. A state change needs 16 consecutive samples on the same side of
// the threshold, i.e. about 6 s.
static constexpr uint32_t RAIN_PHASE_MS = 1000;
static constexpr uint32_t RAIN_SAMPLE_GAP_MS = 10;
static constexpr uint8_t RAIN_SAMPLES_PER_BURST = 5;
static constexpr uint8_t RAIN_DEBOUNCE = 15;
static constexpr uint32_t RAIN_ADC_PUBLISH_MS = 30000;

void SnkMower::setup_rain() {
  adc_unit_t unit;
  if (adc_oneshot_io_to_channel(rain_pin_, &unit, &rain_channel_) != ESP_OK || unit != ADC_UNIT_1) {
    ESP_LOGE(TAG, "GPIO%d is not an ADC1 pin, rain sensor disabled", rain_pin_);
    return;
  }
  adc_oneshot_unit_init_cfg_t unit_cfg = {};
  unit_cfg.unit_id = ADC_UNIT_1;
  if (adc_oneshot_new_unit(&unit_cfg, &rain_adc_) != ESP_OK) {
    ESP_LOGE(TAG, "ADC1 init failed, rain sensor disabled");
    rain_adc_ = nullptr;
    return;
  }
  adc_oneshot_chan_cfg_t chan_cfg = {};
  chan_cfg.atten = ADC_ATTEN_DB_12;  // the original uses 11 dB (full range)
  chan_cfg.bitwidth = ADC_BITWIDTH_12;
  adc_oneshot_config_channel(rain_adc_, rain_channel_, &chan_cfg);

  for (gpio_num_t pin : {rain_drive_a_, rain_drive_b_}) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(pin, GPIO_PULLDOWN_ONLY);
  }
  set_rain_polarity(1);
  int raw = 0;
  adc_oneshot_read(rain_adc_, rain_channel_, &raw);
  rain_acc_ = raw * 4;
  rain_phase_start_ms_ = millis();
  ESP_LOGI(TAG, "Rain sensor on GPIO%d (drive GPIO%d/GPIO%d), first reading %d", rain_pin_, rain_drive_a_,
           rain_drive_b_, raw);
}

// Polarity 1: A high, B low (measure). Polarity 2: reversed, so the
// electrodes do not corrode.
void SnkMower::set_rain_polarity(uint8_t polarity) {
  gpio_set_level(rain_drive_a_, polarity == 1);
  gpio_set_level(rain_drive_b_, polarity != 1);
  rain_polarity_ = polarity;
}

void SnkMower::rain_loop(uint32_t now) {
  if (rain_samples_left_ > 0) {
    if (now - rain_last_sample_ms_ < RAIN_SAMPLE_GAP_MS)
      return;
    rain_last_sample_ms_ = now;
    rain_sample();
    if (--rain_samples_left_ == 0) {
      set_rain_polarity(2);
      rain_phase_start_ms_ = now;
    }
    return;
  }
  if (now - rain_phase_start_ms_ < RAIN_PHASE_MS)
    return;
  if (rain_polarity_ == 2) {
    set_rain_polarity(1);
    rain_phase_start_ms_ = now;
  } else {
    rain_samples_left_ = RAIN_SAMPLES_PER_BURST;
    rain_last_sample_ms_ = now - RAIN_SAMPLE_GAP_MS;
  }
}

void SnkMower::rain_sample() {
  int raw = 0;
  if (adc_oneshot_read(rain_adc_, rain_channel_, &raw) != ESP_OK)
    return;
  rain_acc_ += raw - rain_acc_ / 4;
  int avg = rain_acc_ / 4;

  // A dry sensor reads high (about 3000+ of 4095); water between the
  // electrodes pulls the reading down.
  uint8_t state = rain_state_;
  if (rain_acc_ >= rain_threshold_ * 4) {
    rain_wet_count_ = 0;
    if (rain_dry_count_ < RAIN_DEBOUNCE)
      rain_dry_count_++;
    else
      state = RAIN_DRY;
  } else {
    rain_dry_count_ = 0;
    if (rain_wet_count_ < RAIN_DEBOUNCE)
      rain_wet_count_++;
    else
      state = RAIN_WET;
  }

  uint32_t now = millis();
  if (rain_adc_sensor_ && (rain_adc_published_ms_ == 0 || now - rain_adc_published_ms_ >= RAIN_ADC_PUBLISH_MS)) {
    rain_adc_published_ms_ = now;
    rain_adc_sensor_->publish_state(avg);
  }
  if (state != rain_state_ || (raining_sensor_ && !raining_sensor_->has_state())) {
    if (state != rain_state_)
      ESP_LOGI(TAG, "Rain sensor: %s (adc %d)", state == RAIN_WET ? "raining" : "dry", avg);
    rain_state_ = state;
    if (raining_sensor_)
      raining_sensor_->publish_state(state == RAIN_WET);
    if (link_ == Link::UP)
      send_rain_status();
  }
}

}  // namespace snk_mower
}  // namespace esphome
