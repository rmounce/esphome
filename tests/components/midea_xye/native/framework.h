#pragma once
// Native I/O shims only. All protocol parsing, request handling, queueing and
// packet construction run the production air_conditioner.cpp unchanged.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "esphome/components/climate/climate_mode.h"
namespace esphome {
template<class T> using optional = std::optional<T>;
namespace setup_priority { constexpr float BEFORE_CONNECTION = 100; }
class PollingComponent {
 public:
  explicit PollingComponent(uint32_t interval) : interval_(interval) {}
  virtual ~PollingComponent() = default;
  virtual void setup() {}
  virtual void update() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void set_update_interval(uint32_t interval) { interval_ = interval; }
  uint32_t get_update_interval() const { return interval_; }
  void set_timeout(const char *, uint32_t, std::function<void()> fn) { timeout = std::move(fn); }
  void finish_response() { auto fn = std::move(timeout); timeout = nullptr; fn(); }
  std::function<void()> timeout;
 private:
  uint32_t interval_;
};
namespace sensor {
class Sensor {
 public:
  float state = NAN;
  unsigned publishes = 0;
  bool has_state() const { return publishes != 0; }
  float get_raw_state() const { return state; }
  void publish_state(float value) { state = value; publishes++; }
};
}
namespace number {
class Number : public sensor::Sensor {
 public:
  virtual void control(float) {}
};
}
namespace text_sensor {
class TextSensor {
 public:
  std::string state;
  unsigned publishes = 0;
  bool has_state() const { return publishes != 0; }
  const std::string &get_raw_state() const { return state; }
  void publish_state(const std::string &value) { state = value; publishes++; }
};
}
namespace binary_sensor {
class BinarySensor {
 public:
  bool state = false;
  unsigned publishes = 0;
  bool has_state() const { return publishes != 0; }
  void publish_state(bool value) { state = value; publishes++; }
};
}
namespace switch_ { class Switch : public binary_sensor::BinarySensor {}; }
namespace uart {
class UARTComponent {
 public:
  std::vector<std::vector<uint8_t>> writes;
  std::vector<uint8_t> response;
  size_t cursor = 0;
  void write_array(const uint8_t *data, size_t len) { writes.emplace_back(data, data + len); }
  void flush() {}
  bool available() const { return cursor < response.size(); }
  bool read_byte(uint8_t *out) { *out = response.at(cursor++); return true; }
};
}
namespace climate {
using ClimateModeMask = std::set<ClimateMode>;
using ClimateSwingModeMask = std::set<ClimateSwingMode>;
using ClimatePresetMask = std::set<ClimatePreset>;
class ClimateTraits {
 public:
  ClimateModeMask modes;
  ClimateSwingModeMask swings;
  ClimatePresetMask presets;
  std::set<ClimateFanMode> fans;
  void add_feature_flags(int) {}
  void set_visual_min_temperature(float) {}
  void set_visual_max_temperature(float) {}
  void set_visual_temperature_step(float) {}
  void set_supported_modes(ClimateModeMask x) { modes = x; }
  void set_supported_swing_modes(ClimateSwingModeMask x) { swings = x; }
  void set_supported_presets(ClimatePresetMask x) { presets = x; }
  void set_supported_custom_presets(std::vector<const char *>) {}
  void set_supported_custom_fan_modes(std::vector<const char *>) {}
  void add_supported_mode(ClimateMode x) { modes.insert(x); }
  void add_supported_fan_mode(ClimateFanMode x) { fans.insert(x); }
  void add_supported_swing_mode(ClimateSwingMode x) { swings.insert(x); }
  void add_supported_preset(ClimatePreset x) { presets.insert(x); }
  const auto &get_supported_modes() const { return modes; }
  const auto &get_supported_swing_modes() const { return swings; }
  const auto &get_supported_presets() const { return presets; }
};
class ClimateCall {
 public:
  optional<ClimateMode> mode;
  optional<ClimateFanMode> fan;
  optional<ClimateSwingMode> swing;
  optional<ClimatePreset> preset;
  optional<float> target;
  const auto &get_mode() const { return mode; }
  const auto &get_fan_mode() const { return fan; }
  const auto &get_swing_mode() const { return swing; }
  const auto &get_preset() const { return preset; }
  const auto &get_target_temperature() const { return target; }
};
class Climate {
 public:
  virtual ~Climate() = default;
  ClimateMode mode = CLIMATE_MODE_OFF;
  optional<ClimateFanMode> fan_mode;
  optional<ClimatePreset> preset;
  ClimateSwingMode swing_mode = CLIMATE_SWING_OFF;
  ClimateAction action = CLIMATE_ACTION_OFF;
  float target_temperature = NAN, current_temperature = NAN;
  unsigned publishes = 0;
  void publish_state() { publishes++; }
  void dump_traits_(const char *) {}
 protected:
  virtual void control(const ClimateCall &) = 0;
  virtual ClimateTraits traits() = 0;
};
}
}
