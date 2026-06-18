#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

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

  void set_is_mowing_sensor(binary_sensor::BinarySensor *s);
  void set_is_charging_sensor(binary_sensor::BinarySensor *s);
  void set_is_docked_sensor(binary_sensor::BinarySensor *s);
  void set_has_error_sensor(binary_sensor::BinarySensor *s);

  void start_mowing();
  void return_to_dock();

 protected:
  std::string pin_;

  // --- UART ---
  void send_frame(uint8_t cmd, const uint8_t *payload, size_t len);
  void send_pin();
  void poll_status();
  void poll_battery();

  void handle_response(uint8_t cmd, const uint8_t *data, size_t len);
  void handle_status_response(const uint8_t *data, size_t len);
  void handle_battery_response(const uint8_t *data, size_t len);
  void handle_pin_response(const uint8_t *data, size_t len);
  void handle_error_info(const uint8_t *data, size_t len);

  void publish_mower_state(MowerState state);

  bool pin_sent_{false};
  bool pin_ok_{false};
  int pin_retries_{0};

  bool expecting_response_{false};
  uint32_t last_poll_{0};

  int8_t rx_state_{-1};
  uint8_t rx_cmd_{0};
  uint8_t rx_buf_[32];
  size_t rx_len_{0};
  size_t rx_index_{0};

  // --- Display (3× 74HC595 → 4-digit 7-segment) ---
  void setup_display();
  void refresh_display();
  void set_display_text(const char *text, bool colon = false);
  void set_display_battery(int percent);

  void set_charging_display(int percent);

  uint8_t char_to_segments_(char c) const;

  static constexpr uint8_t DIGITS = 4;
  static constexpr uint8_t DISPLAY_REFRESH_MS = 4;

  // Charging animation: segment patterns for digit 0
  static constexpr uint8_t CHG_FRAMES[3] = {
      0b00001000,  // frame 0: segment D (bottom)       →
      0b01001000,  // frame 1: segments D + G (bottom + middle)
      0b01001001,  // frame 2: segments D + G + A (bottom + middle + top)
  };
  static constexpr uint32_t CHG_FRAME_MS = 350;

  ISRInternalGPIOPin display_clk_;
  ISRInternalGPIOPin display_mosi_;
  ISRInternalGPIOPin display_cs_;

  uint8_t display_segments_[DIGITS]{0, 0, 0, 0};
  uint8_t display_colon_{0};
  uint8_t current_digit_{0};
  uint32_t last_display_ms_{0};

  uint8_t charging_frame_{0};
  uint32_t last_charging_frame_ms_{0};

  MowerState current_state_{MowerState::UNKNOWN};
  int last_battery_percent_{0};

  // --- Sensors ---
  sensor::Sensor *battery_level_sensor_{nullptr};
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *error_code_sensor_{nullptr};
  binary_sensor::BinarySensor *is_mowing_sensor_{nullptr};
  binary_sensor::BinarySensor *is_charging_sensor_{nullptr};
  binary_sensor::BinarySensor *is_docked_sensor_{nullptr};
  binary_sensor::BinarySensor *has_error_sensor_{nullptr};
};

}  // namespace snk_mower
}  // namespace esphome
