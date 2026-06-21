#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <driver/gpio.h>
#include <ArduinoJson.h>
#include <esp_timer.h>

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
};

class SnkMower : public Component, public uart::UARTDevice {
 public:
  explicit SnkMower(const std::string &pin);

  void setup() override;
  void loop() override;

  void set_display_pins(uint8_t clk, uint8_t mosi, uint8_t cs);

  void set_battery_level_sensor(sensor::Sensor *s);
  void set_battery_voltage_sensor(sensor::Sensor *s);
  void set_error_code_sensor(sensor::Sensor *s);
  void set_light_level_sensor(sensor::Sensor *s);
  void set_signal_level_sensor(sensor::Sensor *s);
  void set_work_area_sensor(sensor::Sensor *s);
  void set_cut_area_sensor(sensor::Sensor *s);
  void set_total_minutes_sensor(sensor::Sensor *s);
  void set_on_minutes_sensor(sensor::Sensor *s);
  void set_bat_health_sensor(sensor::Sensor *s);
  void set_bat_level_bars_sensor(sensor::Sensor *s);
  void set_rain_delay_sensor(sensor::Sensor *s);

  void set_is_mowing_sensor(binary_sensor::BinarySensor *s);
  void set_is_charging_sensor(binary_sensor::BinarySensor *s);
  void set_is_docked_sensor(binary_sensor::BinarySensor *s);
  void set_has_error_sensor(binary_sensor::BinarySensor *s);
  void set_is_locked_sensor(binary_sensor::BinarySensor *s);
  void set_is_returning_sensor(binary_sensor::BinarySensor *s);

  void set_device_name_sensor(text_sensor::TextSensor *s);
  void set_model_sensor(text_sensor::TextSensor *s);
  void set_serial_sensor(text_sensor::TextSensor *s);
  void set_firmware_version_sensor(text_sensor::TextSensor *s);
  void set_battery_name_sensor(text_sensor::TextSensor *s);
  void set_mower_state_sensor(text_sensor::TextSensor *s);

  void start_mowing();
  void return_to_dock();
  void buzz(int duration_ms);
  void set_buzzer_pin(gpio_num_t pin);
  void set_display_off_timeout(uint32_t minutes);
  void set_rain_pin(gpio_num_t pin);
  void set_boot_delay(uint32_t seconds);

 protected:
  std::string pin_;

  void send_json(const JsonDocument &doc);
  void send_boot();
  void send_init();
  void send_pin();
  void send_keepalive();
  void send_poll();
  void send_wifi_status();
  void send_esp_info();
  void send_error_ack();
  void send_return_home();
  void send_esp_state(int state);
  void send_rain_status(int rain);

  void handle_json(const JsonDocument &doc);

  void handle_status(const JsonDocument &doc);
  void handle_pin_result(const JsonDocument &doc);
  void handle_error_notify(const JsonDocument &doc);
  void handle_rtc(const JsonDocument &doc);
  void handle_device_info(const JsonDocument &doc);
  void handle_hw_versions(const JsonDocument &doc);
  void handle_battery_info(const JsonDocument &doc);
  void handle_map_cfg(const JsonDocument &doc);
  void handle_schedule(const JsonDocument &doc);
  void handle_rain_cfg(const JsonDocument &doc);
  void handle_multizone(const JsonDocument &doc);
  void handle_light(const JsonDocument &doc);
  void handle_power_on(const JsonDocument &doc);
  void handle_power_ready(const JsonDocument &doc);
  void handle_boot_heart(const JsonDocument &doc);
  void handle_boot_init(const JsonDocument &doc);
  void handle_lock(const JsonDocument &doc);
  void handle_start_ack(const JsonDocument &doc);
  void handle_exec_action(const JsonDocument &doc);
  void handle_shutdown(const JsonDocument &doc);
  void handle_pin_result2(const JsonDocument &doc);
  void handle_schedule_end(const JsonDocument &doc);
  void handle_setting_ack(const JsonDocument &doc, uint32_t cmd);
  void handle_signal_level(const JsonDocument &doc);
  void handle_cut_time_query(const JsonDocument &doc);
  void handle_start_time_query(const JsonDocument &doc);

  void publish_mower_state(MowerState state);
  void read_rain_sensor();

  enum class BootPhase : uint8_t {
    PRE,   // waiting for DEVICE_INFO from MB
    SYNC,  // DEVICE_INFO received, sending ESP_INFO/INIT burst
    DONE,  // handshake complete, normal keepalive operation
  };

  BootPhase boot_phase_{BootPhase::PRE};
  uint32_t phase_start_ms_{0};
  uint32_t device_info_arrived_ms_{0};
  int info_burst_count_{0};
  int init_burst_count_{0};
  bool pin_sent_{false};
  bool pin_ok_{false};
  int pin_retries_{0};
  bool power_ready_{false};

  uint32_t last_poll_{0};
  uint32_t last_keepalive_{0};
  uint32_t last_wifi_status_{0};
  uint32_t last_esp_info_{0};
  uint32_t last_esp_state_{0};
  uint32_t last_activity_ms_{0};
  uint32_t last_rain_read_{0};
  uint32_t last_boot_ms_{0};

  int state_{0};
  int error_code_{0};
  int bat_lv_{0};
  int bat_per_{0};
  int light_lv_{0};
  int signal_lv_{0};
  int work_area_{0};
  int cut_area_{0};
  int total_minutes_{0};
  int on_minutes_{0};
  int bat_health_{0};
  int rain_delay_{0};
  int rain_state_{0};
  int bat_ctime_{0};
  int bat_dtime_{0};
  int cur_minutes_{0};
  int bat_min_temp_{0};
  bool station_{false};
  int lock_{0};

  gpio_num_t buzzer_pin_{GPIO_NUM_NC};
  gpio_num_t rain_pin_{GPIO_NUM_NC};

  uint32_t display_off_timeout_ms_{0};
  bool display_off_{false};

  uint32_t boot_delay_ms_{0};

  static constexpr size_t BUF_SIZE = 512;
  char rx_buf_[BUF_SIZE];
  size_t rx_index_{0};
  bool rx_in_json_{false};
  bool rx_in_string_{false};
  char tx_buf_[BUF_SIZE];

  void finish_setup();
  void setup_display();
  void refresh_display();
  void refresh_display_impl();
  static void display_timer_callback(void *arg);
  void set_display_text(const char *text, bool colon = false);
  void set_display_battery(int percent);
  void set_charging_display(int percent);

  static uint8_t char_to_segments_(char c);

  static constexpr uint8_t DIGITS = 4;
  static constexpr uint8_t DISPLAY_REFRESH_MS = 4;

  static constexpr uint8_t CHG_FRAMES[3] = {
      0b00001000,
      0b01001000,
      0b01001001,
  };
  static constexpr uint32_t CHG_FRAME_MS = 350;

  gpio_num_t display_clk_{GPIO_NUM_NC};
  gpio_num_t display_mosi_{GPIO_NUM_NC};
  gpio_num_t display_cs_{GPIO_NUM_NC};
  esp_timer_handle_t display_timer_{nullptr};

  uint8_t display_segments_[DIGITS]{0, 0, 0, 0};
  uint8_t display_colon_{0};
  uint8_t current_digit_{0};
  uint32_t last_display_ms_{0};

  uint8_t charging_frame_{0};
  uint32_t last_charging_frame_ms_{0};

  MowerState current_state_{MowerState::UNKNOWN};
  int last_battery_percent_{0};

  sensor::Sensor *battery_level_sensor_{nullptr};
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *error_code_sensor_{nullptr};
  sensor::Sensor *light_level_sensor_{nullptr};
  sensor::Sensor *signal_level_sensor_{nullptr};
  sensor::Sensor *work_area_sensor_{nullptr};
  sensor::Sensor *cut_area_sensor_{nullptr};
  sensor::Sensor *total_minutes_sensor_{nullptr};
  sensor::Sensor *on_minutes_sensor_{nullptr};
  sensor::Sensor *bat_health_sensor_{nullptr};
  sensor::Sensor *bat_level_bars_sensor_{nullptr};
  sensor::Sensor *rain_delay_sensor_{nullptr};

  binary_sensor::BinarySensor *is_mowing_sensor_{nullptr};
  binary_sensor::BinarySensor *is_charging_sensor_{nullptr};
  binary_sensor::BinarySensor *is_docked_sensor_{nullptr};
  binary_sensor::BinarySensor *has_error_sensor_{nullptr};
  binary_sensor::BinarySensor *is_locked_sensor_{nullptr};
  binary_sensor::BinarySensor *is_returning_sensor_{nullptr};

  text_sensor::TextSensor *device_name_sensor_{nullptr};
  text_sensor::TextSensor *model_sensor_{nullptr};
  text_sensor::TextSensor *serial_sensor_{nullptr};
  text_sensor::TextSensor *firmware_version_sensor_{nullptr};
  text_sensor::TextSensor *battery_name_sensor_{nullptr};
  text_sensor::TextSensor *mower_state_sensor_{nullptr};
};

}  // namespace snk_mower
}  // namespace esphome
