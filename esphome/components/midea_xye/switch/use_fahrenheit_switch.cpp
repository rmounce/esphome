#include "use_fahrenheit_switch.h"
#include "../air_conditioner.h"

namespace esphome {
namespace midea {
namespace ac {

void UseFahrenheitSwitch::write_state(bool state) {
  this->parent_->set_use_fahrenheit(state);
  this->publish_state(state);
}

}  // namespace ac
}  // namespace midea
}  // namespace esphome
