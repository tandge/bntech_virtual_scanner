#include "settings_server.h"
#include "localization.h"
#include "virtual_scanner.h"
#include "FreeImage.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <map>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>
#pragma comment(lib, "ws2_32.lib")

namespace {

std::map<std::string, std::string> parseUrlEncodedParams(
    const std::string& form_data) {
  std::istringstream stream(form_data);
  std::string pair;
  std::map<std::string, std::string> params;
  while (std::getline(stream, pair, '&')) {
    auto eq = pair.find('=');
    if (eq == std::string::npos) continue;
    std::string key = pair.substr(0, eq);
    std::string val = pair.substr(eq + 1);
    std::string decoded;
    for (size_t i = 0; i < val.size(); ++i) {
      if (val[i] == '%' && i + 2 < val.size()) {
        char hex[3] = {val[i + 1], val[i + 2], 0};
        decoded += static_cast<char>(std::strtol(hex, nullptr, 16));
        i += 2;
      } else if (val[i] == '+') {
        decoded += ' ';
      } else {
        decoded += val[i];
      }
    }
    params[key] = decoded;
  }
  return params;
}

int intParam(const std::map<std::string, std::string>& params,
             const char* key, int fallback) {
  auto it = params.find(key);
  if (it == params.end() || it->second.empty()) return fallback;
  return std::atoi(it->second.c_str());
}

std::string defaultPreviewImagePath() {
  char module_path[MAX_PATH] = {};
  GetModuleFileNameA(VirtualScanner::g_hinstance, module_path, MAX_PATH);
  PathRemoveFileSpecA(module_path);
  return std::string(module_path) + "\\TWAIN_logo.png";
}

size_t readPreviewImageIndex(const std::string& image_dir) {
  std::string info_path = image_dir + "\\info.json";
  std::ifstream file(info_path);
  if (!file.is_open()) return 0;
  std::string line;
  while (std::getline(file, line)) {
    auto pos = line.find("\"next_index\"");
    if (pos == std::string::npos) continue;
    auto colon = line.find(':', pos);
    if (colon == std::string::npos) break;
    size_t val = 0;
    size_t i = colon + 1;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
      val = val * 10 + static_cast<size_t>(line[i] - '0');
      ++i;
    }
    return val;
  }
  return 0;
}

std::string selectPreviewImagePath() {
  std::vector<std::string> images;
  std::string image_dir;
  char appdata_path[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0,
                                 appdata_path))) {
    image_dir = std::string(appdata_path) + "\\bntech\\images";
    std::string search_pattern = image_dir + "\\*.*";
    WIN32_FIND_DATAA find_data = {};
    HANDLE find_handle = FindFirstFileA(search_pattern.c_str(), &find_data);
    if (find_handle != INVALID_HANDLE_VALUE) {
      do {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string filename = find_data.cFileName;
        auto dot_pos = filename.find_last_of('.');
        if (dot_pos == std::string::npos) continue;
        std::string ext = filename.substr(dot_pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) {
                         return static_cast<char>(std::tolower(c));
                       });
        if (ext == "png" || ext == "jpg" || ext == "jpeg" ||
            ext == "bmp" || ext == "tif" || ext == "tiff" ||
            ext == "webp" || ext == "gif") {
          images.push_back(image_dir + "\\" + filename);
        }
      } while (FindNextFileA(find_handle, &find_data));
      FindClose(find_handle);
    }
  }
  if (!images.empty()) {
    std::sort(images.begin(), images.end());
    size_t index = readPreviewImageIndex(image_dir);
    if (index >= images.size()) index = 0;
    return images[index];
  }
  return defaultPreviewImagePath();
}

bool ensure24Bit(FIBITMAP*& dib) {
  if (dib == nullptr) return false;
  if (FreeImage_GetBPP(dib) == 24) return true;
  FIBITMAP* converted = FreeImage_ConvertTo24Bits(dib);
  FreeImage_Unload(dib);
  dib = converted;
  return dib != nullptr;
}

bool applyPreviewPageSize(FIBITMAP*& dib, const ScannerSettings& settings) {
  if (dib == nullptr) return false;
  static const float kPageSizes[][2] = {
      {8.5f, 11.0f}, {8.5f, 14.0f}, {8.2677f, 11.6929f}, {5.8268f, 8.2677f}};
  int page = settings.page_size;
  if (page < 0 || page > 3) page = 0;
  float x_dpi = settings.x_resolution > 0.0f ? settings.x_resolution : 300.0f;
  float y_dpi = settings.y_resolution > 0.0f ? settings.y_resolution : x_dpi;
  int target_w = static_cast<int>(kPageSizes[page][0] * x_dpi + 0.5f);
  int target_h = static_cast<int>(kPageSizes[page][1] * y_dpi + 0.5f);
  if (target_w < 1) target_w = 1;
  if (target_h < 1) target_h = 1;

  int fill_mode = settings.page_fill_mode;
  if (fill_mode < 0 || fill_mode > 2) fill_mode = 0;
  int src_w = FreeImage_GetWidth(dib);
  int src_h = FreeImage_GetHeight(dib);
  if (src_w <= 0 || src_h <= 0) return false;

  if (fill_mode == 0) {
    FIBITMAP* scaled = FreeImage_Rescale(dib, target_w, target_h,
                                         FILTER_BILINEAR);
    if (scaled == nullptr) return false;
    FreeImage_Unload(dib);
    dib = scaled;
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

  FIBITMAP* scaled = FreeImage_Rescale(dib, scaled_w, scaled_h,
                                       FILTER_BILINEAR);
  if (scaled == nullptr) return false;
  if (fill_mode == 1) {
    FIBITMAP* canvas = FreeImage_Allocate(target_w, target_h,
                                          FreeImage_GetBPP(scaled));
    if (canvas == nullptr) {
      FreeImage_Unload(scaled);
      return false;
    }
    RGBQUAD white = {255, 255, 255, 0};
    FreeImage_FillBackground(canvas, &white);
    FreeImage_Paste(canvas, scaled, (target_w - scaled_w) / 2,
                    (target_h - scaled_h) / 2, 256);
    FreeImage_Unload(scaled);
    FreeImage_Unload(dib);
    dib = canvas;
    return true;
  }

  FIBITMAP* cropped = FreeImage_Copy(scaled, (scaled_w - target_w) / 2,
                                     (scaled_h - target_h) / 2,
                                     (scaled_w - target_w) / 2 + target_w,
                                     (scaled_h - target_h) / 2 + target_h);
  FreeImage_Unload(scaled);
  if (cropped == nullptr) return false;
  FreeImage_Unload(dib);
  dib = cropped;
  return true;
}

bool applyPreviewRotation(FIBITMAP*& dib, int rotation) {
  if (dib == nullptr) return false;
  int rot = rotation;
  if (rot < 0 || rot > 3) rot = 0;
  if (rot == 0) return true;
  FIBITMAP* rotated = FreeImage_Rotate(dib, -static_cast<double>(rot) * 90.0,
                                       nullptr);
  if (rotated == nullptr) return false;
  FreeImage_Unload(dib);
  dib = rotated;
  return true;
}

bool applyPreviewFlip(FIBITMAP*& dib, int flip) {
  if (dib == nullptr) return false;
  if (flip == 1) return FreeImage_FlipHorizontal(dib) != FALSE;
  if (flip == 2) return FreeImage_FlipVertical(dib) != FALSE;
  return true;
}

bool applyPreviewPixelType(FIBITMAP*& dib, int pixel_type) {
  if (dib == nullptr) return false;
  if (pixel_type == TWPT_RGB) return true;
  FIBITMAP* converted = nullptr;
  if (pixel_type == TWPT_BW) {
    converted = FreeImage_Threshold(dib, 128);
  } else if (pixel_type == TWPT_GRAY) {
    converted = FreeImage_ConvertTo8Bits(dib);
  } else {
    return true;
  }
  if (converted == nullptr) return false;
  FreeImage_Unload(dib);
  dib = converted;
  return true;
}

bool resizePreviewToFit(FIBITMAP*& dib, int max_w, int max_h) {
  if (dib == nullptr) return false;
  int w = FreeImage_GetWidth(dib);
  int h = FreeImage_GetHeight(dib);
  if (w <= 0 || h <= 0) return false;
  float scale = std::min(static_cast<float>(max_w) / static_cast<float>(w),
                         static_cast<float>(max_h) / static_cast<float>(h));
  if (scale <= 0.0f) return false;
  if (scale > 1.0f) scale = 1.0f;
  int new_w = static_cast<int>(w * scale + 0.5f);
  int new_h = static_cast<int>(h * scale + 0.5f);
  if (new_w < 1) new_w = 1;
  if (new_h < 1) new_h = 1;
  FIBITMAP* scaled = FreeImage_Rescale(dib, new_w, new_h, FILTER_BILINEAR);
  if (scaled == nullptr) return false;
  FreeImage_Unload(dib);
  dib = scaled;
  return true;
}

std::vector<BYTE> buildPreviewPng(const std::string& query) {
  auto params = parseUrlEncodedParams(query);
  ScannerSettings settings = {};
  int pixel = intParam(params, "pixeltype", 2);
  settings.pixel_type = (pixel == 0) ? TWPT_BW : (pixel == 1) ? TWPT_GRAY : TWPT_RGB;
  settings.x_resolution = static_cast<float>(intParam(params, "resolution", 300));
  settings.y_resolution = settings.x_resolution;
  settings.page_size = intParam(params, "pagesize", 0);
  settings.page_fill_mode = intParam(params, "pagefillmode", 0);
  settings.rotation = intParam(params, "rotation", 0);
  settings.flip = intParam(params, "flip", 0);

  std::string image_path = selectPreviewImagePath();
  FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(image_path.c_str());
  if (fif == FIF_UNKNOWN) fif = FreeImage_GetFIFFromFilename(image_path.c_str());
  if (fif == FIF_UNKNOWN) return {};
  int load_flags = 0;
  if (fif == FIF_PNG) load_flags = PNG_IGNOREGAMMA;
  else if (fif == FIF_JPEG) load_flags = JPEG_ACCURATE;
  FIBITMAP* dib = FreeImage_Load(fif, image_path.c_str(), load_flags);
  if (dib == nullptr) return {};

  bool ok = ensure24Bit(dib) && applyPreviewPageSize(dib, settings) &&
            applyPreviewRotation(dib, settings.rotation) &&
            applyPreviewFlip(dib, settings.flip) &&
            applyPreviewPixelType(dib, settings.pixel_type) &&
            resizePreviewToFit(dib, 300, 260);
  std::vector<BYTE> bytes;
  if (ok) {
    FIMEMORY* mem = FreeImage_OpenMemory();
    if (mem != nullptr) {
      if (FreeImage_SaveToMemory(FIF_PNG, dib, mem, PNG_DEFAULT)) {
        BYTE* data = nullptr;
        DWORD size = 0;
        if (FreeImage_AcquireMemory(mem, &data, &size) && data != nullptr && size > 0) {
          bytes.assign(data, data + size);
        }
      }
      FreeImage_CloseMemory(mem);
    }
  }
  FreeImage_Unload(dib);
  return bytes;
}

}  // namespace

SettingsServer::SettingsServer()
    : server_thread_(nullptr),
      running_(false),
      server_port_(0),
      listen_socket_(INVALID_SOCKET),
      browser_window_(nullptr) {
  std::memset(&result_, 0, sizeof(result_));
}
SettingsServer::~SettingsServer() {
  if (listen_socket_ != INVALID_SOCKET) {
    closesocket(listen_socket_);
  }
}
int SettingsServer::findFreePort() const {
  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET) return -1;
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;  
  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closesocket(sock);
    return -1;
  }
  int addr_len = sizeof(addr);
  if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
    closesocket(sock);
    return -1;
  }
  int port = ntohs(addr.sin_port);
  closesocket(sock);
  return port;
}
std::string SettingsServer::buildHtmlPage(int port) const {
  const auto& text = localization::strings();
  auto sel = [](int v, int cur) { return v == cur ? " selected" : ""; };
  std::string out_dir = result_.output_dir[0]
      ? std::string(result_.output_dir) : default_output_dir_;
  std::ostringstream html;
  html << "<!DOCTYPE html>\n<html><head><meta charset='utf-8'>\n";
  html << "<title>" << text.app_title << "</title>\n";
  html << "<style>\n";
  html << "html,body{overflow-x:hidden;overflow-y:auto;}\n";
  html << "body{font-family:Segoe UI,Arial,sans-serif;margin:12px;"
       << "background:#f5f5f5;width:440px;}\n";
  html << "h1{color:#333;font-size:16px;margin:0 0 10px 0;}\n";
  html << ".group{background:#fff;border:1px solid #ddd;border-radius:4px;"
       << "padding:10px 12px;margin-bottom:10px;}\n";
  html << ".group h2{margin:0 0 6px 0;color:#555;font-size:13px;}\n";
  html << "label{display:inline-block;width:130px;margin:2px 0;font-size:13px;}\n";
  html << "select,input{margin:2px 0;padding:3px;font-size:13px;}\n";
  html << ".buttons{text-align:right;margin-top:10px;padding-right:20px;}\n";
  html << "button{padding:6px 20px;margin-left:6px;font-size:13px;"
       << "border:none;border-radius:4px;cursor:pointer;}\n";
  html << ".scan{background:#0078d7;color:#fff;}\n";
  html << ".preview{background:#666;color:#fff;}\n";
  html << ".cancel{background:#ccc;color:#333;}\n";
  html << ".previewBox{text-align:center;}\n";
  html << "#previewImg{display:none;max-width:300px;max-height:260px;"
       << "border:1px solid #ccc;background:#fff;}\n";
  html << "#previewHint{color:#777;font-size:12px;margin:8px 0;}\n";
  html << "</style></head><body>\n";
  html << "<h1>" << text.app_title << " <span style='font-size:11px;color:#999;'>[" << __DATE__ << " " << __TIME__ << "]</span></h1>\n";
  html << "<div class='group'><h2>" << text.scan_settings << "</h2>\n";
  html << "<label>" << text.color_mode << "</label>\n";
  html << "<select id='pixeltype' name='pixeltype'>\n";
  html << "<option value='0'" << sel(0, result_.pixel_type) << ">" << text.black_and_white << "</option>\n";
  html << "<option value='1'" << sel(1, result_.pixel_type) << ">" << text.grayscale << "</option>\n";
  html << "<option value='2'" << sel(2, result_.pixel_type) << ">" << text.color << "</option>\n";
  html << "</select><br>\n";
  html << "<label>" << text.resolution_dpi << "</label>\n";
  html << "<select id='resolution' name='resolution'>\n";
  html << "<option value='50'" << sel(50, result_.resolution) << ">50</option>\n";
  html << "<option value='100'" << sel(100, result_.resolution) << ">100</option>\n";
  html << "<option value='150'" << sel(150, result_.resolution) << ">150</option>\n";
  html << "<option value='200'" << sel(200, result_.resolution) << ">200</option>\n";
  html << "<option value='300'" << sel(300, result_.resolution) << ">300</option>\n";
  html << "<option value='600'" << sel(600, result_.resolution) << ">600</option>\n";
  html << "<option value='1200'" << sel(1200, result_.resolution) << ">1200</option>\n";
  html << "</select><br>\n";
  html << "<label>" << text.page_size << "</label>\n";
  html << "<select id='pagesize' name='pagesize'>\n";
  html << "<option value='0'" << sel(0, result_.page_size) << ">US Letter (8.5 x 11 in)</option>\n";
  html << "<option value='1'" << sel(1, result_.page_size) << ">US Legal (8.5 x 14 in)</option>\n";
  html << "<option value='2'" << sel(2, result_.page_size) << ">A4 (210 x 297 mm)</option>\n";
  html << "<option value='3'" << sel(3, result_.page_size) << ">A5 (148 x 210 mm)</option>\n";
  html << "</select><br>\n";
  html << "<label>" << text.rotation << "</label>\n";
  html << "<select id='rotation' name='rotation'>\n";
  html << "<option value='0'" << sel(0, result_.rotation) << ">" << text.rotation_0deg << "</option>\n";
  html << "<option value='1'" << sel(1, result_.rotation) << ">" << text.rotation_90deg << "</option>\n";
  html << "<option value='2'" << sel(2, result_.rotation) << ">" << text.rotation_180deg << "</option>\n";
  html << "<option value='3'" << sel(3, result_.rotation) << ">" << text.rotation_270deg << "</option>\n";
  html << "</select><br>\n";
  html << "<label>" << text.flip << "</label>\n";
  html << "<select id='flip' name='flip'>\n";
  html << "<option value='0'" << sel(0, result_.flip) << ">" << text.flip_none << "</option>\n";
  html << "<option value='1'" << sel(1, result_.flip) << ">" << text.flip_horizontal << "</option>\n";
  html << "<option value='2'" << sel(2, result_.flip) << ">" << text.flip_vertical << "</option>\n";
  html << "</select><br>\n";
  html << "<label>" << text.page_fill << "</label>\n";
  html << "<select id='pagefillmode' name='pagefillmode'>\n";
  html << "<option value='0'" << sel(0, result_.page_fill_mode) << ">" << text.stretch << "</option>\n";
  html << "<option value='1'" << sel(1, result_.page_fill_mode) << ">" << text.fit_with_padding << "</option>\n";
  html << "<option value='2'" << sel(2, result_.page_fill_mode) << ">" << text.fill_and_crop << "</option>\n";
  html << "</select><br>\n";
  html << "</div>\n";
  if (result_.app_managed_file_output) {
    // Application has already chosen File-transfer mode and will supply the
    // destination path via DAT_SETUPFILEXFER.  Hide all file-output controls
    // so the user only edits scan settings (color, resolution).
    html << "<div class='group'><h2>" << text.output_settings << "</h2>\n";
    html << "<p style='color:#666;margin:4px 0;'>" << text.app_managed_file_output << "</p>\n";
    html << "</div>\n";
  } else {
    html << "<div class='group'><h2>" << text.output_settings << "</h2>\n";
    html << "<label>" << text.transfer_mode << "</label>\n";
    html << "<select id='transfermode' name='transfermode' onchange='updateMode()'>\n";
    html << "<option value='0'" << sel(0, result_.transfer_mode) << ">" << text.native_memory << "</option>\n";
    html << "<option value='1'" << sel(1, result_.transfer_mode) << ">" << text.file << "</option>\n";
    html << "</select><br>\n";
    html << "<div id='row_format'>\n";
    html << "<label>" << text.file_format << "</label>\n";
    html << "<select id='fileformat' name='fileformat'>\n";
    html << "<option value='0'" << sel(0, result_.file_format) << ">PNG</option>\n";
    html << "<option value='1'" << sel(1, result_.file_format) << ">JPG</option>\n";
    html << "<option value='2'" << sel(2, result_.file_format) << ">BMP</option>\n";
    html << "<option value='3'" << sel(3, result_.file_format) << ">TIFF</option>\n";
    html << "<option value='4'" << sel(4, result_.file_format) << ">WEBP</option>\n";
    html << "<option value='5'" << sel(5, result_.file_format) << ">GIF</option>\n";
    html << "</select><br>\n";
    html << "</div>\n";
    html << "<div id='row_output'>\n";
    html << "<label>" << text.output_directory << "</label>\n";
    html << "<input type='text' id='outputdir' name='outputdir' style='width:240px;' value='" << out_dir << "'>\n";
    html << "<button onclick='browseDir()' style='padding:4px 10px;margin-left:4px;'>" << text.browse << "</button><br>\n";
    html << "<label>" << text.output_filename << "</label>\n";
    html << "<input type='text' id='outputfilename' name='outputfilename' style='width:240px;' value='" << result_.output_filename << "'>\n";
    html << "<span id='outputext' style='color:#666;margin-left:4px;'></span><br>\n";
    html << "</div>\n";
    html << "</div>\n";
  }
  html << "<div class='group previewBox'><h2>" << text.preview_image << "</h2>\n";
  html << "<div id='previewHint'>" << text.preview_hint << "</div>\n";
  html << "<img id='previewImg' alt='Preview'>\n";
  html << "</div>\n";
  html << "<div class='buttons'>\n";
  html << "<button class='cancel' onclick='doCancel()'>" << text.cancel << "</button>\n";
  html << "<button class='preview' onclick='doPreview()'>" << text.preview << "</button>\n";
  html << "<button class='scan' onclick='doScan()'>" << text.scan << "</button>\n";
  html << "</div>\n";
  html << "<script>\n";
  html << "var EXTS=['.png','.jpg','.bmp','.tif','.webp','.gif'];\n";
  // Compact layout: resize the browser window to the content width and
  // centre it on screen.  resizeTo/moveTo are best-effort and may be
  // ignored by modern browsers; in that case the fixed 460px body still
  // prevents content from stretching.
  const int winW = 540;
  const int winH = result_.app_managed_file_output ? 650 : 750;
  html << "window.resizeTo(" << winW << "," << winH << ");\n";
  html << "window.moveTo((screen.width-" << winW << ")/2,(screen.height-" << winH << ")/2);\n";
  html << "function val(id,d){var e=document.getElementById(id);return e?e.value:d;}\n";
  html << "function updateMode(){\n";
  html << "  var tm=document.getElementById('transfermode');\n";
  html << "  if(!tm)return;\n";
  html << "  var f=tm.value=='1';\n";
  html << "  var rf=document.getElementById('row_format');\n";
  html << "  var ro=document.getElementById('row_output');\n";
  html << "  if(rf)rf.style.display=f?'':'none';\n";
  html << "  if(ro)ro.style.display=f?'':'none';\n";
  html << "  updateExt();\n";
  html << "}\n";
  html << "function updateExt(){\n";
  html << "  var ff=document.getElementById('fileformat');\n";
  html << "  var span=document.getElementById('outputext');\n";
  html << "  if(ff&&span)span.textContent=EXTS[ff.value]||'';\n";
  html << "}\n";
  html << "function submitAndClose(url){\n";
  html << "  var x=new XMLHttpRequest();\n";
  html << "  x.open('GET',url,true);\n";
  html << "  x.send();\n";
  html << "  setTimeout(function(){window.open('','_self');window.close();},100);\n";
  html << "}\n";
  html << "function collectParams(){\n";
  html << "  var p={};\n";
  html << "  p.pixeltype=val('pixeltype','');\n";
  html << "  p.resolution=val('resolution','');\n";
  html << "  p.pagesize=val('pagesize','');\n";
  html << "  p.pagefillmode=val('pagefillmode','');\n";
  html << "  p.rotation=val('rotation','');\n";
  html << "  p.flip=val('flip','');\n";
  html << "  p.fileformat=val('fileformat','');\n";
  html << "  p.transfermode=val('transfermode','');\n";
  html << "  p.outputdir=val('outputdir','');\n";
  html << "  p.outputfilename=val('outputfilename','');\n";
  html << "  return p;\n";
  html << "}\n";
  html << "function toQuery(p){return Object.keys(p).map(function(k){"
       << "return encodeURIComponent(k)+'='+encodeURIComponent(p[k])}).join('&');}\n";
  html << "function doPreview(){\n";
  html << "  var img=document.getElementById('previewImg');\n";
  html << "  var hint=document.getElementById('previewHint');\n";
  html << "  if(hint){hint.style.display='';hint.textContent='Loading preview...';}\n";
  html << "  if(img){img.style.display='none';img.onload=function(){if(hint)hint.style.display='none';img.style.display='inline-block';};img.onerror=function(){if(hint){hint.style.display='';hint.textContent='Preview failed.';}};img.src='/preview?'+toQuery(collectParams())+'&t='+(new Date().getTime());}\n";
  html << "}\n";
  html << "function doScan(){\n";
  html << "  var p=collectParams();\n";
  html << "  p.action='scan';\n";
  html << "  submitAndClose('/submit?'+toQuery(p));\n";
  html << "}\n";
  html << "function doCancel(){\n";
  html << "  submitAndClose('/submit?action=cancel');\n";
  html << "}\n";
  html << "function browseDir(){\n";
  html << "  var x=new XMLHttpRequest();\n";
  html << "  x.open('GET','/browse',true);\n";
  html << "  x.onload=function(){if(x.responseText){var od=document.getElementById('outputdir');if(od)od.value=x.responseText;}};\n";
  html << "  x.send();\n";
  html << "}\n";
  html << "var ff=document.getElementById('fileformat');if(ff)ff.addEventListener('change',updateExt);\n";
  html << "updateMode();\n";
  html << "</script>\n";
  html << "</body></html>";
  return html.str();
}
void SettingsServer::parseFormData(const std::string& form_data) {
  std::map<std::string, std::string> params = parseUrlEncodedParams(form_data);
  result_.scan_clicked = (params["action"] == "scan");
  result_.pixel_type = std::atoi(params["pixeltype"].c_str());
  result_.resolution = std::atoi(params["resolution"].c_str());
  result_.page_size = std::atoi(params["pagesize"].c_str());
  result_.page_fill_mode = std::atoi(params["pagefillmode"].c_str());
  result_.rotation = std::atoi(params["rotation"].c_str());
  result_.flip = std::atoi(params["flip"].c_str());
  result_.file_format = std::atoi(params["fileformat"].c_str());
  result_.transfer_mode = std::atoi(params["transfermode"].c_str());
  strncpy_s(result_.output_dir, MAX_PATH, params["outputdir"].c_str(), _TRUNCATE);
  strncpy_s(result_.output_filename, MAX_PATH, params["outputfilename"].c_str(), _TRUNCATE);
}
DWORD WINAPI SettingsServer::serverThreadProc(LPVOID param) {
  // SHBrowseForFolderW requires COM initialized on the calling thread.
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  auto* self = static_cast<SettingsServer*>(param);
  if (self == nullptr) { CoUninitialize(); return 1; }
  while (self->running_) {
    sockaddr_in client_addr = {};
    int client_len = sizeof(client_addr);
    SOCKET client = accept(self->listen_socket_,
                           reinterpret_cast<sockaddr*>(&client_addr),
                           &client_len);
    if (client == INVALID_SOCKET) {
      break;
    }
    char buf[8192] = {};
    int received = recv(client, buf, sizeof(buf) - 1, 0);
    if (received <= 0) {
      closesocket(client);
      continue;
    }
    std::string request(buf, received);
    if (request.find("GET / ") != std::string::npos ||
        request.find("GET /index") != std::string::npos) {
      std::string html = self->buildHtmlPage(self->server_port_);
      std::string response = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/html; charset=utf-8\r\n"
                             "Cache-Control: no-store, no-cache, must-revalidate\r\n"
                             "Pragma: no-cache\r\n"
                             "Connection: close\r\n\r\n" + html;
      send(client, response.c_str(), static_cast<int>(response.size()), 0);
      closesocket(client);
    } else if (request.find("GET /preview?") != std::string::npos) {
      auto qpos = request.find("/preview?");
      std::vector<BYTE> png;
      if (qpos != std::string::npos) {
        auto hpos = request.find(" HTTP", qpos);
        if (hpos != std::string::npos) {
          std::string query = request.substr(qpos + 9, hpos - qpos - 9);
          png = buildPreviewPng(query);
        }
      }
      if (!png.empty()) {
        std::ostringstream header;
        header << "HTTP/1.1 200 OK\r\n"
               << "Content-Type: image/png\r\n"
               << "Cache-Control: no-store, no-cache, must-revalidate\r\n"
               << "Pragma: no-cache\r\n"
               << "Content-Length: " << png.size() << "\r\n"
               << "Connection: close\r\n\r\n";
        std::string h = header.str();
        send(client, h.c_str(), static_cast<int>(h.size()), 0);
        send(client, reinterpret_cast<const char*>(png.data()),
             static_cast<int>(png.size()), 0);
      } else {
        std::string resp = "HTTP/1.1 500 Internal Server Error\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n\r\n";
        send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
      }
      closesocket(client);
    } else if (request.find("GET /browse") != std::string::npos) {
      wchar_t folder[MAX_PATH] = {};
      BROWSEINFOW bi = {};
      std::wstring browse_title = localization::toWide(localization::strings().select_output_directory);
      bi.lpszTitle = browse_title.c_str();
      bi.ulFlags = BIF_RETURNONLYFSDIRS;
      // Center the folder picker on screen so it is not hidden behind
      // the browser window that opened it.  The classic dialog (without
      // BIF_NEWDIALOGSTYLE) has a fixed size at BFFM_INITIALIZED, so
      // the centering math is reliable at every resolution.
      bi.lpfn = [](HWND hwnd, UINT msg, LPARAM, LPARAM) -> int {
        if (msg == BFFM_INITIALIZED) {
          RECT rc = {};
          GetWindowRect(hwnd, &rc);
          int w = rc.right - rc.left;
          int h = rc.bottom - rc.top;
          int sw = GetSystemMetrics(SM_CXSCREEN);
          int sh = GetSystemMetrics(SM_CYSCREEN);
          int x = (sw - w) / 2;
          int y = (sh - h) / 2;
          SetForegroundWindow(hwnd);
          SetWindowPos(hwnd, HWND_TOPMOST, x, y, 0, 0,
                       SWP_NOSIZE | SWP_NOACTIVATE);
          SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
          SetForegroundWindow(hwnd);
        }
        return 0;
      };
      LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
      if (pidl) {
        SHGetPathFromIDListW(pidl, folder);
        CoTaskMemFree(pidl);
      }
      char folder_utf8[MAX_PATH * 4] = {};
      if (folder[0]) {
        WideCharToMultiByte(CP_UTF8, 0, folder, -1, folder_utf8,
                            sizeof(folder_utf8), nullptr, nullptr);
      }
      std::string resp = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain; charset=utf-8\r\n"
                         "Connection: close\r\n\r\n" + std::string(folder_utf8);
      send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
      closesocket(client);
    } else if (request.find("GET /submit?") != std::string::npos ||
               request.find("GET /submit ") != std::string::npos) {
      auto qpos = request.find("/submit?");
      if (qpos != std::string::npos) {
        auto hpos = request.find(" HTTP", qpos);
        if (hpos != std::string::npos) {
          std::string query = request.substr(qpos + 8, hpos - qpos - 8);
          self->parseFormData(query);
        }
      }
      const auto& text = localization::strings();
      std::string body =
          std::string("<!DOCTYPE html><html><head><meta charset='utf-8'>") +
          "<title>" + text.app_title + "</title>"
          "<script>window.open('','_self');window.close();</script>"
          "</head><body " +
          "style='font-family:Segoe UI,Arial;text-align:center;margin-top:60px;'>" +
          "<h2>" + text.request_received + "</h2>" +
          "<p>" + text.close_tab_now + "</p></body></html>";
      std::string response = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/html; charset=utf-8\r\n"
                             "Connection: close\r\n\r\n" + body;
      send(client, response.c_str(), static_cast<int>(response.size()), 0);
      closesocket(client);
      self->closeBrowserWindow();
      self->running_ = false;
      break;
    } else {
      // Handle favicon.ico and any other path with a 404 so the browser
      // doesn't keep the connection open waiting for data.
      std::string resp = "HTTP/1.1 404 Not Found\r\n"
                         "Content-Length: 0\r\n"
                         "Connection: close\r\n\r\n";
      send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
      closesocket(client);
    }
  }
  CoUninitialize();
  return 0;
}
void SettingsServer::closeBrowserWindow() const {
  HWND hwnd = browser_window_;
  if (!hwnd || !IsWindow(hwnd)) {
    std::wstring needle = localization::toWide(localization::strings().app_title);
    hwnd = FindWindowW(nullptr, needle.c_str());
    if (hwnd == nullptr) {
      struct Ctx { std::wstring n; HWND h; } ctx = {needle, nullptr};
      EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        wchar_t buf[512] = {};
        if (GetWindowTextW(h, buf, 512) && wcsstr(buf, c->n.c_str())) {
          c->h = h;
          return FALSE;
        }
        return TRUE;
      }, reinterpret_cast<LPARAM>(&ctx));
      hwnd = ctx.h;
    }
  }
  if (hwnd && IsWindow(hwnd)) {
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
  }
}
void SettingsServer::initDefaultOutputDir() {
  char pics[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_MYPICTURES, nullptr, 0, pics))) {
    default_output_dir_ = std::string(pics) + "\\BNTechScans";
  } else {
    char user[MAX_PATH] = {};
    if (GetEnvironmentVariableA("USERPROFILE", user, MAX_PATH))
      default_output_dir_ = std::string(user) + "\\Pictures\\BNTechScans";
    else
      default_output_dir_ = "C:\\BNTechScans";
  }
}
bool SettingsServer::showSettingsUi(const std::string& /*html_dir*/,
                                     SettingsUiResult& out_result) {
  initDefaultOutputDir();
  WSADATA wsa_data = {};
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    return false;
  }
  server_port_ = findFreePort();
  if (server_port_ <= 0) {
    WSACleanup();
    return false;
  }
  listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_socket_ == INVALID_SOCKET) {
    WSACleanup();
    return false;
  }
  int opt = 1;
  setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char*>(&opt), sizeof(opt));
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<u_short>(server_port_));
  if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closesocket(listen_socket_);
    WSACleanup();
    return false;
  }
  if (listen(listen_socket_, 5) != 0) {
    closesocket(listen_socket_);
    WSACleanup();
    return false;
  }
  running_ = true;
  result_ = out_result;
  server_thread_ = CreateThread(nullptr, 0, serverThreadProc, this, 0, nullptr);
  if (server_thread_ == nullptr) {
    closesocket(listen_socket_);
    WSACleanup();
    return false;
  }
  std::string url = "http://127.0.0.1:" + std::to_string(server_port_) + "/";
  ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

  // The browser window may not appear instantly.  Poll for up to 3 seconds
  // looking for a top-level window whose title contains the app name, then
  // centre it on screen so it is not left wherever the browser last closed.
  {
    const int kW = 540;
    const int kH = result_.app_managed_file_output ? 650 : 750;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int tx = (sw - kW) / 2;
    int ty = (sh - kH) / 2;
    std::wstring needle = localization::toWide(localization::strings().app_title);
    HWND found = nullptr;
    for (int i = 0; i < 30; ++i) {
      found = FindWindowW(nullptr, needle.c_str());
      if (found == nullptr) {
        // Some browsers append " - BrowserName" to the title; try a
        // prefix search via EnumWindows as a fallback.
        struct Ctx { std::wstring n; HWND h; } ctx = {needle, nullptr};
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
          auto* c = reinterpret_cast<Ctx*>(lp);
          wchar_t buf[512] = {};
          if (GetWindowTextW(h, buf, 512) && wcsstr(buf, c->n.c_str())) {
            c->h = h;
            return FALSE;
          }
          return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        found = ctx.h;
      }
      if (found != nullptr) {
        browser_window_ = found;
        if (IsZoomed(found)) ShowWindow(found, SW_RESTORE);
        SetWindowPos(found, nullptr, tx, ty, kW, kH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // Remove sizing border and maximize button so the user cannot
        // resize the compact settings dialog.
        LONG style = GetWindowLongW(found, GWL_STYLE);
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        SetWindowLongW(found, GWL_STYLE, style);
        SetWindowPos(found, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
        break;
      }
      Sleep(100);
    }
  }

  WaitForSingleObject(server_thread_, 60000);
  running_ = false;
  closesocket(listen_socket_);
  CloseHandle(server_thread_);
  WSACleanup();
  out_result = result_;
  return true;
}
bool SettingsServer::isRunning() const {
  return running_;
}