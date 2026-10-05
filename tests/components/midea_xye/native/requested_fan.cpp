#include "esphome/components/midea_xye/air_conditioner.h"
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace esphome;
using namespace esphome::climate;
using namespace esphome::midea::ac;

class TestAC : public AirConditioner {
 public:
  using AirConditioner::control;
  using AirConditioner::traits;
};

// Frames supplied through UART and the real deferred response callback.
std::vector<uint8_t> frame(uint8_t command, uint8_t fan = 0, uint8_t mode = OP_MODE_HEAT) {
  std::vector<uint8_t> data(RX_LEN, 0);
  data[0] = PREAMBLE;
  data[1] = command;
  data[8] = mode;
  data[9] = fan;
  data[10] = 21;
  data[11] = 80;
  data[31] = PROLOGUE;
  unsigned sum = 0;
  for (auto value : data) sum += value;
  data[30] = 0xFF - (sum & 0xFF);
  return data;
}
void checksum(std::vector<uint8_t> &data) {
  data[30] = 0;
  unsigned sum = 0;
  for (auto value : data) sum += value;
  data[30] = 0xFF - (sum & 0xFF);
}
struct Rig {
  TestAC ac;
  uart::UARTComponent uart;
  sensor::Sensor feedback, command;
  text_sensor::TextSensor legacy;
  Rig() {
    ac.set_supported_modes({CLIMATE_MODE_HEAT, CLIMATE_MODE_COOL, CLIMATE_MODE_HEAT_COOL});
    ac.set_uart_parent(&uart);
    ac.set_use_fahrenheit(false);
    ac.set_fan_feedback_sensor(&feedback);
    ac.set_fan_command_sensor(&command);
    ac.set_fan_speed_sensor(&legacy);
    ac.setup();
  }
  void respond(std::vector<uint8_t> data) {
    uart.response = std::move(data);
    uart.cursor = 0;
    ac.finish_response();
  }
  void poll(uint8_t fan, uint8_t mode = OP_MODE_HEAT) {
    ac.prepareTXData(CLIENT_COMMAND_QUERY);
    ac.sendRecv(CLIENT_COMMAND_QUERY);
    respond(frame(CLIENT_COMMAND_QUERY, fan, mode));
  }
  void request(ClimateFanMode fan) {
    ClimateCall call;
    call.mode = CLIMATE_MODE_HEAT;
    call.fan = fan;
    call.target = 24;
    ac.control(call);
  }
  void emit(uint8_t expected) {
    ac.update();
    assert(uart.writes.back()[1] == CLIENT_COMMAND_SET);
    assert(uart.writes.back()[7] == expected);
    assert(command.state == expected);
    unsigned sum = 0;
    for (auto value : uart.writes.back()) sum += value;
    assert((sum & 0xFF) == 0xFF);
    respond(frame(CLIENT_COMMAND_SET));
  }
};
unsigned count_logs(const std::string &prefix) {
  return std::count_if(native_logs.begin(), native_logs.end(), [&](const auto &line) {
    return line.find(prefix) == 0;
  });
}
int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  if (scenario == "requests") {
    for (auto pair : {std::make_pair(CLIMATE_FAN_LOW, FAN_MODE_LOW),
                      std::make_pair(CLIMATE_FAN_MEDIUM, FAN_MODE_MEDIUM),
                      std::make_pair(CLIMATE_FAN_HIGH, FAN_MODE_HIGH),
                      std::make_pair(CLIMATE_FAN_AUTO, FAN_MODE_AUTO)}) {
      Rig r;
      r.request(pair.first);
      r.emit(pair.second);
      for (auto feedback : {0x00, 0x04, 0x02, 0x01, 0x80, 0xA7}) {
        r.poll(feedback);
        assert(r.ac.fan_mode == pair.first);
        assert(r.feedback.state == feedback);
        ClimateCall call;
        call.target = 25;
        r.ac.control(call);
        r.emit(pair.second);
      }
    }
  } else if (scenario == "queued") {
    Rig r;
    r.ac.update(); // C0 in flight
    r.request(CLIMATE_FAN_LOW); // queue C3 while waiting
    r.respond(frame(CLIENT_COMMAND_QUERY, 0, OP_MODE_AUTO));
    assert(r.ac.fan_mode == CLIMATE_FAN_LOW);
    assert(r.ac.mode == CLIMATE_MODE_HEAT);
    assert(r.ac.target_temperature == 24);
    assert(r.feedback.state == 0);
    r.emit(FAN_MODE_LOW);
    r.ac.do_follow_me(23, true);
    r.ac.update();
    assert(r.uart.writes.back()[1] == 0xC6);
    assert(r.uart.writes.back()[10] == 6);
    assert(r.uart.writes.back()[11] == 23);
    ClimateCall call;
    call.mode = CLIMATE_MODE_COOL;
    r.ac.control(call); // queued behind the in-flight C6
    r.respond(frame(0xC6));
    r.emit(FAN_MODE_LOW);
    assert(r.uart.writes.back()[6] == OP_MODE_COOL);
  } else if (scenario == "partial") {
    Rig r;
    r.request(CLIMATE_FAN_MEDIUM);
    r.emit(FAN_MODE_MEDIUM);
    for (int part = 0; part != 4; part++) {
      r.ac.update(); // query in flight
      ClimateCall call;
      if (part == 0) call.target = 26;
      if (part == 1) call.mode = CLIMATE_MODE_COOL;
      if (part == 2) call.preset = CLIMATE_PRESET_BOOST;
      if (part == 3) call.swing = CLIMATE_SWING_VERTICAL;
      r.ac.control(call);
      r.respond(frame(CLIENT_COMMAND_QUERY, FAN_MODE_HIGH));
      assert(r.ac.fan_mode == CLIMATE_FAN_MEDIUM);
      if (part == 0) assert(r.ac.target_temperature == 26);
      if (part == 1) assert(r.ac.mode == CLIMATE_MODE_COOL);
      if (part == 2) assert(r.ac.preset == CLIMATE_PRESET_BOOST);
      if (part == 3) assert(r.ac.swing_mode == CLIMATE_SWING_VERTICAL);
      r.emit(FAN_MODE_MEDIUM);
      if (part == 0) assert(r.uart.writes.back()[8] == 26);
      if (part == 1) assert(r.uart.writes.back()[6] == OP_MODE_COOL);
      if (part == 2) assert(r.uart.writes.back()[11] & MODE_FLAG_AUX_HEAT);
      if (part == 3) assert(r.uart.writes.back()[11] & MODE_FLAG_SWING);
    }
  } else if (scenario == "full-auto") {
    Rig r;
    r.request(CLIMATE_FAN_HIGH);
    r.emit(FAN_MODE_HIGH);
    ClimateCall call;
    call.mode = CLIMATE_MODE_HEAT_COOL;
    call.fan = CLIMATE_FAN_LOW;
    r.ac.control(call);
    assert(r.ac.fan_mode == CLIMATE_FAN_AUTO);
    r.emit(FAN_MODE_AUTO);
    call.mode = CLIMATE_MODE_HEAT;
    call.fan.reset();
    r.ac.control(call);
    r.emit(FAN_MODE_AUTO);
    r.request(CLIMATE_FAN_HIGH);
    r.emit(FAN_MODE_HIGH);
    r.poll(FAN_MODE_LOW, OP_MODE_AUTO); // external full-auto mode
    assert(r.ac.fan_mode == CLIMATE_FAN_AUTO);
    call.mode = CLIMATE_MODE_HEAT;
    r.ac.control(call);
    r.emit(FAN_MODE_AUTO);
  } else if (scenario == "off") {
    Rig r;
    r.request(CLIMATE_FAN_LOW);
    r.emit(FAN_MODE_LOW);
    ClimateCall call;
    call.mode = CLIMATE_MODE_OFF;
    r.ac.control(call);
    r.emit(FAN_MODE_LOW);
    assert(r.uart.writes.back()[6] == OP_MODE_OFF);
    r.poll(0, OP_MODE_OFF);
    assert(r.ac.fan_mode == CLIMATE_FAN_LOW);
    assert(r.feedback.state == 0);
    r.ac.do_power_on(); // configured default on-mode is full-auto
    r.emit(FAN_MODE_AUTO);
    assert(r.ac.fan_mode == CLIMATE_FAN_AUTO);
    assert(!r.ac.traits().fans.count(CLIMATE_FAN_OFF));
  } else if (scenario == "startup") {
    Rig r;
    assert(r.ac.fan_mode == CLIMATE_FAN_AUTO);
    assert(!r.command.has_state());
    for (int i = 0; i != 20; i++) {
      r.ac.update();
      const auto cmd = r.uart.writes.back()[1];
      assert(cmd == CLIENT_COMMAND_QUERY || cmd == 0xC4);
      r.respond(frame(cmd, FAN_MODE_HIGH));
    }
    assert(r.ac.fan_mode == CLIMATE_FAN_AUTO);
    assert(!r.command.has_state());
    ClimateCall call;
    call.target = 25;
    r.ac.control(call);
    r.emit(FAN_MODE_AUTO);
  } else if (scenario == "diagnostics") {
    Rig r;
    for (int i = 0; i != 20; i++) r.poll(0xA7);
    assert(r.feedback.state == 0xA7);
    assert(r.ac.action == CLIMATE_ACTION_HEATING);
    assert(r.feedback.publishes == 1);
    assert(count_logs("C0 fan feedback byte:") == 1);
    assert(count_logs("C3 fan command byte:") == 0);
    r.poll(0x80);
    assert(r.feedback.state == 0x80);
    assert(r.legacy.state == "Off");
    assert(r.ac.action == CLIMATE_ACTION_IDLE);
    assert(count_logs("C0 fan feedback byte:") == 2);
    r.request(CLIMATE_FAN_HIGH);
    r.ac.setACParams(); // construction alone must not publish command
    assert(!r.command.has_state());
    r.emit(FAN_MODE_HIGH);
    assert(count_logs("C3 fan command byte:") == 1);
    r.request(CLIMATE_FAN_HIGH);
    r.emit(FAN_MODE_HIGH);
    assert(r.command.publishes == 1);
    assert(count_logs("C3 fan command byte:") == 2);
    auto invalid = frame(CLIENT_COMMAND_QUERY, 0xFF);
    invalid[30] ^= 1;
    r.ac.prepareTXData(CLIENT_COMMAND_QUERY);
    r.ac.sendRecv(CLIENT_COMMAND_QUERY);
    r.respond(invalid);
    assert(r.feedback.state == 0x80);
  } else if (scenario == "fahrenheit") {
    Rig r;
    r.ac.set_use_fahrenheit(true);
    r.request(CLIMATE_FAN_LOW);
    r.emit(FAN_MODE_LOW);
    assert(r.uart.writes.back()[8] == uint8_t(75 + 0x87));
    r.ac.prepareTXData(0xC4);
    r.ac.sendRecv(0xC4);
    ClimateCall call;
    call.target = 25;
    r.ac.control(call);
    auto data = frame(0xC4);
    data[18] = 70 + 0x87;
    checksum(data);
    r.respond(data);
    assert(r.ac.target_temperature == 25);
    r.emit(FAN_MODE_LOW);
    assert(r.uart.writes.back()[8] == uint8_t(77 + 0x87));
  } else if (scenario == "pressure") {
    Rig r;
    r.ac.set_static_pressure(5); // rejected before confirmed off
    r.ac.update();
    assert(r.uart.writes.back()[1] == CLIENT_COMMAND_QUERY);
    r.respond(frame(CLIENT_COMMAND_QUERY, 0, OP_MODE_OFF));
    r.ac.set_static_pressure(5);
    r.ac.update();
    assert(r.uart.writes.back()[1] == 0xC6);
    assert(r.uart.writes.back()[8] == 0x15);
    assert(!r.command.has_state());
    r.respond(frame(0xC6));
  } else {
    return 2;
  }
  std::cout << scenario << " passed\n";
}
