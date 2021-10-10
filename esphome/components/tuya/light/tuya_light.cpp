#include "esphome/core/log.h"
#include "tuya_light.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace tuya {

static const char *const TAG = "tuya.light";
static const float mid_bright = 0.25f;

void TuyaLight::maybe_set_temp_brightness() {
  if (this->colour_temperature_cached_ != UINT32_MAX && this->brightness_cached_ != UINT32_MAX) {
    if (this->color_temperature_invert_) {
      this->colour_temperature_cached_ = this->color_temperature_max_value_ - this->colour_temperature_cached_;
    }

    float color_temperature = float(this->colour_temperature_cached_) / this->color_temperature_max_value_;
    float brightness = float(this->brightness_cached_) / this->max_value_;

    if (colour_temperature_cached_ != this->color_temperature_max_value_ / 2) {
      if (this->brightness_cached_ <= this->min_value_) {
        brightness = mid_bright * 2.0f * std::abs(color_temperature - 0.5f);
        color_temperature = std::round(color_temperature);
      } else {
        float mid_bright_scaled = (mid_bright * 2.0f * std::abs(color_temperature - 0.5f));
        brightness = mid_bright_scaled + ((1.0f - mid_bright_scaled) * brightness);
      }
    }

    auto call = this->state_->make_call();
    call.set_color_temperature(this->cold_white_temperature_ +
                               (this->warm_white_temperature_ - this->cold_white_temperature_) * color_temperature);
    call.set_brightness(brightness);
    call.perform();

    this->colour_temperature_cached_ = UINT32_MAX;
    this->brightness_cached_ = UINT32_MAX;
  }
}

void TuyaLight::setup() {
  if (this->color_temperature_id_.has_value()) {
    this->parent_->register_listener(*this->color_temperature_id_, [this](const TuyaDatapoint &datapoint) {
      if (this->state_->current_values != this->state_->remote_values) {
        ESP_LOGD(TAG, "Light is transitioning, datapoint change ignored");
        return;
      }

      this->colour_temperature_cached_ = datapoint.value_uint;
      maybe_set_temp_brightness();
    });
  }
  if (this->dimmer_id_.has_value()) {
    this->parent_->register_listener(*this->dimmer_id_, [this](const TuyaDatapoint &datapoint) {
      if (this->state_->current_values != this->state_->remote_values) {
        ESP_LOGD(TAG, "Light is transitioning, datapoint change ignored");
        return;
      }

      this->brightness_cached_ = datapoint.value_uint;
      maybe_set_temp_brightness();
    });
  }
  if (switch_id_.has_value()) {
    this->parent_->register_listener(*this->switch_id_, [this](const TuyaDatapoint &datapoint) {
      if (this->state_->current_values != this->state_->remote_values) {
        ESP_LOGD(TAG, "Light is transitioning, datapoint change ignored");
        return;
      }

      auto call = this->state_->make_call();
      call.set_state(datapoint.value_bool);
      call.perform();
    });
  }
  if (color_id_.has_value()) {
    this->parent_->register_listener(*this->color_id_, [this](const TuyaDatapoint &datapoint) {
      if (this->state_->current_values != this->state_->remote_values) {
        ESP_LOGD(TAG, "Light is transitioning, datapoint change ignored");
        return;
      }

      switch (*this->color_type_) {
        case TuyaColorType::RGBHSV:
        case TuyaColorType::RGB: {
          auto red = parse_hex<uint8_t>(datapoint.value_string.substr(0, 2));
          auto green = parse_hex<uint8_t>(datapoint.value_string.substr(2, 2));
          auto blue = parse_hex<uint8_t>(datapoint.value_string.substr(4, 2));
          if (red.has_value() && green.has_value() && blue.has_value()) {
            auto rgb_call = this->state_->make_call();
            rgb_call.set_rgb(float(*red) / 255, float(*green) / 255, float(*blue) / 255);
            rgb_call.perform();
          }
          break;
        }
        case TuyaColorType::HSV: {
          auto hue = parse_hex<uint16_t>(datapoint.value_string.substr(0, 4));
          auto saturation = parse_hex<uint16_t>(datapoint.value_string.substr(4, 4));
          auto value = parse_hex<uint16_t>(datapoint.value_string.substr(8, 4));
          if (hue.has_value() && saturation.has_value() && value.has_value()) {
            float red, green, blue;
            hsv_to_rgb(*hue, float(*saturation) / 1000, float(*value) / 1000, red, green, blue);
            auto rgb_call = this->state_->make_call();
            rgb_call.set_rgb(red, green, blue);
            rgb_call.perform();
          }
          break;
        }
      }
    });
  }
  if (min_value_datapoint_id_.has_value()) {
    parent_->set_integer_datapoint_value(*this->min_value_datapoint_id_, this->min_value_);
  }
}

void TuyaLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Tuya Dimmer:");
  if (this->dimmer_id_.has_value()) {
    ESP_LOGCONFIG(TAG, "   Dimmer has datapoint ID %u", *this->dimmer_id_);
  }
  if (this->switch_id_.has_value()) {
    ESP_LOGCONFIG(TAG, "   Switch has datapoint ID %u", *this->switch_id_);
  }
  if (this->color_id_.has_value()) {
    ESP_LOGCONFIG(TAG, "   Color has datapoint ID %u", *this->color_id_);
  }
}

light::LightTraits TuyaLight::get_traits() {
  auto traits = light::LightTraits();
  if (this->color_temperature_id_.has_value() && this->dimmer_id_.has_value()) {
    if (this->color_id_.has_value()) {
      if (this->color_interlock_) {
        traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::COLOR_TEMPERATURE});
      } else {
        traits.set_supported_color_modes(
            {light::ColorMode::RGB_COLOR_TEMPERATURE, light::ColorMode::COLOR_TEMPERATURE});
      }
    } else
      traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
    traits.set_min_mireds(this->cold_white_temperature_);
    traits.set_max_mireds(this->warm_white_temperature_);
  } else if (this->color_id_.has_value()) {
    if (this->dimmer_id_.has_value()) {
      if (this->color_interlock_) {
        traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::WHITE});
      } else {
        traits.set_supported_color_modes({light::ColorMode::RGB_WHITE});
      }
    } else
      traits.set_supported_color_modes({light::ColorMode::RGB});
  } else if (this->dimmer_id_.has_value()) {
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
  } else {
    traits.set_supported_color_modes({light::ColorMode::ON_OFF});
  }
  return traits;
}

void TuyaLight::setup_state(light::LightState *state) { state_ = state; }

void TuyaLight::write_state(light::LightState *state) {
  float red = 0.0f, green = 0.0f, blue = 0.0f;
  float color_temperature = 0.0f, brightness = 0.0f;

  if (this->color_id_.has_value()) {
    if (this->color_temperature_id_.has_value()) {
      state->current_values_as_rgbct(&red, &green, &blue, &color_temperature, &brightness);
    } else if (this->dimmer_id_.has_value()) {
      state->current_values_as_rgbw(&red, &green, &blue, &brightness);
    } else {
      state->current_values_as_rgb(&red, &green, &blue);
    }
  } else if (this->color_temperature_id_.has_value()) {
    state->current_values_as_ct(&color_temperature, &brightness);
  } else {
    state->current_values_as_brightness(&brightness);
  }

  float temp_diff = color_temperature - 0.5f;
  float mid_bright_scaled = (mid_bright * 2.0f * std::abs(temp_diff));

  if (brightness <= mid_bright_scaled) {
    color_temperature = 0.5f + (temp_diff * (brightness / mid_bright_scaled));
    brightness = 0.0001f;
  } else {
    brightness = (brightness - mid_bright_scaled) / (1.0f - mid_bright_scaled);
  }
  this->colour_temperature_cached_ = UINT32_MAX;
  this->brightness_cached_ = UINT32_MAX;

  if (!state->current_values.is_on() && this->switch_id_.has_value()) {
    parent_->set_boolean_datapoint_value(*this->switch_id_, false);
    return;
  }

  if (brightness > 0.0f || !color_interlock_) {
    if (this->color_temperature_id_.has_value()) {
      uint32_t color_temp_int = static_cast<uint32_t>(color_temperature * this->color_temperature_max_value_);
      if (this->color_temperature_invert_) {
        color_temp_int = this->color_temperature_max_value_ - color_temp_int;
      }
      color_temp_int += color_temp_int % 2;
      parent_->set_integer_datapoint_value(*this->color_temperature_id_, color_temp_int);
    }

    if (this->dimmer_id_.has_value()) {
      auto brightness_int = static_cast<uint32_t>(brightness * this->max_value_);
      brightness_int = std::max(brightness_int, this->min_value_);
      brightness_int += brightness_int % 2;
      parent_->set_integer_datapoint_value(*this->dimmer_id_, brightness_int);
    }
  }

  if (this->color_id_.has_value() && (brightness == 0.0f || !color_interlock_)) {
    std::string color_value;
    switch (*this->color_type_) {
      case TuyaColorType::RGB: {
        char buffer[7];
        sprintf(buffer, "%02X%02X%02X", int(red * 255), int(green * 255), int(blue * 255));
        color_value = buffer;
        break;
      }
      case TuyaColorType::HSV: {
        int hue;
        float saturation, value;
        rgb_to_hsv(red, green, blue, hue, saturation, value);
        char buffer[13];
        sprintf(buffer, "%04X%04X%04X", hue, int(saturation * 1000), int(value * 1000));
        color_value = buffer;
        break;
      }
      case TuyaColorType::RGBHSV: {
        int hue;
        float saturation, value;
        rgb_to_hsv(red, green, blue, hue, saturation, value);
        char buffer[15];
        sprintf(buffer, "%02X%02X%02X%04X%02X%02X", int(red * 255), int(green * 255), int(blue * 255), hue,
                int(saturation * 255), int(value * 255));
        color_value = buffer;
        break;
      }
    }
    this->parent_->set_string_datapoint_value(*this->color_id_, color_value);
  }

  if (this->switch_id_.has_value()) {
    parent_->set_boolean_datapoint_value(*this->switch_id_, true);
  }
}

}  // namespace tuya
}  // namespace esphome
