#ifndef SETTINGS_SERVER_H_
#define SETTINGS_SERVER_H_
#include "twain.h"
#include <winsock2.h>
#include <windows.h>
#include <string>
struct SettingsUiResult {
  bool scan_clicked;   
  int pixel_type;      
  int resolution;      
};
class SettingsServer {
public:
  SettingsServer();
  ~SettingsServer();
  bool showSettingsUi(const std::string& html_dir, SettingsUiResult& out_result);
  bool isRunning() const;
private:
  int findFreePort() const;
  std::string buildHtmlPage(int port) const;
  static DWORD WINAPI serverThreadProc(LPVOID param);
  void parseFormData(const std::string& form_data);
  HANDLE server_thread_;
  volatile bool running_;
  int server_port_;
  SettingsUiResult result_;
  SOCKET listen_socket_;
};
#endif  