#pragma once

#include "esphome/components/switch/switch.h"
#include "../air_conditioner.h"

namespace esphome {
namespace midea {
namespace ac {

class UseFahrenheitSwitch : public switch_::Switch, public Parented<AirConditioner> {
 public:
  UseFahrenheitSwitch() = default;

 protected:
  void write_state(bool state) override;
};

}  // namespace ac
}  // namespace midea
}  // namespace esphome
