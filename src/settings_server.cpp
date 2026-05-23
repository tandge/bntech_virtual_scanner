#include "settings_server.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <map>
#pragma comment(lib, "ws2_32.lib")
SettingsServer::SettingsServer()
    : server_thread_(nullptr),
      running_(false),
      server_port_(0),
      listen_socket_(INVALID_SOCKET) {
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
  auto sel = [](int v, int cur) { return v == cur ? " selected" : ""; };
  std::string out_dir = result_.output_dir[0]
      ? std::string(result_.output_dir) : default_output_dir_;
  std::ostringstream html;
  html << "<!DOCTYPE html>\n<html><head><meta charset='utf-8'>\n";
  html << "<title>BN Tech Virtual Scanner</title>\n";
  html << "<style>\n";
  html << "body{font-family:Segoe UI,Arial,sans-serif;margin:20px;background:#f5f5f5;}\n";
  html << "h1{color:#333;font-size:18px;}\n";
  html << ".group{background:#fff;border:1px solid #ddd;border-radius:4px;"
       << "padding:16px;margin-bottom:16px;}\n";
  html << ".group h2{margin-top:0;color:#555;font-size:14px;}\n";
  html << "label{display:inline-block;width:140px;margin:4px 0;}\n";
  html << "select,input{margin:4px 0;padding:4px;}\n";
  html << ".buttons{text-align:right;margin-top:16px;}\n";
  html << "button{padding:8px 24px;margin-left:8px;font-size:14px;"
       << "border:none;border-radius:4px;cursor:pointer;}\n";
  html << ".scan{background:#0078d7;color:#fff;}\n";
  html << ".cancel{background:#ccc;color:#333;}\n";
  html << "</style></head><body>\n";
  html << "<h1>BN Tech Virtual Scanner <span style='font-size:11px;color:#999;'>[" << __DATE__ << " " << __TIME__ << "]</span></h1>\n";
  html << "<div class='group'><h2>Scan Settings</h2>\n";
  html << "<label>Color Mode:</label>\n";
  html << "<select id='pixeltype' name='pixeltype'>\n";
  html << "<option value='0'" << sel(0, result_.pixel_type) << ">Black and White</option>\n";
  html << "<option value='1'" << sel(1, result_.pixel_type) << ">Grayscale</option>\n";
  html << "<option value='2'" << sel(2, result_.pixel_type) << ">Color</option>\n";
  html << "</select><br>\n";
  html << "<label>Resolution (DPI):</label>\n";
  html << "<select id='resolution' name='resolution'>\n";
  html << "<option value='150'" << sel(150, result_.resolution) << ">150</option>\n";
  html << "<option value='200'" << sel(200, result_.resolution) << ">200</option>\n";
  html << "<option value='300'" << sel(300, result_.resolution) << ">300</option>\n";
  html << "<option value='600'" << sel(600, result_.resolution) << ">600</option>\n";
  html << "</select><br>\n";
  html << "</div>\n";
  if (result_.app_managed_file_output) {
    // Application has already chosen File-transfer mode and will supply the
    // destination path via DAT_SETUPFILEXFER.  Hide all file-output controls
    // so the user only edits scan settings (color, resolution).
    html << "<div class='group'><h2>Output Settings</h2>\n";
    html << "<p style='color:#666;margin:4px 0;'>File transfer mode (application-managed).</p>\n";
    html << "</div>\n";
  } else {
    html << "<div class='group'><h2>Output Settings</h2>\n";
    html << "<label>Transfer Mode:</label>\n";
    html << "<select id='transfermode' name='transfermode' onchange='updateMode()'>\n";
    html << "<option value='0'" << sel(0, result_.transfer_mode) << ">Native (Memory)</option>\n";
    html << "<option value='1'" << sel(1, result_.transfer_mode) << ">File</option>\n";
    html << "</select><br>\n";
    html << "<div id='row_format'>\n";
    html << "<label>File Format:</label>\n";
    html << "<select id='fileformat' name='fileformat'>\n";
    html << "<option value='0'" << sel(0, result_.file_format) << ">PNG</option>\n";
    html << "<option value='1'" << sel(1, result_.file_format) << ">JPG</option>\n";
    html << "<option value='2'" << sel(2, result_.file_format) << ">BMP</option>\n";
    html << "<option value='3'" << sel(3, result_.file_format) << ">TIFF</option>\n";
    html << "</select><br>\n";
    html << "</div>\n";
    html << "<div id='row_output'>\n";
    html << "<label>Output Directory:</label>\n";
    html << "<input type='text' id='outputdir' name='outputdir' style='width:240px;' value='" << out_dir << "'>\n";
    html << "<button onclick='browseDir()' style='padding:4px 10px;margin-left:4px;'>Browse...</button><br>\n";
    html << "<label>Output Filename:</label>\n";
    html << "<input type='text' id='outputfilename' name='outputfilename' style='width:240px;' value='" << result_.output_filename << "'>\n";
    html << "<span id='outputext' style='color:#666;margin-left:4px;'></span><br>\n";
    html << "</div>\n";
    html << "</div>\n";
  }
  html << "<div class='buttons'>\n";
  html << "<button class='cancel' onclick='doCancel()'>Cancel</button>\n";
  html << "<button class='scan' onclick='doScan()'>Scan</button>\n";
  html << "</div>\n";
  html << "<script>\n";
  html << "var EXTS=['.png','.jpg','.bmp','.tif'];\n";
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
  html << "function doScan(){\n";
  html << "  var p={};\n";
  html << "  p.pixeltype=val('pixeltype','');\n";
  html << "  p.resolution=val('resolution','');\n";
  html << "  p.fileformat=val('fileformat','');\n";
  html << "  p.transfermode=val('transfermode','');\n";
  html << "  p.outputdir=val('outputdir','');\n";
  html << "  p.outputfilename=val('outputfilename','');\n";
  html << "  p.action='scan';\n";
  html << "  var qs=Object.keys(p).map(function(k){"
       << "return encodeURIComponent(k)+'='+encodeURIComponent(p[k])}).join('&');\n";
  html << "  fetch('/submit?'+qs).finally(function(){window.close();});\n";
  html << "}\n";
  html << "function doCancel(){\n";
  html << "  fetch('/submit?action=cancel').finally(function(){window.close();});\n";
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
  std::istringstream stream(form_data);
  std::string pair;
  std::map<std::string, std::string> params;
  while (std::getline(stream, pair, '&')) {
    auto eq = pair.find('=');
    if (eq != std::string::npos) {
      std::string key = pair.substr(0, eq);
      std::string val = pair.substr(eq + 1);
      std::string decoded;
      for (size_t i = 0; i < val.size(); ++i) {
        if (val[i] == '%' && i + 2 < val.size()) {
          char hex[3] = {val[i+1], val[i+2], 0};
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
  }
  result_.scan_clicked = (params["action"] == "scan");
  result_.pixel_type = std::atoi(params["pixeltype"].c_str());
  result_.resolution = std::atoi(params["resolution"].c_str());
  result_.file_format = std::atoi(params["fileformat"].c_str());
  result_.transfer_mode = std::atoi(params["transfermode"].c_str());
  std::strncpy(result_.output_dir, params["outputdir"].c_str(), MAX_PATH - 1);
  result_.output_dir[MAX_PATH - 1] = '\0';
  std::strncpy(result_.output_filename, params["outputfilename"].c_str(), MAX_PATH - 1);
  result_.output_filename[MAX_PATH - 1] = '\0';
}
DWORD WINAPI SettingsServer::serverThreadProc(LPVOID param) {
  auto* self = static_cast<SettingsServer*>(param);
  if (self == nullptr) return 1;
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
    } else if (request.find("GET /browse") != std::string::npos) {
      char folder[MAX_PATH] = {};
      BROWSEINFOA bi = {};
      bi.lpszTitle = "Select Output Directory";
      bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
      // Bring the folder picker to the foreground and center it on screen
      // so it is not hidden behind the browser window that opened it.
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
      LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
      if (pidl) {
        SHGetPathFromIDListA(pidl, folder);
        CoTaskMemFree(pidl);
      }
      std::string resp = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain\r\n"
                         "Connection: close\r\n\r\n" + std::string(folder);
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
      std::string body =
          "<!DOCTYPE html><html><head><meta charset='utf-8'>"
          "<title>BN Tech Virtual Scanner</title></head><body "
          "style='font-family:Segoe UI,Arial;text-align:center;margin-top:60px;'>"
          "<h2>Request received</h2>"
          "<p>You may close this browser tab now.</p></body></html>";
      std::string response = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/html; charset=utf-8\r\n"
                             "Connection: close\r\n\r\n" + body;
      send(client, response.c_str(), static_cast<int>(response.size()), 0);
      closesocket(client);
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
  return 0;
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