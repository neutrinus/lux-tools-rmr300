#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <driver/spi_master.h>
#include <esp_adc/adc_oneshot.h>
#include <ArduinoJson.h>
#include <string>
#include <vector>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace esphome {
namespace snk_mower {

enum class MowerState : uint8_t {
  UNKNOWN = 0,
  IDLE,
  MOWING,
  RETURNING,
  CHARGING,
  DOCKED,
  ERROR_STATE,
  LOCKED,
  TRIMMING,  // edge trim from the station (U13 state 16)
};

class SnkMower : public Component, public uart::UARTDevice {
 public:
  explicit SnkMower(const std::string &pin) : pin_(pin) {}

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Config
  void set_display_pins(uint8_t clk, uint8_t mosi, uint8_t cs);
  void set_buzzer_pin(gpio_num_t pin) { buzzer_pin_ = pin; }
  void set_rain_pin(gpio_num_t pin) { rain_pin_ = pin; }
  void set_rain_drive_pins(gpio_num_t a, gpio_num_t b) {
    rain_drive_a_ = a;
    rain_drive_b_ = b;
  }
  void set_rain_threshold(int raw) { rain_threshold_ = raw; }
  void set_display_off_timeout(uint32_t minutes) { display_off_timeout_ms_ = minutes * 60000UL; }
  void set_display_night_brightness(uint8_t percent) { display_night_brightness_ = percent; }

  // Night mode: dims the display to display_night_brightness percent.
  void set_display_night(bool night);

  void set_battery_level_sensor(sensor::Sensor *s) { battery_level_sensor_ = s; }
  void set_error_code_sensor(sensor::Sensor *s) { error_code_sensor_ = s; }
  void set_light_level_sensor(sensor::Sensor *s) { light_level_sensor_ = s; }
  void set_work_area_sensor(sensor::Sensor *s) { work_area_sensor_ = s; }
  void set_cut_area_sensor(sensor::Sensor *s) { cut_area_sensor_ = s; }
  void set_total_minutes_sensor(sensor::Sensor *s) { total_minutes_sensor_ = s; }
  void set_on_minutes_sensor(sensor::Sensor *s) { on_minutes_sensor_ = s; }
  void set_bat_health_sensor(sensor::Sensor *s) { bat_health_sensor_ = s; }
  void set_bat_level_bars_sensor(sensor::Sensor *s) { bat_level_bars_sensor_ = s; }
  void set_rain_delay_sensor(sensor::Sensor *s) { rain_delay_sensor_ = s; }
  void set_rain_adc_sensor(sensor::Sensor *s) { rain_adc_sensor_ = s; }

  void set_is_mowing_sensor(binary_sensor::BinarySensor *s) { is_mowing_sensor_ = s; }
  void set_is_charging_sensor(binary_sensor::BinarySensor *s) { is_charging_sensor_ = s; }
  void set_is_docked_sensor(binary_sensor::BinarySensor *s) { is_docked_sensor_ = s; }
  void set_has_error_sensor(binary_sensor::BinarySensor *s) { has_error_sensor_ = s; }
  void set_is_locked_sensor(binary_sensor::BinarySensor *s) { is_locked_sensor_ = s; }
  void set_is_returning_sensor(binary_sensor::BinarySensor *s) { is_returning_sensor_ = s; }
  void set_raining_sensor(binary_sensor::BinarySensor *s) { raining_sensor_ = s; }

  void set_device_name_sensor(text_sensor::TextSensor *s) { device_name_sensor_ = s; }
  void set_model_sensor(text_sensor::TextSensor *s) { model_sensor_ = s; }
  void set_serial_sensor(text_sensor::TextSensor *s) { serial_sensor_ = s; }
  void set_firmware_version_sensor(text_sensor::TextSensor *s) { firmware_version_sensor_ = s; }
  void set_battery_name_sensor(text_sensor::TextSensor *s) { battery_name_sensor_ = s; }
  void set_mower_state_sensor(text_sensor::TextSensor *s) { mower_state_sensor_ = s; }

  // Actions (HA buttons)
  void start_mowing();
  void return_to_dock();
  void stop_mowing();
  void trim_edge();
  // Front-panel buttons, call from binary_sensor on_press
  void key_start() { arm_key(KEY_START); }
  void key_home() { arm_key(KEY_HOME); }
  void key_ok();
  // Debugging: sends any JSON object as a frame
  void send_raw_json(const std::string &json);

 protected:
  // ── Link to U13 (snk_mower.cpp) ──────────────────────────────
  // WAIT_MB:   ESP is up, U13 not seen yet. POLL every 100 ms like the original.
  // HANDSHAKE: U13 is booting and sends BOOT_HEART / BOOT_INIT; each must be
  //            answered at once or U13 gives up on the display board.
  // UP:        link established, KEEPALIVE every 500 ms.
  enum class Link : uint8_t { WAIT_MB, HANDSHAKE, UP };

  void link_up(const char *why);
  void link_loop(uint32_t now);
  static void link_guard_callback(void *arg);

  void write_frame(const uint8_t *data, size_t len);
  void send_json(const JsonDocument &doc);
  void send_cmd(uint32_t cmd);
  void send_esp_info();
  void send_init();
  void send_pin();
  void send_wifi_status();
  void send_rain_status();

  static const uint8_t KEY_NONE = 0, KEY_START = 1, KEY_HOME = 2;
  void arm_key(uint8_t key);

  std::string pin_;
  Link link_{Link::WAIT_MB};
  bool pin_sent_{false};
  int pin_retries_{0};
  uint8_t armed_key_{KEY_NONE};
  uint32_t armed_at_ms_{0};
  uint32_t link_up_ms_{0};
  uint32_t last_poll_{0};
  uint32_t last_keepalive_{0};
  uint32_t last_wifi_status_{0};
  // Debug counters, logged every 10 s by log_link_stats().
  void log_link_stats(uint32_t now);
  uint32_t last_stats_ms_{0};
  uint32_t last_rx_ms_{0};
  uint32_t rx_bytes_{0};
  uint32_t rx_frames_{0};
  uint32_t rx_bad_{0};
  uint32_t tx_frames_{0};
  // The API log usually connects 30 s after a power-on, too late for the boot
  // handshake, so the first non-periodic frames are kept and logged later.
  void trace(const char *dir, const char *json);
  static constexpr size_t TRACE_MAX = 80;
  std::vector<std::string> trace_;
  void trace_loop();
  size_t trace_dump_pos_{0};
  bool trace_done_{false};

  SemaphoreHandle_t tx_mutex_{nullptr};
  esp_timer_handle_t link_guard_timer_{nullptr};
  volatile uint32_t last_tx_ms_{0};
  uint8_t keepalive_frame_[32];
  size_t keepalive_frame_len_{0};

  // ── RX (snk_mower_rx.cpp) ────────────────────────────────────
  void read_uart();
  void handle_json(const JsonDocument &doc);
  void handle_status(const JsonDocument &doc);
  void handle_device_info(const JsonDocument &doc);
  void handle_pin_result(const JsonDocument &doc);
  void handle_error_notify(const JsonDocument &doc);

  static constexpr size_t BUF_SIZE = 512;
  char rx_buf_[BUF_SIZE];
  size_t rx_index_{0};
  bool rx_in_json_{false};
  bool rx_after_amp_{false};
  bool rx_in_string_{false};

  int state_{0};
  int error_code_{0};
  int battery_percent_{0};
  bool station_{false};

  // ── Rain sensor (snk_mower_rain.cpp) ─────────────────────────
  // Resistive sensor: two electrodes driven in alternating polarity from
  // rain_drive_a_/rain_drive_b_, read through ADC1 on rain_pin_.
  // rain_state_ is what the original sends in 0x22000000: 1 dry, 2 raining.
  static const uint8_t RAIN_DRY = 1, RAIN_WET = 2;
  void setup_rain();
  void rain_loop(uint32_t now);
  void rain_sample();
  void set_rain_polarity(uint8_t polarity);

  gpio_num_t rain_drive_a_{GPIO_NUM_18};
  gpio_num_t rain_drive_b_{GPIO_NUM_5};
  int rain_threshold_{3000};
  adc_oneshot_unit_handle_t rain_adc_{nullptr};
  adc_channel_t rain_channel_{ADC_CHANNEL_0};
  uint8_t rain_polarity_{1};
  uint8_t rain_samples_left_{0};
  uint32_t rain_phase_start_ms_{0};
  uint32_t rain_last_sample_ms_{0};
  int rain_acc_{0};  // 4 x running average, as in the original
  uint8_t rain_dry_count_{0};
  uint8_t rain_wet_count_{0};
  uint8_t rain_state_{RAIN_DRY};
  uint32_t rain_adc_published_ms_{0};

  // ── Display, buzzer, HA state (snk_mower_display.cpp) ────────
  // Multiplexing: a GPTimer alarm marks every digit slot (and, when dimmed,
  // the end of the lit part of the slot); the ISR picks the frame and wakes
  // display_task_, which shifts it out over SPI.
  void setup_display();
  static bool display_alarm_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *arg);
  static void display_task(void *arg);
  void update_display_on_time();
  void display_loop(uint32_t now);
  void set_display_text(const char *text);
  void set_display_number(int value);
  void publish_mower_state(MowerState state);
  void buzz(int duration_ms);

  static constexpr uint8_t DIGITS = 4;
  gpio_num_t display_clk_{GPIO_NUM_NC};
  gpio_num_t display_mosi_{GPIO_NUM_NC};
  gpio_num_t display_cs_{GPIO_NUM_NC};
  gpio_num_t buzzer_pin_{GPIO_NUM_NC};
  gpio_num_t rain_pin_{GPIO_NUM_NC};
  spi_device_handle_t spi_dev_{nullptr};
  gptimer_handle_t display_timer_{nullptr};
  TaskHandle_t display_task_{nullptr};
  volatile uint8_t display_segments_[DIGITS]{0, 0, 0, 0};
  volatile uint8_t current_digit_{0};
  volatile uint16_t display_frame_{0};  // digit select << 8 | segments
  volatile bool display_blank_next_{false};
  volatile uint32_t display_on_us_{0};
  volatile bool display_off_{false};
  bool display_night_{false};
  uint8_t display_night_brightness_{20};
  uint32_t display_off_timeout_ms_{0};
  uint32_t last_activity_ms_{0};
  uint32_t state_display_cycle_ms_{0};
  bool state_show_alt_{false};
  bool shutdown_pending_{false};
  uint32_t shutdown_start_ms_{0};
  MowerState current_state_{MowerState::UNKNOWN};

  sensor::Sensor *battery_level_sensor_{nullptr};
  sensor::Sensor *error_code_sensor_{nullptr};
  sensor::Sensor *light_level_sensor_{nullptr};
  sensor::Sensor *work_area_sensor_{nullptr};
  sensor::Sensor *cut_area_sensor_{nullptr};
  sensor::Sensor *total_minutes_sensor_{nullptr};
  sensor::Sensor *on_minutes_sensor_{nullptr};
  sensor::Sensor *bat_health_sensor_{nullptr};
  sensor::Sensor *bat_level_bars_sensor_{nullptr};
  sensor::Sensor *rain_delay_sensor_{nullptr};
  sensor::Sensor *rain_adc_sensor_{nullptr};

  binary_sensor::BinarySensor *is_mowing_sensor_{nullptr};
  binary_sensor::BinarySensor *is_charging_sensor_{nullptr};
  binary_sensor::BinarySensor *is_docked_sensor_{nullptr};
  binary_sensor::BinarySensor *has_error_sensor_{nullptr};
  binary_sensor::BinarySensor *is_locked_sensor_{nullptr};
  binary_sensor::BinarySensor *is_returning_sensor_{nullptr};
  binary_sensor::BinarySensor *raining_sensor_{nullptr};

  text_sensor::TextSensor *device_name_sensor_{nullptr};
  text_sensor::TextSensor *model_sensor_{nullptr};
  text_sensor::TextSensor *serial_sensor_{nullptr};
  text_sensor::TextSensor *firmware_version_sensor_{nullptr};
  text_sensor::TextSensor *battery_name_sensor_{nullptr};
  text_sensor::TextSensor *mower_state_sensor_{nullptr};
};

}  // namespace snk_mower
}  // namespace esphome
