// Virtual scanner implementation.
// Reads images from %APPDATA%\bntech\images, converts them to the
// requested TWAIN pixel type and resolution, and outputs DIB-compatible
// scan data line by line (bottom-up, 4-byte aligned).

#include "virtual_scanner.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <cmath>
#include <fstream>
#include <vector>
#pragma comment(lib, "shlwapi.lib")
#ifdef DS_DEBUG_LOG
  #define DS_LOG(msg) OutputDebugStringA(msg)
  #define DS_LOG_FMT(fmt, ...) do { char _buf[512]; \
      _snprintf_s(_buf, sizeof(_buf), fmt, __VA_ARGS__); \
      OutputDebugStringA(_buf); } while(0)
#else
  #define DS_LOG(msg) ((void)0)
  #define DS_LOG_FMT(fmt, ...) ((void)0)
#endif
HMODULE VirtualScanner::g_hinstance = nullptr;

namespace {

DWORD readBigEndian32(const std::vector<BYTE>& data, size_t offset) {
  return (static_cast<DWORD>(data[offset]) << 24) |
         (static_cast<DWORD>(data[offset + 1]) << 16) |
         (static_cast<DWORD>(data[offset + 2]) << 8) |
         static_cast<DWORD>(data[offset + 3]);
}

WORD readLittleEndian16(const std::vector<BYTE>& data, size_t offset) {
  return static_cast<WORD>(data[offset] |
      (static_cast<WORD>(data[offset + 1]) << 8));
}

DWORD readLittleEndian32(const std::vector<BYTE>& data, size_t offset) {
  return static_cast<DWORD>(data[offset]) |
         (static_cast<DWORD>(data[offset + 1]) << 8) |
         (static_cast<DWORD>(data[offset + 2]) << 16) |
         (static_cast<DWORD>(data[offset + 3]) << 24);
}

void writeLittleEndian16(std::vector<BYTE>& data, size_t offset, WORD value) {
  data[offset] = static_cast<BYTE>(value & 0xFF);
  data[offset + 1] = static_cast<BYTE>((value >> 8) & 0xFF);
}

void writeLittleEndian32(std::vector<BYTE>& data, size_t offset, DWORD value) {
  data[offset] = static_cast<BYTE>(value & 0xFF);
  data[offset + 1] = static_cast<BYTE>((value >> 8) & 0xFF);
  data[offset + 2] = static_cast<BYTE>((value >> 16) & 0xFF);
  data[offset + 3] = static_cast<BYTE>((value >> 24) & 0xFF);
}

void writeBigEndian16(std::vector<BYTE>& data, size_t offset, WORD value) {
  data[offset] = static_cast<BYTE>((value >> 8) & 0xFF);
  data[offset + 1] = static_cast<BYTE>(value & 0xFF);
}

void writeBigEndian32(std::vector<BYTE>& data, size_t offset, DWORD value) {
  data[offset] = static_cast<BYTE>((value >> 24) & 0xFF);
  data[offset + 1] = static_cast<BYTE>((value >> 16) & 0xFF);
  data[offset + 2] = static_cast<BYTE>((value >> 8) & 0xFF);
  data[offset + 3] = static_cast<BYTE>(value & 0xFF);
}

WORD dpiToJpegDensity(float dpi) {
  if (dpi <= 0.0f) return 300;
  if (dpi > 65535.0f) return 65535;
  return static_cast<WORD>(dpi + 0.5f);
}

DWORD dpiToPixelsPerMeter(float dpi) {
  if (dpi <= 0.0f) dpi = 300.0f;
  return static_cast<DWORD>(dpi * 39.3700787f + 0.5f);
}

DWORD crc32Png(const BYTE* data, size_t length) {
  DWORD crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

std::vector<BYTE> makePngPhysChunk(float x_dpi, float y_dpi) {
  std::vector<BYTE> chunk(21, 0);
  writeBigEndian32(chunk, 0, 9);
  chunk[4] = 'p';
  chunk[5] = 'H';
  chunk[6] = 'Y';
  chunk[7] = 's';
  // PNG pHYs stores pixels per meter.  The scanner UI and Windows Explorer
  // details show pixels per inch (DPI/PPI), so convert DPI to the PNG unit:
  //     pixels_per_meter = dpi / 0.0254 = dpi * 39.3700787
  // Windows converts this back and displays the expected DPI value.
  DWORD x_pixels_per_meter = dpiToPixelsPerMeter(x_dpi);
  DWORD y_pixels_per_meter = dpiToPixelsPerMeter(y_dpi);
  writeBigEndian32(chunk, 8, x_pixels_per_meter);
  writeBigEndian32(chunk, 12, y_pixels_per_meter);
  chunk[16] = 1;  // PNG pHYs unit specifier: meter.
  DWORD crc = crc32Png(&chunk[4], 13);
  writeBigEndian32(chunk, 17, crc);
  return chunk;
}

std::vector<BYTE> makeJpegJfifApp0Segment(float x_dpi, float y_dpi) {
  std::vector<BYTE> segment(18, 0);
  segment[0] = 0xFF;
  segment[1] = 0xE0;
  writeBigEndian16(segment, 2, 16);  // APP0 payload length, includes length bytes.
  segment[4] = 'J';
  segment[5] = 'F';
  segment[6] = 'I';
  segment[7] = 'F';
  segment[8] = 0;
  segment[9] = 1;   // Version 1.01.
  segment[10] = 1;
  segment[11] = 1;  // Density unit: dots per inch.
  writeBigEndian16(segment, 12, dpiToJpegDensity(x_dpi));
  writeBigEndian16(segment, 14, dpiToJpegDensity(y_dpi));
  segment[16] = 0;  // No thumbnail.
  segment[17] = 0;
  return segment;
}

bool readFileBytes(const std::string& path, std::vector<BYTE>& data) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return false;
  data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

bool writeFileBytes(const std::string& path, const std::vector<BYTE>& data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  out.write(reinterpret_cast<const char*>(data.data()), data.size());
  return out.good();
}

bool patchBmpDpiMetadata(const std::string& path, float x_dpi, float y_dpi) {
  std::vector<BYTE> data;
  if (!readFileBytes(path, data)) return false;
  if (data.size() < 46 || data[0] != 'B' || data[1] != 'M') return false;
  DWORD dib_header_size = readLittleEndian32(data, 14);
  if (dib_header_size < 40 || data.size() < 14 + dib_header_size) return false;

  writeLittleEndian32(data, 38, dpiToPixelsPerMeter(x_dpi));
  writeLittleEndian32(data, 42, dpiToPixelsPerMeter(y_dpi));
  return writeFileBytes(path, data);
}

WORD readTiff16(const std::vector<BYTE>& data, size_t offset, bool little_endian) {
  if (little_endian) return readLittleEndian16(data, offset);
  return static_cast<WORD>((data[offset] << 8) | data[offset + 1]);
}

DWORD readTiff32(const std::vector<BYTE>& data, size_t offset, bool little_endian) {
  if (little_endian) return readLittleEndian32(data, offset);
  return readBigEndian32(data, offset);
}

void writeTiff16(std::vector<BYTE>& data, size_t offset, WORD value,
                 bool little_endian) {
  if (little_endian) writeLittleEndian16(data, offset, value);
  else writeBigEndian16(data, offset, value);
}

void writeTiff32(std::vector<BYTE>& data, size_t offset, DWORD value,
                 bool little_endian) {
  if (little_endian) writeLittleEndian32(data, offset, value);
  else writeBigEndian32(data, offset, value);
}

bool patchTiffDpiMetadata(const std::string& path, float x_dpi, float y_dpi) {
  std::vector<BYTE> data;
  if (!readFileBytes(path, data)) return false;
  if (data.size() < 8) return false;

  bool little_endian = false;
  if (data[0] == 'I' && data[1] == 'I') little_endian = true;
  else if (data[0] == 'M' && data[1] == 'M') little_endian = false;
  else return false;
  if (readTiff16(data, 2, little_endian) != 42) return false;

  DWORD ifd_offset = readTiff32(data, 4, little_endian);
  if (ifd_offset + 2 > data.size()) return false;
  WORD entry_count = readTiff16(data, ifd_offset, little_endian);
  size_t entries = ifd_offset + 2;
  if (entries + static_cast<size_t>(entry_count) * 12 + 4 > data.size()) {
    return false;
  }

  DWORD x_num = static_cast<DWORD>(x_dpi * 100.0f + 0.5f);
  DWORD y_num = static_cast<DWORD>(y_dpi * 100.0f + 0.5f);
  const DWORD denominator = 100;
  bool patched_any = false;

  for (WORD i = 0; i < entry_count; ++i) {
    size_t entry = entries + static_cast<size_t>(i) * 12;
    WORD tag = readTiff16(data, entry, little_endian);
    WORD type = readTiff16(data, entry + 2, little_endian);
    DWORD count = readTiff32(data, entry + 4, little_endian);
    DWORD value_offset = readTiff32(data, entry + 8, little_endian);

    if ((tag == 282 || tag == 283) && type == 5 && count == 1 &&
        value_offset + 8 <= data.size()) {
      writeTiff32(data, value_offset, (tag == 282) ? x_num : y_num,
                  little_endian);
      writeTiff32(data, value_offset + 4, denominator, little_endian);
      patched_any = true;
    } else if (tag == 296 && type == 3 && count == 1) {
      // ResolutionUnit = 2 means inch, so X/YResolution are pixels per inch.
      writeTiff16(data, entry + 8, 2, little_endian);
      patched_any = true;
    }
  }

  return patched_any ? writeFileBytes(path, data) : false;
}

bool patchJpegDpiMetadata(const std::string& path, float x_dpi, float y_dpi) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return false;
  std::vector<BYTE> data((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
  in.close();
  if (data.size() < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;

  WORD x_density = dpiToJpegDensity(x_dpi);
  WORD y_density = dpiToJpegDensity(y_dpi);
  size_t pos = 2;
  while (pos + 4 <= data.size()) {
    if (data[pos] != 0xFF) break;
    BYTE marker = data[pos + 1];
    if (marker == 0xDA || marker == 0xD9) break;
    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
      pos += 2;
      continue;
    }
    WORD length = static_cast<WORD>((data[pos + 2] << 8) | data[pos + 3]);
    if (length < 2 || pos + 2 + length > data.size()) break;
    if (marker == 0xE0 && length >= 16 && pos + 18 <= data.size() &&
        data[pos + 4] == 'J' && data[pos + 5] == 'F' &&
        data[pos + 6] == 'I' && data[pos + 7] == 'F' && data[pos + 8] == 0) {
      data[pos + 11] = 1;  // Density unit: dots per inch.
      writeBigEndian16(data, pos + 12, x_density);
      writeBigEndian16(data, pos + 14, y_density);
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      if (!out.is_open()) return false;
      out.write(reinterpret_cast<const char*>(data.data()), data.size());
      return out.good();
    }
    pos += 2 + length;
  }

  std::vector<BYTE> app0 = makeJpegJfifApp0Segment(x_dpi, y_dpi);
  data.insert(data.begin() + 2, app0.begin(), app0.end());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  out.write(reinterpret_cast<const char*>(data.data()), data.size());
  return out.good();
}

bool patchPngDpiMetadata(const std::string& path, float x_dpi, float y_dpi) {
  static const BYTE kPngSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return false;
  std::vector<BYTE> data((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
  in.close();
  if (data.size() < 33 ||
      std::memcmp(data.data(), kPngSignature, sizeof(kPngSignature)) != 0) {
    return false;
  }

  std::vector<BYTE> phys = makePngPhysChunk(x_dpi, y_dpi);
  size_t pos = sizeof(kPngSignature);
  size_t insert_pos = 0;
  while (pos + 12 <= data.size()) {
    DWORD length = readBigEndian32(data, pos);
    if (pos + 12 + length > data.size()) return false;
    char type[5] = {
        static_cast<char>(data[pos + 4]), static_cast<char>(data[pos + 5]),
        static_cast<char>(data[pos + 6]), static_cast<char>(data[pos + 7]), 0};
    if (std::strcmp(type, "pHYs") == 0) {
      data.erase(data.begin() + pos, data.begin() + pos + 12 + length);
      data.insert(data.begin() + pos, phys.begin(), phys.end());
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      if (!out.is_open()) return false;
      out.write(reinterpret_cast<const char*>(data.data()), data.size());
      return out.good();
    }
    if (std::strcmp(type, "IHDR") == 0) {
      insert_pos = pos + 12 + length;
    }
    pos += 12 + length;
  }
  if (insert_pos == 0 || insert_pos > data.size()) return false;
  data.insert(data.begin() + insert_pos, phys.begin(), phys.end());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  out.write(reinterpret_cast<const char*>(data.data()), data.size());
  return out.good();
}

}  // namespace

VirtualScanner::VirtualScanner()
    : dib_(nullptr),
      scan_line_(0),
      locked_(false),
      dest_bytes_per_row_(0),
      row_offset_(0),
      current_image_index_(0),
      output_format_(0) {
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
    DS_LOG_FMT("ds: Scanned image dir: %s found %zu image(s)\n",
                image_dir_.c_str(), image_list_.size());
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
  settings_.page_size = 0;
  settings_.page_fill_mode = 0;
  settings_.rotation = 0;
  settings_.flip = 0;
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
    DS_LOG_FMT("ds: Image file not found: %s\n", image_path.c_str());
    return false;
  }
  FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(image_path.c_str());
  if (fif == FIF_UNKNOWN) {
    fif = FreeImage_GetFIFFromFilename(image_path.c_str());
  }
  if (fif == FIF_UNKNOWN) {
    DS_LOG_FMT("ds: Unknown image format: %s\n", image_path.c_str());
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
    DS_LOG_FMT("ds: Failed to load image: %s\n", image_path.c_str());
    return false;
  }
  if (!preScanPrep()) {
    return false;
  }
  return true;
}
// Converts the loaded image to the requested pixel type and calculates
// the DIB-compatible byte-per-row values for scan output.
bool VirtualScanner::preScanPrep() {
  if (dib_ == nullptr) return false;
  if (!ensure24BitDib()) return false;
  if (!applyPageSizeScaling()) return false;
  if (!applyRotation()) return false;
  if (!applyFlip()) return false;
  if (!applyPixelFormat()) return false;
  calculateRowParams();
  return true;
}

// Converts non-24-bit images to 24-bit RGB for uniform processing.
bool VirtualScanner::ensure24BitDib() {
  if (FreeImage_GetBPP(dib_) == 24) return true;
  FIBITMAP* converted = FreeImage_ConvertTo24Bits(dib_);
  FreeImage_Unload(dib_);
  dib_ = converted;
  return dib_ != nullptr;
}

bool VirtualScanner::applyPageSizeScaling() {
  if (dib_ == nullptr) return false;
  static const float kPageSizes[][2] = {
      {8.5f, 11.0f},       // US Letter.
      {8.5f, 14.0f},       // US Legal.
      {8.2677f, 11.6929f}, // A4.
      {5.8268f, 8.2677f}   // A5.
  };
  int page = settings_.page_size;
  if (page < 0 || page > 3) page = 0;
  float x_dpi = settings_.x_resolution;
  float y_dpi = settings_.y_resolution;
  if (x_dpi <= 0.0f) x_dpi = 300.0f;
  if (y_dpi <= 0.0f) y_dpi = x_dpi;
  int target_w = static_cast<int>(kPageSizes[page][0] * x_dpi + 0.5f);
  int target_h = static_cast<int>(kPageSizes[page][1] * y_dpi + 0.5f);
  if (target_w < 1) target_w = 1;
  if (target_h < 1) target_h = 1;

  int fill_mode = settings_.page_fill_mode;
  if (fill_mode < 0 || fill_mode > 2) fill_mode = 0;

  int src_w = FreeImage_GetWidth(dib_);
  int src_h = FreeImage_GetHeight(dib_);
  if (src_w <= 0 || src_h <= 0) return false;

  if (fill_mode == 0) {
    // Stretch: force the image to exactly match the page dimensions.
    if (src_w == target_w && src_h == target_h) return true;
    FIBITMAP* scaled = FreeImage_Rescale(dib_, target_w, target_h, FILTER_BILINEAR);
    if (scaled == nullptr) return false;
    FreeImage_Unload(dib_);
    dib_ = scaled;
    return true;
  }

  float scale_x = static_cast<float>(target_w) / static_cast<float>(src_w);
  float scale_y = static_cast<float>(target_h) / static_cast<float>(src_h);
  float scale = (fill_mode == 1) ? std::min(scale_x, scale_y)
                                 : std::max(scale_x, scale_y);
  int scaled_w = (fill_mode == 1)
      ? static_cast<int>(std::floor(src_w * scale + 0.5f))
      : static_cast<int>(std::ceil(src_w * scale));
  int scaled_h = (fill_mode == 1)
      ? static_cast<int>(std::floor(src_h * scale + 0.5f))
      : static_cast<int>(std::ceil(src_h * scale));
  if (scaled_w < 1) scaled_w = 1;
  if (scaled_h < 1) scaled_h = 1;
  if (fill_mode == 1) {
    if (scaled_w > target_w) scaled_w = target_w;
    if (scaled_h > target_h) scaled_h = target_h;
  } else {
    if (scaled_w < target_w) scaled_w = target_w;
    if (scaled_h < target_h) scaled_h = target_h;
  }

  FIBITMAP* scaled = FreeImage_Rescale(dib_, scaled_w, scaled_h, FILTER_BILINEAR);
  if (scaled == nullptr) return false;

  if (fill_mode == 1) {
    // Fit with padding: preserve aspect ratio, center on a white page canvas.
    FIBITMAP* canvas = FreeImage_Allocate(target_w, target_h, FreeImage_GetBPP(scaled));
    if (canvas == nullptr) {
      FreeImage_Unload(scaled);
      return false;
    }
    RGBQUAD white = {255, 255, 255, 0};
    FreeImage_FillBackground(canvas, &white);
    int left = (target_w - scaled_w) / 2;
    int top = (target_h - scaled_h) / 2;
    FreeImage_Paste(canvas, scaled, left, top, 256);
    FreeImage_Unload(scaled);
    FreeImage_Unload(dib_);
    dib_ = canvas;
    return true;
  }

  // Fill and crop: preserve aspect ratio, then crop the centered page area.
  int left = (scaled_w - target_w) / 2;
  int top = (scaled_h - target_h) / 2;
  FIBITMAP* cropped = FreeImage_Copy(scaled, left, top, left + target_w, top + target_h);
  FreeImage_Unload(scaled);
  if (cropped == nullptr) return false;
  FreeImage_Unload(dib_);
  dib_ = cropped;
  return true;
}

// Rotates the image clockwise by 0/90/180/270 degrees based on settings.
// Uses FreeImage_Rotate with exact 90-degree multiples for lossless rotation.
bool VirtualScanner::applyRotation() {
  if (dib_ == nullptr) return false;
  int rot = settings_.rotation;
  if (rot < 0 || rot > 3) rot = 0;
  if (rot == 0) return true;
  // FreeImage positive angle is counter-clockwise; negative is clockwise.
  double angle = -static_cast<double>(rot) * 90.0;
  FIBITMAP* rotated = FreeImage_Rotate(dib_, angle, nullptr);
  if (rotated == nullptr) return false;
  FreeImage_Unload(dib_);
  dib_ = rotated;
  return true;
}

// Flips the image horizontally or vertically based on settings.
bool VirtualScanner::applyFlip() {
  if (dib_ == nullptr) return false;
  int flip = settings_.flip;
  if (flip == 1) {
    return FreeImage_FlipHorizontal(dib_) != FALSE;
  } else if (flip == 2) {
    return FreeImage_FlipVertical(dib_) != FALSE;
  }
  return true;
}

// Applies the requested pixel type: BW threshold or grayscale conversion via
// FreeImage.  For RGB, no channel swap is needed: FreeImage stores 24-bit
// pixels in BGR order, which is exactly what Windows DIB / BMP format expects,
// so the raw scanline bytes can be copied through unchanged for both native
// transfer and file save.
// Also sets the DPI metadata based on current resolution settings.
bool VirtualScanner::applyPixelFormat() {
  // Non-RGB modes: convert to BW or grayscale.
  if (settings_.pixel_type != TWPT_RGB) {
    FIBITMAP* converted = nullptr;
    if (settings_.pixel_type == TWPT_BW) {
      converted = FreeImage_Threshold(dib_, 128);
    } else if (settings_.pixel_type == TWPT_GRAY) {
      converted = FreeImage_ConvertTo8Bits(dib_);
    }
    if (converted == nullptr) return false;
    FreeImage_Unload(dib_);
    dib_ = converted;
  }

  applyDpiMetadata();
  return true;
}

void VirtualScanner::applyDpiMetadata() {
  if (dib_ == nullptr) return;

  float x_dpi = settings_.x_resolution;
  float y_dpi = settings_.y_resolution;
  if (x_dpi <= 0.0f) x_dpi = 300.0f;
  if (y_dpi <= 0.0f) y_dpi = x_dpi;

  FreeImage_SetDotsPerMeterX(dib_,
      static_cast<unsigned>(x_dpi * 39.37f + 0.5f));
  FreeImage_SetDotsPerMeterY(dib_,
      static_cast<unsigned>(y_dpi * 39.37f + 0.5f));

  // Some applications inspect EXIF/TIFF tags rather than the bitmap header's
  // pixels-per-meter fields.  Write both so PNG/JPEG/TIFF/BMP outputs carry
  // discoverable DPI metadata after FreeImage_Save().
  struct Rational {
    DWORD numerator;
    DWORD denominator;
  };
  const WORD kXResolution = 0x011A;
  const WORD kYResolution = 0x011B;
  const WORD kResolutionUnit = 0x0128;
  Rational x_res = { static_cast<DWORD>(x_dpi * 100.0f + 0.5f), 100 };
  Rational y_res = { static_cast<DWORD>(y_dpi * 100.0f + 0.5f), 100 };
  WORD inch_unit = 2;

  auto set_tag = [&](const char* key, WORD id, FREE_IMAGE_MDTYPE type,
                     DWORD count, DWORD length, const void* value) {
    FITAG* tag = FreeImage_CreateTag();
    if (tag == nullptr) return;
    FreeImage_SetTagKey(tag, key);
    FreeImage_SetTagID(tag, id);
    FreeImage_SetTagType(tag, type);
    FreeImage_SetTagCount(tag, count);
    FreeImage_SetTagLength(tag, length);
    FreeImage_SetTagValue(tag, value);
    FreeImage_SetMetadata(FIMD_EXIF_MAIN, dib_, key, tag);
    FreeImage_DeleteTag(tag);
  };

  set_tag("XResolution", kXResolution, FIDT_RATIONAL, 1,
          sizeof(x_res), &x_res);
  set_tag("YResolution", kYResolution, FIDT_RATIONAL, 1,
          sizeof(y_res), &y_res);
  set_tag("ResolutionUnit", kResolutionUnit, FIDT_SHORT, 1,
          sizeof(inch_unit), &inch_unit);
}

void VirtualScanner::patchSavedDpiMetadata(FREE_IMAGE_FORMAT fif,
                                           const std::string& path) {
  float x_dpi = settings_.x_resolution;
  float y_dpi = settings_.y_resolution;
  if (x_dpi <= 0.0f) x_dpi = 300.0f;
  if (y_dpi <= 0.0f) y_dpi = x_dpi;

  if (fif == FIF_PNG) {
    patchPngDpiMetadata(path, x_dpi, y_dpi);
  } else if (fif == FIF_JPEG) {
    patchJpegDpiMetadata(path, x_dpi, y_dpi);
  } else if (fif == FIF_BMP) {
    patchBmpDpiMetadata(path, x_dpi, y_dpi);
  } else if (fif == FIF_TIFF) {
    patchTiffDpiMetadata(path, x_dpi, y_dpi);
  }
}

// Calculates DIB-compatible bytes-per-row (DWORD-aligned) and
// resets the scan line counter for a new scan session.
void VirtualScanner::calculateRowParams() {
  int width = FreeImage_GetWidth(dib_);
  row_offset_ = 0;
  int bpp = FreeImage_GetBPP(dib_);
  dest_bytes_per_row_ = (((width * bpp) + 31) / 32) * 4;
  scan_line_ = 0;
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
        DS_LOG_FMT("ds: Loaded next_index=%zu from %s\n",
                  current_image_index_, info_path.c_str());
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
    DS_LOG_FMT("ds: Failed to write %s\n", info_path.c_str());
    return;
  }
  file << "{\n  \"next_index\": " << current_image_index_ << "\n}\n";
  DS_LOG_FMT("ds: Saved next_index=%zu to %s\n",
            current_image_index_, info_path.c_str());
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
bool VirtualScanner::saveImageToFile() {
  if (dib_ == nullptr || output_dir_.empty()) return false;
  static const FREE_IMAGE_FORMAT kFiFmts[] = {FIF_PNG, FIF_JPEG, FIF_BMP, FIF_TIFF};
  static const char* kExts[] = {".png", ".jpg", ".bmp", ".tif"};
  FREE_IMAGE_FORMAT fif = kFiFmts[output_format_];
  // Ensure directory exists
  SHCreateDirectoryExA(nullptr, output_dir_.c_str(), nullptr);
  // Use the user-supplied filename if any; otherwise generate a timestamped one.
  char fname[MAX_PATH];
  if (!output_filename_.empty()) {
    _snprintf_s(fname, sizeof(fname), "%s%s",
                output_filename_.c_str(), kExts[output_format_]);
  } else {
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf_s(fname, sizeof(fname), "scan_%04d%02d%02d_%02d%02d%02d%s",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                kExts[output_format_]);
  }
  last_saved_file_ = output_dir_ + "\\" + fname;
  applyDpiMetadata();
  int flags = (fif == FIF_JPEG) ? 85 : 0;
  bool saved = FreeImage_Save(fif, dib_, last_saved_file_.c_str(), flags) != FALSE;
  if (saved) patchSavedDpiMetadata(fif, last_saved_file_);
  return saved;
}

bool VirtualScanner::saveImageToPath(const std::string& path) {
  if (dib_ == nullptr || path.empty()) return false;
  // Resolve to an absolute path if relative.
  char abs_path[MAX_PATH] = {};
  if (PathIsRelativeA(path.c_str())) {
    char cwd[MAX_PATH] = {};
    GetCurrentDirectoryA(MAX_PATH, cwd);
    _snprintf_s(abs_path, sizeof(abs_path), "%s\\%s", cwd, path.c_str());
  } else {
    _snprintf_s(abs_path, sizeof(abs_path), "%s", path.c_str());
  }
  // Derive format from the extension; default to PNG.
  FREE_IMAGE_FORMAT fif = FIF_PNG;
  auto dot = std::string(abs_path).find_last_of('.');
  if (dot != std::string::npos) {
    std::string ext = std::string(abs_path).substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == "png") fif = FIF_PNG;
    else if (ext == "jpg" || ext == "jpeg") fif = FIF_JPEG;
    else if (ext == "bmp") fif = FIF_BMP;
    else if (ext == "tif" || ext == "tiff") fif = FIF_TIFF;
  }
  // Ensure the output directory exists.
  std::string dir(abs_path);
  auto slash = dir.find_last_of("\\/");
  if (slash != std::string::npos) {
    dir = dir.substr(0, slash);
    SHCreateDirectoryExA(nullptr, dir.c_str(), nullptr);
  }
  last_saved_file_ = abs_path;
  applyDpiMetadata();
  int flags = (fif == FIF_JPEG) ? 85 : 0;
  bool saved = FreeImage_Save(fif, dib_, abs_path, flags) != FALSE;
  if (saved) patchSavedDpiMetadata(fif, abs_path);
  return saved;
}