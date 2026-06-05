#include "localization.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstring>
#include <sstream>

namespace localization {
namespace {

const Strings kEnUs = {
    "BN Tech Virtual Scanner",
    "Scan Settings",
    "Color Mode:",
    "Black and White",
    "Grayscale",
    "Color",
    "Resolution (DPI):",
    "Page Size:",
    "Page Fill:",
    "Stretch",
    "Fit with padding",
    "Fill and crop",
    "Output Settings",
    "File transfer mode (application-managed).",
    "Transfer Mode:",
    "Native (Memory)",
    "File",
    "File Format:",
    "Output Directory:",
    "Browse...",
    "Output Filename:",
    "Cancel",
    "Scan",
    "Select Output Directory",
    "Request received",
    "You may close this browser tab now.",
    "Rotation:",
    "0 degree",
    "90 degree",
    "180 degree",
    "270 degree",
    "Virtual Scanner",
    "BN Tech Virtual Scanner"};

const Strings kZhCn = {
    "BN Tech 虚拟扫描仪",
    "扫描设置",
    "颜色模式：",
    "黑白",
    "灰度",
    "彩色",
    "分辨率 (DPI)：",
    "页面大小：",
    "页面填充：",
    "拉伸",
    "适应并留白",
    "填充并裁剪",
    "输出设置",
    "文件传输模式（由应用程序管理）。",
    "传输模式：",
    "本机（内存）",
    "文件",
    "文件格式：",
    "输出目录：",
    "浏览...",
    "输出文件名：",
    "取消",
    "扫描",
    "选择输出目录",
    "请求已收到",
    "现在可以关闭此浏览器标签页。",
    "旋转：",
    "0 度",
    "90 度",
    "180 度",
    "270 度",
    "虚拟扫描仪",
    "BN Tech 虚拟扫描仪"};

std::string trim(const std::string& value) {
  size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }
  size_t last = value.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(value[last - 1]))) {
    --last;
  }
  return value.substr(first, last - first);
}

std::string normalize(std::string value) {
  value = trim(value);
  if (!value.empty() && (value.front() == '"' || value.front() == '\'')) {
    value.erase(value.begin());
  }
  if (!value.empty() && (value.back() == '"' || value.back() == '\'')) {
    value.pop_back();
  }
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) {
                   if (c == '-') return '_';
                   return static_cast<char>(std::tolower(c));
                 });
  return value;
}

std::string readConfiguredLanguage() {
  std::ifstream in(configPath());
  if (!in.is_open()) return "";

  std::string line;
  while (std::getline(in, line)) {
    if (line.size() >= 3 &&
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF) {
      line.erase(0, 3);
    }
    size_t comment = line.find_first_of(";#");
    if (comment != std::string::npos) line.erase(comment);
    size_t equal = line.find('=');
    if (equal == std::string::npos) continue;
    std::string key = normalize(line.substr(0, equal));
    if (key == "language" || key == "lang" || key == "locale") {
      return normalize(line.substr(equal + 1));
    }
  }
  return "";
}

}  // namespace

std::string configPath() {
  char appdata[MAX_PATH] = {};
  if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH) && appdata[0]) {
    return std::string(appdata) + "\\bntech\\config.ini";
  }
  return "config.ini";
}

Language currentLanguage() {
  std::string language = readConfiguredLanguage();
  if (language == "zh_cn" || language == "zh" ||
      language == "zh_hans" || language == "chinese" ||
      language == "chinese_simplified") {
    return Language::kZhCn;
  }
  return Language::kEnUs;
}

const Strings& strings() {
  return currentLanguage() == Language::kZhCn ? kZhCn : kEnUs;
}

std::wstring toWide(const char* utf8) {
  if (utf8 == nullptr || utf8[0] == '\0') return std::wstring();
  int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
  if (len <= 0) return std::wstring();
  std::wstring wide(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), len);
  wide.resize(static_cast<size_t>(len - 1));
  return wide;
}

}  // namespace localization
