#pragma once
#include <cstdio>
#include <string>
#include <vector>
namespace esphome {
class LogString;
inline std::vector<std::string> native_logs;
template<class... Args> void native_log(const char *format, Args... args) {
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), format, args...);
  native_logs.emplace_back(buffer);
}
}
#define ESP_LOGD(tag, ...) esphome::native_log(__VA_ARGS__)
#define ESP_LOGI(tag, ...) esphome::native_log(__VA_ARGS__)
#define ESP_LOGW(tag, ...) esphome::native_log(__VA_ARGS__)
#define ESP_LOGE(tag, ...) esphome::native_log(__VA_ARGS__)
#define ESP_LOGCONFIG(tag, ...) esphome::native_log(__VA_ARGS__)
