#include "settings_server.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
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
  html << "<h1>BN Tech Virtual Scanner</h1>\n";
  html << "<div class='group'><h2>Scan Settings</h2>\n";
  html << "<label>Color Mode:</label>\n";
  html << "<select id='pixeltype' name='pixeltype'>\n";
  html << "<option value='0'>Black and White</option>\n";
  html << "<option value='1'>Grayscale</option>\n";
  html << "<option value='2' selected>Color</option>\n";
  html << "</select><br>\n";
  html << "<label>Resolution (DPI):</label>\n";
  html << "<select id='resolution' name='resolution'>\n";
  html << "<option value='150'>150</option>\n";
  html << "<option value='200'>200</option>\n";
  html << "<option value='300' selected>300</option>\n";
  html << "<option value='600'>600</option>\n";
  html << "</select><br>\n";
  html << "</div>\n";
  html << "<div class='buttons'>\n";
  html << "<button class='cancel' onclick='doCancel()'>Cancel</button>\n";
  html << "<button class='scan' onclick='doScan()'>Scan</button>\n";
  html << "</div>\n";
  html << "<script>\n";
  html << "function doScan(){\n";
  html << "  var p={};\n";
  html << "  p.pixeltype=document.getElementById('pixeltype').value;\n";
  html << "  p.resolution=document.getElementById('resolution').value;\n";
  html << "  p.action='scan';\n";
  html << "  var qs=Object.keys(p).map(function(k){"
       << "return encodeURIComponent(k)+'='+encodeURIComponent(p[k])}).join('&');\n";
  html << "  fetch('http://127.0.0.1:" << port << "/submit?'+qs)\n"
       << ".then(function(){window.close()});\n";
  html << "}\n";
  html << "function doCancel(){\n";
  html << "  fetch('http://127.0.0.1:" << port << "/submit?action=cancel')\n"
       << ".then(function(){window.close()});\n";
  html << "}\n";
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
                             "Connection: close\r\n\r\n" + html;
      send(client, response.c_str(), static_cast<int>(response.size()), 0);
      closesocket(client);
    } else if (request.find("GET /submit?") != std::string::npos) {
      auto qpos = request.find("/submit?");
      auto hpos = request.find(" HTTP", qpos);
      if (hpos != std::string::npos) {
        std::string query = request.substr(qpos + 8, hpos - qpos - 8);
        self->parseFormData(query);
      }
      std::string response = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/plain\r\n"
                             "Connection: close\r\n\r\nOK";
      send(client, response.c_str(), static_cast<int>(response.size()), 0);
      closesocket(client);
      self->running_ = false;
      break;
    } else {
      closesocket(client);
    }
  }
  return 0;
}
bool SettingsServer::showSettingsUi(const std::string& /*html_dir*/,
                                     SettingsUiResult& out_result) {
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
  result_ = SettingsUiResult();
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