#include "arlec_fan_light.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace arlec_fan_light {

static const char *const TAG = "arlec_fan_light";

static float clamp01(float value) { return clamp(value, 0.0f, 1.0f); }

static bool nearly_equal(float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; }

static uint32_t round_up_to_even(uint32_t value, uint32_t max_value) {
  value += value % 2;
  return std::min(value, max_value);
}

void ArlecFanLight::setup() {
  this->parent_->register_listener(this->color_temperature_id_, [this](const tuya::TuyaDatapoint &datapoint) {
    if (this->light_is_transitioning_()) {
      ESP_LOGD(TAG, "Light is transitioning, color temperature datapoint change ignored");
      return;
    }

    const uint32_t value = datapoint.value_uint;
    const bool echo =
        this->suppress_echo_(value, &this->command_color_temperature_, &this->command_color_temperature_at_);
    if (echo) {
      ESP_LOGV(TAG, "Suppressed color temperature command echo: %u", value);
      return;
    }
    this->raw_color_temperature_ = value;
    this->schedule_publish_();
  });

  this->parent_->register_listener(this->dimmer_id_, [this](const tuya::TuyaDatapoint &datapoint) {
    if (this->light_is_transitioning_()) {
      ESP_LOGD(TAG, "Light is transitioning, dimmer datapoint change ignored");
      return;
    }

    const uint32_t value = datapoint.value_uint;
    const bool echo = this->suppress_echo_(value, &this->command_brightness_, &this->command_brightness_at_);
    if (echo) {
      ESP_LOGV(TAG, "Suppressed brightness command echo: %u", value);
      return;
    }
    this->raw_brightness_ = value;
    this->schedule_publish_();
  });

  if (this->switch_id_.has_value()) {
    this->parent_->register_listener(*this->switch_id_, [this](const tuya::TuyaDatapoint &datapoint) {
      if (this->light_is_transitioning_()) {
        ESP_LOGD(TAG, "Light is transitioning, switch datapoint change ignored");
        return;
      }

      const bool value = datapoint.value_bool;
      const bool echo = this->suppress_echo_(value ? 1 : 0, &this->command_switch_, &this->command_switch_at_);
      if (echo) {
        ESP_LOGV(TAG, "Suppressed switch command echo: %s", ONOFF(value));
        return;
      }
      this->raw_switch_ = value ? 1 : 0;
      this->schedule_publish_();
    });
  }
}

void ArlecFanLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Arlec Fan Light:");
  ESP_LOGCONFIG(TAG, "  Dimmer datapoint: %u", this->dimmer_id_);
  ESP_LOGCONFIG(TAG, "  Color temperature datapoint: %u", this->color_temperature_id_);
  if (this->switch_id_.has_value()) {
    ESP_LOGCONFIG(TAG, "  Switch datapoint: %u", *this->switch_id_);
  }
  ESP_LOGCONFIG(TAG, "  Brightness raw range: %u-%u", this->min_value_, this->max_value_);
  ESP_LOGCONFIG(TAG, "  Color temperature raw max: %u", this->color_temperature_max_value_);
  ESP_LOGCONFIG(TAG, "  Echo suppress duration: %ums", this->echo_suppress_duration_);
  ESP_LOGCONFIG(TAG, "  Datapoint pair timeout: %ums", this->pair_timeout_);
}

light::LightTraits ArlecFanLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
  traits.set_min_mireds(this->cold_white_temperature_);
  traits.set_max_mireds(this->warm_white_temperature_);
  return traits;
}

void ArlecFanLight::write_state(light::LightState *state) {
  if (state->is_transformer_active()) {
    return;
  }

  float color_temperature;
  float brightness;
  state->current_values_as_ct(&color_temperature, &brightness);

  if (!state->current_values.is_on() && this->switch_id_.has_value()) {
    if (this->raw_switch_ != 0) {
      this->arm_switch_echo_(false);
      this->raw_switch_ = 0;
      this->parent_->set_boolean_datapoint_value(*this->switch_id_, false);
    }
    return;
  }

  uint32_t raw_color_temperature;
  uint32_t raw_brightness;
  this->encode_state_(color_temperature, brightness, &raw_color_temperature, &raw_brightness);

  this->arm_integer_echo_(raw_color_temperature, this->raw_color_temperature_, &this->command_color_temperature_,
                          &this->command_color_temperature_at_);
  if (this->raw_color_temperature_ != raw_color_temperature) {
    this->raw_color_temperature_ = raw_color_temperature;
    this->parent_->set_integer_datapoint_value(this->color_temperature_id_, raw_color_temperature);
  }

  this->arm_integer_echo_(raw_brightness, this->raw_brightness_, &this->command_brightness_,
                          &this->command_brightness_at_);
  if (this->raw_brightness_ != raw_brightness) {
    this->raw_brightness_ = raw_brightness;
    this->parent_->set_integer_datapoint_value(this->dimmer_id_, raw_brightness);
  }

  if (this->switch_id_.has_value() && this->raw_switch_ != 1) {
    this->arm_switch_echo_(true);
    this->raw_switch_ = 1;
    this->parent_->set_boolean_datapoint_value(*this->switch_id_, true);
  }
}

void ArlecFanLight::encode_state_(float color_temperature, float brightness, uint32_t *raw_color_temperature,
                                  uint32_t *raw_brightness) const {
  color_temperature = clamp01(color_temperature);
  brightness = clamp01(brightness);

  const float temp_diff = color_temperature - 0.5f;
  const float midpoint_brightness = MID_BRIGHTNESS * 2.0f * std::abs(temp_diff);

  float encoded_color_temperature = color_temperature;
  float encoded_brightness = brightness;
  if (midpoint_brightness > 0.0f) {
    if (brightness <= midpoint_brightness) {
      encoded_color_temperature = 0.5f + (temp_diff * (brightness / midpoint_brightness));
      encoded_brightness = 0.0001f;
    } else {
      encoded_brightness = (brightness - midpoint_brightness) / (1.0f - midpoint_brightness);
    }
  }

  uint32_t color_temp_int =
      static_cast<uint32_t>(roundf(clamp01(encoded_color_temperature) * this->color_temperature_max_value_));
  if (this->color_temperature_invert_) {
    color_temp_int = this->color_temperature_max_value_ - color_temp_int;
  }
  color_temp_int = round_up_to_even(color_temp_int, this->color_temperature_max_value_);

  uint32_t brightness_int = static_cast<uint32_t>(clamp01(encoded_brightness) * this->max_value_);
  brightness_int = std::max(brightness_int, this->min_value_);
  brightness_int = round_up_to_even(brightness_int, this->max_value_);

  *raw_color_temperature = color_temp_int;
  *raw_brightness = brightness_int;
}

void ArlecFanLight::decode_state_(uint32_t raw_color_temperature, uint32_t raw_brightness, float *color_temperature,
                                  float *brightness) const {
  raw_color_temperature = std::min(raw_color_temperature, this->color_temperature_max_value_);
  raw_brightness = std::min(raw_brightness, this->max_value_);

  if (this->color_temperature_invert_) {
    raw_color_temperature = this->color_temperature_max_value_ - raw_color_temperature;
  }

  float decoded_color_temperature = float(raw_color_temperature) / this->color_temperature_max_value_;
  float decoded_brightness = float(raw_brightness) / this->max_value_;

  if (raw_color_temperature != this->color_temperature_max_value_ / 2) {
    const float midpoint_brightness = MID_BRIGHTNESS * 2.0f * std::abs(decoded_color_temperature - 0.5f);
    if (raw_brightness <= this->min_value_) {
      decoded_brightness = midpoint_brightness;
      decoded_color_temperature = std::round(decoded_color_temperature);
    } else {
      decoded_brightness = midpoint_brightness + ((1.0f - midpoint_brightness) * decoded_brightness);
    }
  }

  *color_temperature = clamp01(decoded_color_temperature);
  *brightness = clamp01(decoded_brightness);
}

void ArlecFanLight::publish_from_datapoints_() {
  if (this->state_ == nullptr) {
    return;
  }

  auto call = this->state_->make_call();
  bool has_value = false;
  bool changed = false;

  if (this->raw_switch_ != SWITCH_UNKNOWN) {
    const bool is_on = this->raw_switch_ != 0;
    call.set_state(is_on);
    has_value = true;
    changed |= this->state_->remote_values.is_on() != is_on;
    if (this->raw_switch_ == 0) {
      if (changed) {
        call.perform();
      }
      return;
    }
  }

  if (this->raw_color_temperature_ != RAW_UNKNOWN && this->raw_brightness_ != RAW_UNKNOWN) {
    float color_temperature;
    float brightness;
    this->decode_state_(this->raw_color_temperature_, this->raw_brightness_, &color_temperature, &brightness);
    const float color_temperature_mireds =
        this->cold_white_temperature_ + ((this->warm_white_temperature_ - this->cold_white_temperature_) *
                                         color_temperature);
    call.set_color_temperature(color_temperature_mireds);
    call.set_brightness(brightness);
    has_value = true;
    changed |= this->state_->remote_values.get_color_mode() != light::ColorMode::COLOR_TEMPERATURE;
    changed |= !nearly_equal(this->state_->remote_values.get_brightness(), brightness, 0.01f);
    changed |= !nearly_equal(this->state_->remote_values.get_color_temperature(), color_temperature_mireds, 2.0f);
  }

  if (has_value && changed) {
    call.perform();
  }
}

void ArlecFanLight::schedule_publish_() {
  this->set_timeout("publish", this->pair_timeout_, [this] { this->publish_from_datapoints_(); });
}

bool ArlecFanLight::suppress_echo_(uint32_t value, uint32_t *command_value, uint32_t *command_at) {
  if (*command_value == RAW_UNKNOWN) {
    return false;
  }

  const uint32_t age = millis() - *command_at;
  if (age > this->echo_suppress_duration_) {
    *command_value = RAW_UNKNOWN;
    return false;
  }

  if (*command_value == value) {
    *command_value = RAW_UNKNOWN;
  }
  return true;
}

void ArlecFanLight::arm_integer_echo_(uint32_t value, uint32_t known_value, uint32_t *command_value,
                                      uint32_t *command_at) {
  if (known_value == value) {
    *command_value = RAW_UNKNOWN;
    return;
  }
  *command_value = value;
  *command_at = millis();
}

void ArlecFanLight::arm_switch_echo_(bool value) {
  if (this->raw_switch_ != SWITCH_UNKNOWN && this->raw_switch_ == (value ? 1 : 0)) {
    this->command_switch_ = RAW_UNKNOWN;
    return;
  }
  this->command_switch_ = value ? 1 : 0;
  this->command_switch_at_ = millis();
}

bool ArlecFanLight::light_is_transitioning_() const {
  return this->state_ != nullptr && this->state_->is_transformer_active();
}

}  // namespace arlec_fan_light
}  // namespace esphome
