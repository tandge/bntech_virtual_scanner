// Virtual scanner that simulates a physical flatbed scanner.
// Reads images from a local directory, converts them per the requested
// pixel type and resolution, and outputs scan data one strip at a time
// to mimic real scanner behavior.

#ifndef VIRTUAL_SCANNER_H_
#define VIRTUAL_SCANNER_H_
#include "twain.h"
#include "FreeImage.h"
#include <string>
#include <vector>
#include <mutex>

// Scanner operational parameters controlled by TWAIN capabilities.
struct ScannerSettings {
  int pixel_type;      // TWPT_BW, TWPT_GRAY, or TWPT_RGB.
  float x_resolution;  // Horizontal DPI.
  float y_resolution;  // Vertical DPI.
};

class VirtualScanner {
public:
  VirtualScanner();
  ~VirtualScanner();

  // Resets scanner state: unlocks, clears image, resets settings to defaults.
  bool resetScanner();

  // Loads the next image from the image directory.
  // Returns false if no more images are available.
  bool acquireImage();

  // Reads up to bytes_to_read of the next image strip in bottom-up DIB order.
  // bytes_received is set to the number of bytes actually read.
  bool getScanStrip(BYTE* buffer, DWORD bytes_to_read, DWORD& bytes_received);

  // Sets the output directory and format for file transfer mode.
  void setOutputDir(const std::string& dir) { output_dir_ = dir; }
  void setOutputFormat(int fmt) { output_format_ = fmt; }
  // Sets the base output filename (without extension).  When empty, a
  // timestamped name is generated automatically by saveImageToFile().
  void setOutputFilename(const std::string& name) { output_filename_ = name; }
  // Saves the current image to output_dir_ with the configured format.
  bool saveImageToFile();
  // Saves the current image to the given absolute or relative path.
  // The file format is derived from the path's extension (defaults to PNG
  // if the extension is unrecognized or missing).  Relative paths are
  // resolved against the process working directory.
  bool saveImageToPath(const std::string& path);
  // Returns the path of the last saved file.
  std::string getLastSavedFilePath() const { return last_saved_file_; }

  ScannerSettings getSettings() const;
  void setSettings(const ScannerSettings& settings);

  // Always returns false (this is a flatbed scanner, no feeder).
  bool isFeederLoaded() const;

  // Always returns true (the virtual scanner is always online).
  bool getDeviceOnline() const;

  // Lock/unlock the scanner for exclusive access during a scan session.
  void lock();
  void unlock();

  // RAII lock guard: calls unlock() on destruction.
  // Use ScopedLocker locker(scanner.lock()) or simply scanner.lock();
  // the guard is movable but non-copyable.
  struct ScopedLocker {
    explicit ScopedLocker(VirtualScanner* s) : scanner(s) {}
    ScopedLocker(const ScopedLocker&) = delete;
    ScopedLocker& operator=(const ScopedLocker&) = delete;
    ScopedLocker(ScopedLocker&& other) noexcept
        : scanner(other.scanner) { other.scanner = nullptr; }
    ScopedLocker& operator=(ScopedLocker&& other) noexcept {
      if (this != &other) { scanner = other.scanner; other.scanner = nullptr; }
      return *this;
    }
    ~ScopedLocker() { if (scanner) scanner->unlock(); }
    VirtualScanner* scanner;
  };

  // Acquires a scoped lock; returned guard auto-unlocks when destroyed.
  ScopedLocker scopedLock() { lock(); return ScopedLocker(this); }

  // Dimensions of the currently loaded image in pixels.
  int getImageWidth() const;
  int getImageHeight() const;
  int getBitsPerPixel() const;

  // Wraps the image index back to 0 when past the end of the list.
  void wrapImageIndex();

  // Total number of images available in the directory.
  size_t getImageCount() const { return image_list_.size(); }

  // Number of images remaining after the current index.
  size_t getRemainingImageCount() const {
    return (current_image_index_ < image_list_.size())
        ? image_list_.size() - current_image_index_
        : 0;
  }

private:
  // Scans the image directory for supported image files.
  void scanImageDirectory();

  // Returns the path to the default fallback image.
  std::string getDefaultImagePath() const;

  // Prepares the loaded image for scanning: converts pixel type,
  // swaps color channels, and calculates row sizes.
  bool preScanPrep();

  // Ensures the loaded DIB is in 24-bit RGB format (required for uniform processing).
  bool ensure24BitDib();

  // Scales the image so that the output pixel dimensions match the requested
  // DPI.  Source DPI is read from the image metadata (defaults to 300 if
  // missing).  scale = target / source; FreeImage_Rescale is used.
  bool applyResolutionScaling();

  // Applies pixel type conversion (BW threshold or gray).  Also sets DPI
  // metadata to match the requested resolution so the output files / DIB
  // carry the correct DPI tags.
  bool applyPixelFormat();

  // Writes the requested resolution into the DIB using both FreeImage's
  // pixels-per-meter fields and EXIF/TIFF resolution tags.  This is called
  // immediately before saving as some conversions can drop metadata.
  void applyDpiMetadata();

  // Patches the saved file when a format needs container-level DPI fields that
  // FreeImage may omit (for example PNG pHYs for Windows Explorer details).
  void patchSavedDpiMetadata(FREE_IMAGE_FORMAT fif, const std::string& path);

  // Calculates DIB-compatible bytes-per-row and resets the scan line counter.
  void calculateRowParams();

  // Loads/saves the current image index to info.json for persistence.
  void loadImageIndex();
  void saveImageIndex() const;

  FIBITMAP* dib_;
  int scan_line_;
  bool locked_;
  int dest_bytes_per_row_;
  int row_offset_;
  std::vector<std::string> image_list_;
  size_t current_image_index_;
  std::string image_dir_;
  std::string default_image_path_;
  ScannerSettings settings_;
  std::string output_dir_;
  int output_format_;       // FreeImage format index (0=PNG, 1=JPG, etc.)
  std::string output_filename_;  // Base filename (no extension); empty = auto.
  std::string last_saved_file_;

public:
  // DLL module handle, set during DLL_PROCESS_ATTACH.
  static HMODULE g_hinstance;
};
#endif  
