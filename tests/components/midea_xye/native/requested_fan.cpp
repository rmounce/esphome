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
  sensor::Sensor feedback, command, outside;
  binary_sensor::BinarySensor compressor;
  StaticPressureNumber pressure;
  binary_sensor::BinarySensor fresh;
  text_sensor::TextSensor legacy;
  Rig() {
    ac.set_supported_modes({CLIMATE_MODE_HEAT, CLIMATE_MODE_COOL, CLIMATE_MODE_HEAT_COOL});
    ac.set_uart_parent(&uart);
    ac.set_use_fahrenheit(false);
    ac.set_fan_feedback_sensor(&feedback);
    ac.set_fan_command_sensor(&command);
    ac.set_fan_speed_sensor(&legacy);
    ac.set_status_fresh_sensor(&fresh);
    ac.set_compressor_status_sensor(&compressor);
    ac.set_outdoor_temperature_sensor(&outside);
    ac.set_static_pressure_number(&pressure);
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
  if (scenario == "freshness") {
    Rig r;
    assert(r.fresh.has_state() && !r.fresh.state && r.fresh.publishes == 1);
    r.poll(0);
    assert(r.fresh.state && r.fresh.publishes == 2);
    r.ac.advance(29000);
    r.poll(0);  // Reset the named deadline without another state publication.
    assert(r.fresh.publishes == 2);
    r.ac.advance(1000);  // Original deadline would have expired here.
    assert(r.fresh.state);
    for (uint8_t cmd : {0xC3, 0xC4, 0xC6}) {
      r.ac.prepareTXData(cmd);
      r.ac.sendRecv(cmd);
      r.respond(frame(cmd));
    }
    auto invalid = frame(CLIENT_COMMAND_QUERY);
    invalid[30] ^= 1;
    r.ac.prepareTXData(CLIENT_COMMAND_QUERY);
    r.ac.sendRecv(CLIENT_COMMAND_QUERY);
    r.respond(invalid);
    r.ac.advance(28999);
    assert(r.fresh.state);
    r.ac.advance(1); // No polling: scheduler alone expires the bit.
    assert(!r.fresh.state && r.fresh.publishes == 3);
    r.ac.advance(60000);
    assert(r.fresh.publishes == 3);
    r.ac.prepareTXData(0xC3);  // Unexpected C0 while waiting for an ack cannot refresh.
    r.ac.sendRecv(0xC3);
    r.respond(frame(CLIENT_COMMAND_QUERY));
    assert(!r.fresh.state);
    r.poll(0);
    assert(r.fresh.state && r.fresh.publishes == 4);
  } else if (scenario == "requests") {
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
  } else if (scenario == "snapshot-c3") {
    Rig r;
    r.ac.update(); // C0 outstanding; every later request must retain its payload.
    ClimateCall call;
    call.mode = CLIMATE_MODE_OFF;
    call.target = 20;
    call.fan = CLIMATE_FAN_LOW;
    r.ac.control(call);
    call.mode = CLIMATE_MODE_COOL;
    call.target = 21;
    call.fan = CLIMATE_FAN_HIGH;
    r.ac.control(call);
    r.ac.control(call); // Intentional retransmission, not coalesced.
    r.respond(frame(CLIENT_COMMAND_QUERY, 0, OP_MODE_OFF));
    r.emit(FAN_MODE_LOW);
    assert(r.uart.writes.back()[6] == OP_MODE_OFF && r.uart.writes.back()[8] == 20);
    r.emit(FAN_MODE_HIGH);
    assert(r.uart.writes.back()[6] == OP_MODE_COOL && r.uart.writes.back()[8] == 21);
    r.emit(FAN_MODE_HIGH);
    assert(r.uart.writes.back()[6] == OP_MODE_COOL && r.uart.writes.back()[8] == 21);
    assert(r.uart.writes.size() == 4);
  } else if (scenario == "snapshot-follow-me") {
    Rig r;
    r.poll(0, OP_MODE_COOL);
    r.ac.update(); // C4 in flight.
    r.ac.do_follow_me(22, false);
    r.ac.do_follow_me(23, true);
    r.ac.do_follow_me(21, false);
    r.respond(frame(0xC4));
    for (int i = 0; i != 3; ++i) {
      r.ac.update();
      const auto &tx = r.uart.writes.back();
      assert(tx[1] == 0xC6);
      assert(tx[11] == std::vector<int>({22, 23, 21})[i]);
      assert(tx[10] == (i < 2 ? 6 : 2));
      r.respond(frame(0xC6));
    }
  } else if (scenario == "snapshot-pressure") {
    Rig r;
    r.poll(0, OP_MODE_OFF);
    r.ac.update(); // C4 in flight.
    r.ac.set_static_pressure(1);
    r.ac.set_static_pressure(4);
    r.respond(frame(0xC4));
    for (uint8_t pressure : {1, 4}) {
      r.ac.update();
      assert(r.uart.writes.back()[1] == 0xC6);
      assert(r.uart.writes.back()[8] == (0x10 | pressure));
      r.respond(frame(0xC6));
    }
  } else if (scenario == "unexpected-status") {
    Rig r;
    r.request(CLIMATE_FAN_LOW);
    r.emit(FAN_MODE_LOW);
    r.ac.do_follow_me(23);
    r.ac.update();
    const auto old_mode = r.ac.mode;
    const auto old_target = r.ac.target_temperature;
    auto wrong = frame(CLIENT_COMMAND_QUERY, 0xA7, OP_MODE_OFF);
    wrong[19] = 3; // Would falsely publish compressor and fan ON in old parser.
    checksum(wrong);
    r.respond(wrong);
    assert(r.ac.mode == old_mode && r.ac.target_temperature == old_target);
    assert(!r.feedback.has_state() && !r.compressor.has_state() && !r.fresh.state);
    r.ac.set_static_pressure(1); // Unexpected OFF must not grant permission.
    r.ac.update();
    assert(r.uart.writes.back()[1] == CLIENT_COMMAND_QUERY);
  } else if (scenario == "wrong-extension-reply") {
    Rig r;
    r.ac.prepareTXData(0xC4);
    r.ac.sendRecv(0xC4);
    auto wrong = frame(0xC6);
    wrong[21] = 0x68;
    wrong[24] = 1;
    checksum(wrong);
    r.respond(wrong);
    assert(!r.outside.has_state() && !r.pressure.has_state());
    r.ac.prepareTXData(0xC4);
    r.ac.sendRecv(0xC4);
    wrong[1] = 0xC4;
    checksum(wrong);
    r.respond(wrong);
    assert(r.outside.state == 32 && r.pressure.state == 1); // Decoder unchanged.
  } else if (scenario == "c6-ack-only") {
    Rig r;
    r.ac.set_use_fahrenheit(true);
    r.ac.mode = CLIMATE_MODE_COOL;
    r.ac.target_temperature = 24;
    r.outside.publish_state(17.5);
    r.pressure.publish_state(1);
    r.ac.do_follow_me(21);
    r.ac.update();
    auto ack = frame(0xC6);
    ack[18] = 70 + 0x87;
    ack[21] = 0x68; // Would incorrectly publish ambient32 degrees.
    ack[24] = 2; // Must not confirm new pressure from ACK data.
    checksum(ack);
    r.respond(ack);
    assert(r.outside.state == 17.5 && r.outside.publishes == 1);
    assert(r.pressure.state == 1 && r.pressure.publishes == 1);
    assert(r.ac.target_temperature == 24);
    r.ac.prepareTXData(0xC4);
    r.ac.sendRecv(0xC4);
    auto observation = frame(0xC4);
    observation[18] = 75 + 0x87;
    observation[21] = 0x49;
    observation[24] = 0x22;
    checksum(observation);
    r.respond(observation);
    assert(r.outside.state == 16.5);
    assert(r.pressure.state == 2 && r.pressure.publishes == 2);
    assert(r.ac.target_temperature > 23 && r.ac.target_temperature < 25);
  } else if (scenario == "stale-off" || scenario == "stale-off-no-sensor") {
    Rig r;
    if (scenario == "stale-off-no-sensor") r.ac.set_status_fresh_sensor(nullptr);
    r.poll(0, OP_MODE_OFF);
    r.ac.set_static_pressure(1); // Admission fresh at enqueue.
    r.ac.advance(30000);
    const auto before = r.uart.writes.size();
    r.ac.update(); // Admission must be checked again at TX.
    assert(r.uart.writes.size() == before);
    assert(count_logs("XYE pressure cancelled") == 1);
    r.ac.set_static_pressure(4); // Stale OFF must also reject new admission.
    r.ac.update();
    assert(r.uart.writes.back()[1] == CLIENT_COMMAND_QUERY);
  } else if (scenario == "off-generation") {
    Rig r;
    r.ac.update(); // Old query was sent before a new COOL intent.
    ClimateCall call;
    call.mode = CLIMATE_MODE_COOL;
    r.ac.control(call);
    r.respond(frame(CLIENT_COMMAND_QUERY, 0, OP_MODE_OFF));
    r.ac.set_static_pressure(1);
    r.emit(FAN_MODE_AUTO);
    r.ac.update();
    assert(r.uart.writes.back()[1] == CLIENT_COMMAND_QUERY);
    assert(count_logs("Cannot set static pressure") == 1);
  } else if (scenario == "pressure-cancelled-by-mode") {
    Rig r;
    r.poll(0, OP_MODE_OFF);
    r.ac.set_static_pressure(1);
    ClimateCall call;
    call.mode = CLIMATE_MODE_COOL;
    r.ac.control(call);
    const auto before = r.uart.writes.size();
    r.ac.update();
    assert(r.uart.writes.size() == before); // No C6 pressure transmission.
    r.emit(FAN_MODE_AUTO); // Later command survives cancellation.
    assert(r.uart.writes.back()[6] == OP_MODE_COOL);
  } else if (scenario == "invalid-ack" || scenario == "wrong-ack") {
    Rig r;
    r.request(CLIMATE_FAN_LOW);
    r.ac.update();
    r.ac.do_follow_me(22);
    r.ac.do_follow_me(21);
    auto ack = frame(scenario == "invalid-ack" ? CLIENT_COMMAND_SET : 0xC4);
    if (scenario == "invalid-ack") ack[30] ^= 1;
    r.respond(ack);
    assert(count_logs("XYE transaction failed") == 1);
    assert(!r.outside.has_state());
    for (uint8_t temperature : {22, 21}) {
      r.ac.update();
      assert(r.uart.writes.back()[1] == 0xC6);
      assert(r.uart.writes.back()[11] == temperature);
      r.respond(frame(0xC6));
    }
    assert(count_logs("C3 fan command byte:") == 1); // No unsafe blind replay.
  } else if (scenario.rfind("rx-length-", 0) == 0) {
    const size_t length = std::stoul(scenario.substr(10));
    Rig r;
    r.ac.update();
    r.request(CLIMATE_FAN_LOW);
    auto data = frame(CLIENT_COMMAND_QUERY);
    data.resize(length, 0xAA);
    r.respond(data);
    assert(!r.fresh.state && !r.compressor.has_state());
    assert(!r.uart.available()); // Extra bytes all consumed, no 8-bit wrap/hang.
    r.emit(FAN_MODE_LOW); // Length failure did not clear queued intent.
    assert(count_logs("XYE transaction failed") == 1);
  } else if (scenario == "unknown-mode") {
    Rig r;
    r.poll(0, 0xFF);
    assert(!r.fresh.state && !r.compressor.has_state());
    r.ac.set_static_pressure(1);
    r.ac.update();
    assert(r.uart.writes.back()[1] == 0xC4);
    assert(count_logs("Cannot set static pressure") == 1);
  } else if (scenario == "configured-timeout") {
    Rig r;
    r.ac.set_response_timeout(450);
    r.ac.update();
    r.uart.response = frame(CLIENT_COMMAND_QUERY, 0, OP_MODE_OFF);
    r.ac.advance(100);
    assert(!r.fresh.state);
    r.ac.advance(349);
    assert(!r.fresh.state);
    r.ac.advance(1);
    assert(r.fresh.state);
  } else if (scenario == "stale-rx") {
    Rig r;
    r.uart.response = frame(CLIENT_COMMAND_QUERY, 0, OP_MODE_OFF);
    r.request(CLIMATE_FAN_LOW);
    r.emit(FAN_MODE_LOW);
    assert(count_logs("XYE discarded stale RX") == 1);
    assert(!r.fresh.state);
    assert(!r.feedback.has_state());
  } else if (scenario == "init-at-transmit") {
    Rig r;
    r.poll(0, OP_MODE_COOL);
    r.ac.do_follow_me(21);
    r.ac.update();
    assert(r.uart.writes.back()[10] == 6);
    r.respond(frame(0xC6));
    r.ac.update(); // Query in flight.
    r.ac.do_follow_me(20); // Before new mode, should not reinitialize.
    ClimateCall call;
    call.mode = CLIMATE_MODE_COOL;
    r.ac.control(call);
    r.ac.do_follow_me(19); // After actual C3 TX, should reinitialize.
    r.respond(frame(CLIENT_COMMAND_QUERY, 0, OP_MODE_COOL));
    r.ac.update();
    assert(r.uart.writes.back()[10] == 2 && r.uart.writes.back()[11] == 20);
    r.respond(frame(0xC6));
    r.emit(FAN_MODE_AUTO);
    r.ac.update();
    assert(r.uart.writes.back()[10] == 6 && r.uart.writes.back()[11] == 19);
    r.respond(frame(0xC6));
  } else if (scenario == "queue-bound") {
    Rig r;
    r.ac.update();
    for (int i = 0; i != 65; ++i) {
      ClimateCall call;
      call.mode = CLIMATE_MODE_COOL;
      call.target = 18 + (i % 10);
      r.ac.control(call);
    }
    assert(count_logs("XYE enqueue rejected") == 1);
    r.respond(frame(CLIENT_COMMAND_QUERY));
    for (int i = 0; i != 64; ++i) {
      r.emit(FAN_MODE_AUTO);
      assert(r.uart.writes.back()[8] == 18 + (i % 10));
    }
    assert(count_logs("C3 fan command byte:") == 64);
  } else {
    return 2;
  }
  std::cout << scenario << " passed\n";
}
