#ifndef LOCALIZATION_H_
#define LOCALIZATION_H_

#include <string>

namespace localization {

enum class Language {
  kEnUs,
  kZhCn
};

struct Strings {
  const char* app_title;
  const char* scan_settings;
  const char* color_mode;
  const char* black_and_white;
  const char* grayscale;
  const char* color;
  const char* resolution_dpi;
  const char* page_size;
  const char* page_fill;
  const char* stretch;
  const char* fit_with_padding;
  const char* fill_and_crop;
  const char* output_settings;
  const char* app_managed_file_output;
  const char* transfer_mode;
  const char* native_memory;
  const char* file;
  const char* file_format;
  const char* output_directory;
  const char* browse;
  const char* output_filename;
  const char* cancel;
  const char* scan;
  const char* select_output_directory;
  const char* request_received;
  const char* close_tab_now;
  const char* rotation;
  const char* rotation_0deg;
  const char* rotation_90deg;
  const char* rotation_180deg;
  const char* rotation_270deg;
  const char* flip;
  const char* flip_none;
  const char* flip_horizontal;
  const char* flip_vertical;
  const char* product_family;
  const char* product_name;
};

Language currentLanguage();
const Strings& strings();
std::string configPath();
std::wstring toWide(const char* utf8);

}  // namespace localization

#endif  // LOCALIZATION_H_
