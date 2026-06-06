#pragma once

#include <cstdint>

#include "esphome/components/light/light_output.h"
#include "esphome/components/tuya/tuya.h"
#include "esphome/core/component.h"

namespace esphome {
namespace arlec_fan_light {

class ArlecFanLight : public Component, public light::LightOutput {
 public:
  void setup() override;
  void dump_config() override;

  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override { this->state_ = state; }
  void write_state(light::LightState *state) override;

  void set_tuya_parent(tuya::Tuya *parent) { this->parent_ = parent; }
  void set_dimmer_id(uint8_t dimmer_id) { this->dimmer_id_ = dimmer_id; }
  void set_switch_id(uint8_t switch_id) { this->switch_id_ = switch_id; }
  void set_color_temperature_id(uint8_t color_temperature_id) { this->color_temperature_id_ = color_temperature_id; }
  void set_color_temperature_invert(bool invert) { this->color_temperature_invert_ = invert; }
  void set_min_value(uint32_t min_value) { this->min_value_ = min_value; }
  void set_max_value(uint32_t max_value) { this->max_value_ = max_value; }
  void set_color_temperature_max_value(uint32_t max_value) { this->color_temperature_max_value_ = max_value; }
  void set_cold_white_temperature(float temperature) { this->cold_white_temperature_ = temperature; }
  void set_warm_white_temperature(float temperature) { this->warm_white_temperature_ = temperature; }
  void set_echo_suppress_duration(uint32_t duration) { this->echo_suppress_duration_ = duration; }
  void set_pair_timeout(uint32_t timeout) { this->pair_timeout_ = timeout; }

 protected:
  static constexpr uint32_t RAW_UNKNOWN = UINT32_MAX;
  static constexpr int8_t SWITCH_UNKNOWN = -1;
  static constexpr float MID_BRIGHTNESS = 0.25f;

  void encode_state_(float color_temperature, float brightness, uint32_t *raw_color_temperature,
                     uint32_t *raw_brightness) const;
  void decode_state_(uint32_t raw_color_temperature, uint32_t raw_brightness, float *color_temperature,
                     float *brightness) const;
  void publish_from_datapoints_();
  void schedule_publish_();

  bool suppress_echo_(uint32_t value, uint32_t *command_value, uint32_t *command_at);
  void arm_integer_echo_(uint32_t value, uint32_t known_value, uint32_t *command_value, uint32_t *command_at);
  void arm_switch_echo_(bool value);
  bool light_is_transitioning_() const;

  tuya::Tuya *parent_{nullptr};
  light::LightState *state_{nullptr};
  uint8_t dimmer_id_{0};
  optional<uint8_t> switch_id_{};
  uint8_t color_temperature_id_{0};

  uint32_t min_value_{0};
  uint32_t max_value_{255};
  uint32_t color_temperature_max_value_{255};
  float cold_white_temperature_{};
  float warm_white_temperature_{};
  bool color_temperature_invert_{false};
  uint32_t echo_suppress_duration_{1000};
  uint32_t pair_timeout_{200};

  uint32_t raw_brightness_{RAW_UNKNOWN};
  uint32_t raw_color_temperature_{RAW_UNKNOWN};
  int8_t raw_switch_{SWITCH_UNKNOWN};

  uint32_t command_brightness_{RAW_UNKNOWN};
  uint32_t command_brightness_at_{0};
  uint32_t command_color_temperature_{RAW_UNKNOWN};
  uint32_t command_color_temperature_at_{0};
  uint32_t command_switch_{RAW_UNKNOWN};
  uint32_t command_switch_at_{0};
};

}  // namespace arlec_fan_light
}  // namespace esphome
