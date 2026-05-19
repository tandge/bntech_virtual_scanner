// Virtual scanner implementation.
// Reads images from %APPDATA%\bntech\images, converts them to the
// requested TWAIN pixel type and resolution, and outputs DIB-compatible
// scan data line by line (bottom-up, 4-byte aligned).

#include "virtual_scanner.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#pragma comment(lib, "shlwapi.lib")
#ifdef DS_DEBUG_LOG
  #define DS_LOG(msg) OutputDebugStringA(msg)
#else
  #define DS_LOG(msg) ((void)0)
#endif
HMODULE VirtualScanner::g_hinstance = nullptr;

VirtualScanner::VirtualScanner()
    : dib_(nullptr),
      scan_line_(0),
      locked_(false),
      dest_bytes_per_row_(0),
      row_offset_(0),
      current_image_index_(0) {
  default_image_path_ = getDefaultImagePath();
  scanImageDirectory();
  loadImageIndex();
  FreeImage_Initialise();
  resetScanner();
}
VirtualScanner::~VirtualScanner() {
  if (dib_ != nullptr) {
    FreeImage_Unload(dib_);
  }
  FreeImage_DeInitialise();
}
std::string VirtualScanner::getDefaultImagePath() const {
  char module_path[MAX_PATH] = {};
  GetModuleFileNameA(g_hinstance, module_path, MAX_PATH);
  PathRemoveFileSpecA(module_path);
  std::string result = std::string(module_path) + "\\TWAIN_logo.png";
  return result;
}
// Scans %APPDATA%\bntech\images for supported image files (PNG, JPG,
// JPEG, BMP, TIF, TIFF).  Sorted alphabetically.  Falls back to the
// default TWAIN_logo.png if no images are found.
void VirtualScanner::scanImageDirectory() {
  image_list_.clear();
  char appdata_path[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appdata_path))) {
    image_dir_ = std::string(appdata_path) + "\\bntech\\images";
    DWORD attrs = GetFileAttributesA(image_dir_.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
      SHCreateDirectoryExA(nullptr, image_dir_.c_str(), nullptr);
    }
    std::string search_pattern = image_dir_ + "\\*.*";
    WIN32_FIND_DATAA find_data = {};
    HANDLE find_handle = FindFirstFileA(search_pattern.c_str(), &find_data);
    if (find_handle != INVALID_HANDLE_VALUE) {
      do {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
          std::string filename = find_data.cFileName;
          auto dot_pos = filename.find_last_of('.');
          if (dot_pos != std::string::npos) {
            std::string ext = filename.substr(dot_pos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" ||
                ext == "tif" || ext == "tiff") {
              image_list_.push_back(image_dir_ + "\\" + filename);
            }
          }
        }
      } while (FindNextFileA(find_handle, &find_data));
      FindClose(find_handle);
    }
    std::sort(image_list_.begin(), image_list_.end());
    std::cerr << "ds: Scanned image dir: " << image_dir_
              << " found " << image_list_.size() << " image(s)" << std::endl;
  }
  if (image_list_.empty()) {
    image_list_.push_back(default_image_path_);
  }
}
bool VirtualScanner::resetScanner() {
  unlock();
  scan_line_ = 0;
  dest_bytes_per_row_ = 0;
  row_offset_ = 0;
  settings_.pixel_type = TWPT_RGB;
  settings_.x_resolution = 300.0f;
  settings_.y_resolution = 300.0f;
  if (dib_ != nullptr) {
    FreeImage_Unload(dib_);
    dib_ = nullptr;
  }
  return true;
}
// Loads the next image file from the directory and advances the index.
// Re-scans the directory each time to pick up newly added images.
bool VirtualScanner::acquireImage() {
  if (dib_ != nullptr) {
    FreeImage_Unload(dib_);
    dib_ = nullptr;
  }
  scanImageDirectory();
  if (current_image_index_ >= image_list_.size()) {
    return false;
  }
  const std::string& image_path = image_list_[current_image_index_];
  current_image_index_++;
  saveImageIndex();
  if (GetFileAttributesA(image_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::cerr << "ds: Image file not found: " << image_path << std::endl;
    return false;
  }
  FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(image_path.c_str());
  if (fif == FIF_UNKNOWN) {
    fif = FreeImage_GetFIFFromFilename(image_path.c_str());
  }
  if (fif == FIF_UNKNOWN) {
    std::cerr << "ds: Unknown image format: " << image_path << std::endl;
    return false;
  }
  int load_flags = 0;
  if (fif == FIF_PNG) {
    load_flags = PNG_IGNOREGAMMA;  
  } else if (fif == FIF_JPEG) {
    load_flags = JPEG_ACCURATE;    
  }
  dib_ = FreeImage_Load(fif, image_path.c_str(), load_flags);
  if (dib_ == nullptr) {
    std::cerr << "ds: Failed to load image: " << image_path << std::endl;
    return false;
  }
  if (!preScanPrep()) {
    return false;
  }
  return true;
}
// Converts the loaded image to the requested pixel type and calculates
// the DIB-compatible byte-per-row values for scan output.
// RGB images have R/B channels swapped (FreeImage uses BGR internally).
// BW and gray conversions use FreeImage's threshold/convert functions.
bool VirtualScanner::preScanPrep() {
  if (dib_ == nullptr) return false;
  if (FreeImage_GetBPP(dib_) != 24) {
    FIBITMAP* converted = FreeImage_ConvertTo24Bits(dib_);
    FreeImage_Unload(dib_);
    dib_ = converted;
    if (dib_ == nullptr) return false;
  }
  if (settings_.pixel_type == TWPT_RGB) {
    int w = FreeImage_GetWidth(dib_);
    int h = FreeImage_GetHeight(dib_);
    for (int y = 0; y < h; ++y) {
      BYTE* line = FreeImage_GetScanLine(dib_, y);
      for (int x = 0; x < w; ++x) {
        std::swap(line[x * 3 + 0], line[x * 3 + 2]);  
      }
    }
  }
  FreeImage_SetDotsPerMeterX(dib_, static_cast<unsigned>(settings_.x_resolution * 39.37 + 0.5));
  FreeImage_SetDotsPerMeterY(dib_, static_cast<unsigned>(settings_.y_resolution * 39.37 + 0.5));
  if (settings_.pixel_type != TWPT_RGB) {
    FIBITMAP* converted = nullptr;
    switch (settings_.pixel_type) {
      case TWPT_BW:
        converted = FreeImage_Threshold(dib_, 128);
        break;
      case TWPT_GRAY:
        converted = FreeImage_ConvertTo8Bits(dib_);
        break;
    }
    if (converted != nullptr) {
      FreeImage_Unload(dib_);
      dib_ = converted;
    } else if (settings_.pixel_type != TWPT_RGB) {
      return false;
    }
  }
  int width = FreeImage_GetWidth(dib_);
  switch (settings_.pixel_type) {
    case TWPT_BW:
      dest_bytes_per_row_ = (((width * 1) + 31) / 32) * 4;
      row_offset_ = 0;
      break;
    case TWPT_GRAY:
      dest_bytes_per_row_ = (((width * 8) + 31) / 32) * 4;
      row_offset_ = 0;
      break;
    case TWPT_RGB:
    default:
      dest_bytes_per_row_ = (((width * 24) + 31) / 32) * 4;
      row_offset_ = 0;
      break;
  }
  scan_line_ = 0;
  return true;
}
// Outputs the next batch of scan lines in bottom-up DIB order.
// Each line is padded to a 4-byte boundary.  The function outputs as many
// complete lines as fit within bytes_to_read, advancing scan_line_ internally.
bool VirtualScanner::getScanStrip(BYTE* buffer, DWORD bytes_to_read,
                                   DWORD& bytes_received) {
  bytes_received = 0;
  if (buffer == nullptr || dib_ == nullptr) {
    return false;
  }
  if (bytes_to_read < static_cast<DWORD>(dest_bytes_per_row_)) {
    return false;
  }
  int source_height = FreeImage_GetHeight(dib_);
  WORD max_rows = static_cast<WORD>(bytes_to_read / dest_bytes_per_row_);
  for (WORD row = 0; row < max_rows; row++) {
    if (scan_line_ >= source_height) break;
    BYTE* bits = reinterpret_cast<BYTE*>(
        FreeImage_GetScanLine(dib_, source_height - scan_line_ - 1));
    int line_bytes = std::min(dest_bytes_per_row_,
        static_cast<int>(FreeImage_GetLine(dib_)));
    std::memcpy(buffer, bits + row_offset_, line_bytes);
    if (dest_bytes_per_row_ > line_bytes) {
      std::memset(buffer + line_bytes, 0, dest_bytes_per_row_ - line_bytes);
    }
    buffer += dest_bytes_per_row_;
    bytes_received += dest_bytes_per_row_;
    scan_line_++;
  }
  return bytes_received > 0;
}
ScannerSettings VirtualScanner::getSettings() const {
  return settings_;
}
void VirtualScanner::setSettings(const ScannerSettings& settings) {
  settings_ = settings;
}
bool VirtualScanner::isFeederLoaded() const {
  return false;
}
bool VirtualScanner::getDeviceOnline() const {
  return true;
}
void VirtualScanner::loadImageIndex() {
  if (image_dir_.empty()) return;
  std::string info_path = image_dir_ + "\\info.json";
  std::ifstream file(info_path);
  if (!file.is_open()) return;
  std::string line;
  while (std::getline(file, line)) {
    auto pos = line.find("\"next_index\"");
    if (pos != std::string::npos) {
      auto colon = line.find(':', pos);
      if (colon != std::string::npos) {
        size_t val = 0;
        size_t i = colon + 1;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
        while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
          val = val * 10 + (line[i] - '0');
          i++;
        }
        current_image_index_ = val;
        std::cerr << "ds: Loaded next_index=" << current_image_index_
                  << " from " << info_path << std::endl;
      }
      break;
    }
  }
}
void VirtualScanner::saveImageIndex() const {
  if (image_dir_.empty()) return;
  std::string info_path = image_dir_ + "\\info.json";
  std::ofstream file(info_path);
  if (!file.is_open()) {
    std::cerr << "ds: Failed to write " << info_path << std::endl;
    return;
  }
  file << "{\n  \"next_index\": " << current_image_index_ << "\n}\n";
  std::cerr << "ds: Saved next_index=" << current_image_index_
            << " to " << info_path << std::endl;
}
// Resets the image index to 0 when it has passed the end of the list.
// Re-scans the directory to pick up any newly added images before wrapping.
void VirtualScanner::wrapImageIndex() {
  scanImageDirectory();
  if (current_image_index_ >= image_list_.size()) {
    DS_LOG("ds: wrapImageIndex() - wrapping to 0\n");
    current_image_index_ = 0;
    saveImageIndex();
  }
}
void VirtualScanner::lock() {
  locked_ = true;
}
void VirtualScanner::unlock() {
  locked_ = false;
}
int VirtualScanner::getImageWidth() const {
  if (dib_ == nullptr) return 0;
  return FreeImage_GetWidth(dib_);
}
int VirtualScanner::getImageHeight() const {
  if (dib_ == nullptr) return 0;
  return FreeImage_GetHeight(dib_);
}
int VirtualScanner::getBitsPerPixel() const {
  if (dib_ == nullptr) return 0;
  return FreeImage_GetBPP(dib_);
}