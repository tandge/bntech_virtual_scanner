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
  int page_size;         // 0=US Letter, 1=US Legal, 2=A4, 3=A5.
  int page_fill_mode;    // 0=Stretch, 1=Fit with padding, 2=Fill and crop.
  int rotation;          // 0=0 deg, 1=90 deg, 2=180 deg, 3=270 deg clockwise.
  int flip;              // 0=None, 1=Horizontal, 2=Vertical.
  int file_format;       // 0=PNG, 1=JPG, 2=BMP, 3=TIFF, 4=WEBP, 5=GIF
  int transfer_mode;     // 0=Native, 1=File
  char output_dir[MAX_PATH];
  char output_filename[MAX_PATH];  // Base filename (no extension); extension is
                                    // derived from file_format.
  // Input-only flag: when true, the calling application has already chosen
  // File-transfer mode and will supply the destination path itself via
  // DAT_SETUPFILEXFER.  In that case, the settings UI hides the transfer
  // mode selector and the file-output fields, leaving only the scan
  // settings (color, resolution) editable by the user.
  bool app_managed_file_output;
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
  void initDefaultOutputDir();
  void closeBrowserWindow() const;
  static DWORD WINAPI serverThreadProc(LPVOID param);
  void parseFormData(const std::string& form_data);
  HANDLE server_thread_;
  volatile bool running_;
  int server_port_;
  SettingsUiResult result_;
  SOCKET listen_socket_;
  HWND browser_window_;
  std::string default_output_dir_;
};
#endif  