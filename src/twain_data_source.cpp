// TWAIN Data Source implementation: state machine, message routing,
// image acquisition, and DIB construction.  Also hosts the inlined
// DSM interface functions (memory management and event signalling).

#include "twain_data_source.h"
#include "unit_convert.h"
#include <windows.h>
#include <cstring>
#include <algorithm>
#include <cstdlib>

// --- DSM Interface (formerly dsm_interface.cpp) ---

// Handle to the system TWAIN_32.dll (or replacement DSM library).
static HMODULE g_dsm_module = nullptr;
// Cached entry points from the DSM, including memory management functions.
static TW_ENTRYPOINT g_dsm_entry = {};

static bool loadDsmLib(const char* lib_name) {
  if (g_dsm_module != nullptr) {
    return true;
  }
  std::memset(&g_dsm_entry, 0, sizeof(TW_ENTRYPOINT));
  g_dsm_module = LoadLibraryA(lib_name);
  if (g_dsm_module == nullptr) {
    return false;
  }
  auto* func = reinterpret_cast<DSMENTRYPROC>(
      GetProcAddress(g_dsm_module, "DSM_Entry"));
  if (func == nullptr) {
    FreeLibrary(g_dsm_module);
    g_dsm_module = nullptr;
    return false;
  }
  g_dsm_entry.DSM_Entry = func;
  return true;
}

static void unloadDsmLib() {
  if (g_dsm_module != nullptr) {
    std::memset(&g_dsm_entry, 0, sizeof(TW_ENTRYPOINT));
    FreeLibrary(g_dsm_module);
    g_dsm_module = nullptr;
  }
}

static TW_UINT16 callDsmEntry(pTW_IDENTITY origin, pTW_IDENTITY dest,
                               TW_UINT32 dg, TW_UINT16 dat, TW_UINT16 msg,
                               TW_MEMREF data) {
  if (g_dsm_entry.DSM_Entry == nullptr) {
    char dsm_path[MAX_PATH] = {};
    if (GetWindowsDirectoryA(dsm_path, MAX_PATH) == 0) {
      return TWRC_FAILURE;
    }
    std::strcat(dsm_path, "\\TWAIN_32.dll");
    if (!loadDsmLib(dsm_path)) {
      return TWRC_FAILURE;
    }
  }
  if (g_dsm_entry.DSM_Entry == nullptr) {
    return TWRC_FAILURE;
  }
  return g_dsm_entry.DSM_Entry(origin, dest, dg, dat, msg, data);
}

static void setEntryPoints(pTW_ENTRYPOINT entry_points) {
  if (entry_points != nullptr) {
    g_dsm_entry = *entry_points;
  } else {
    std::memset(&g_dsm_entry, 0, sizeof(TW_ENTRYPOINT));
  }
}

static TW_HANDLE dsmAlloc(TW_UINT32 size) {
  if (g_dsm_entry.DSM_MemAllocate != nullptr) {
    return g_dsm_entry.DSM_MemAllocate(size);
  }
  return GlobalAlloc(GPTR, size);
}

static void dsmFree(TW_HANDLE handle) {
  if (g_dsm_entry.DSM_MemFree != nullptr) {
    g_dsm_entry.DSM_MemFree(handle);
    return;
  }
  GlobalFree(handle);
}

static TW_MEMREF dsmLockMemory(TW_HANDLE handle) {
  if (g_dsm_entry.DSM_MemLock != nullptr) {
    return g_dsm_entry.DSM_MemLock(handle);
  }
  return static_cast<TW_MEMREF>(GlobalLock(handle));
}

static void dsmUnlockMemory(TW_HANDLE handle) {
  if (g_dsm_entry.DSM_MemUnlock != nullptr) {
    g_dsm_entry.DSM_MemUnlock(handle);
    return;
  }
  GlobalUnlock(handle);
}
#ifdef DS_DEBUG_LOG
  #define DS_LOG(msg) OutputDebugStringA(msg)
  #define DS_LOG_FMT(fmt, ...) do { char _buf[512]; \
      _snprintf_s(_buf, sizeof(_buf), fmt, __VA_ARGS__); \
      OutputDebugStringA(_buf); } while(0)
#else
  #define DS_LOG(msg) ((void)0)
  #define DS_LOG_FMT(fmt, ...) ((void)0)
#endif

// Rounds up a scanline width to the nearest DWORD (4-byte) boundary.
#define BYTES_PERLINE(width, bpp) ((((width) * (bpp)) + 31) / 32 * 4)

// Static identity template for the virtual scanner.
// Manufacturer/product/version strings are shared across all instances.
TW_IDENTITY TwainDataSource::s_the_identity = {
  0,  
  {   
    1,              
    9,              
    TWLG_ENGLISH,   
    TWCY_USA,       
    "1.9.0 bntech"  
  },
  1,                                    
  9,                                    
  DG_IMAGE | DG_CONTROL,               
  "BN Tech",                           
  "Virtual Scanner",                    
  "BN Tech Virtual Scanner"            
};
TwainDataSource::TwainDataSource(const TW_IDENTITY& /*app_id*/)
    : state_(DsState::kLoaded),
      condition_code_(TWCC_SUCCESS),
      image_data_(nullptr),
      canceled_(false),
      xfer_pending_(false) {
  std::memset(&identity_, 0, sizeof(identity_));
  std::memset(&app_, 0, sizeof(app_));
  std::memset(&pending_xfers_, 0, sizeof(pending_xfers_));
  std::memset(&image_info_, 0, sizeof(image_info_));
}
TwainDataSource::~TwainDataSource() {
  if (image_data_ != nullptr) {
    dsmFree(image_data_);
    image_data_ = nullptr;
  }
}
TW_INT16 TwainDataSource::initialize() {
  caps_.initialize();
  identity_ = s_the_identity;
  return TWRC_SUCCESS;
}
// Central dispatch for all TWAIN (DG, DAT, MSG) triples.
// Routes DG_CONTROL messages to the corresponding handleDatXxx functions
// and DG_IMAGE messages to image-info and native-transfer handlers.
TW_UINT16 TwainDataSource::dsEntry(pTW_IDENTITY origin, TW_UINT32 dg,
                                     TW_UINT16 dat, TW_UINT16 msg,
                                     TW_MEMREF data) {
  if (dg == DG_CONTROL) {
    switch (dat) {
      case DAT_EVENT:
        return handleDatEvent(msg, static_cast<pTW_EVENT>(data));
      case DAT_IDENTITY:
        return handleDatIdentity(origin, msg, static_cast<pTW_IDENTITY>(data));
      case DAT_CAPABILITY:
        return handleDatCapability(msg, static_cast<pTW_CAPABILITY>(data));
      case DAT_USERINTERFACE:
        return handleDatUserInterface(msg,
                                      static_cast<pTW_USERINTERFACE>(data));
      case DAT_STATUS:
        return handleDatStatus(msg, static_cast<pTW_STATUS>(data));
      case DAT_PENDINGXFERS:
        return handleDatPendingXfers(msg,
                                     static_cast<pTW_PENDINGXFERS>(data));
      case DAT_ENTRYPOINT:
        return handleDatEntryPoint(msg, static_cast<pTW_ENTRYPOINT>(data));
      case DAT_XFERGROUP:
        return handleDatXferGroup(msg, static_cast<pTW_UINT32>(data));
      case DAT_IMAGELAYOUT:
        return handleDatImageLayout(msg, static_cast<pTW_IMAGELAYOUT>(data));
      default:
        condition_code_ = TWCC_BADPROTOCOL;
        return TWRC_FAILURE;
    }
  }
  if (dg == DG_IMAGE) {
    switch (dat) {
      case DAT_IMAGEINFO:
        return handleDatImageInfo(msg, static_cast<pTW_IMAGEINFO>(data));
      case DAT_IMAGENATIVEXFER:
        if (data != nullptr) {
          return handleDatImageNativeXfer(
              msg, *static_cast<TW_HANDLE*>(data));
        }
        condition_code_ = TWCC_BADVALUE;
        return TWRC_FAILURE;
      default:
        condition_code_ = TWCC_CAPUNSUPPORTED;
        return TWRC_FAILURE;
    }
  }
  condition_code_ = TWCC_CAPUNSUPPORTED;
  return TWRC_FAILURE;
}
TW_INT16 TwainDataSource::handleDatIdentity(pTW_IDENTITY origin, TW_UINT16 msg,
                                             pTW_IDENTITY data) {
  switch (msg) {
    case MSG_GET:
      std::memcpy(data, &identity_, sizeof(TW_IDENTITY));
      return TWRC_SUCCESS;
    case MSG_OPENDS:
      identity_.Id = data->Id;
      return openDs(origin);
    case MSG_CLOSEDS:
      return closeDs();
    default:
      condition_code_ = TWCC_BADPROTOCOL;
      return TWRC_FAILURE;
  }
}
TW_INT16 TwainDataSource::handleDatCapability(TW_UINT16 msg,
                                                pTW_CAPABILITY data) {
  if (data == nullptr) {
    condition_code_ = TWCC_BADVALUE;
    return TWRC_FAILURE;
  }
  if (msg == MSG_RESETALL) {
    if (state_ >= DsState::kEnabled) {
      condition_code_ = TWCC_SEQERROR;
      return TWRC_FAILURE;
    }
    caps_.resetAll();
    return TWRC_SUCCESS;
  }
  return caps_.handleCapability(msg, data);
}
TW_INT16 TwainDataSource::handleDatUserInterface(TW_UINT16 msg,
                                                   pTW_USERINTERFACE data) {
  if (data == nullptr) {
    condition_code_ = TWCC_BADVALUE;
    return TWRC_FAILURE;
  }
  switch (msg) {
    case MSG_ENABLEDS:
      return enableDs(data);
    case MSG_ENABLEDSUIONLY:
      return enableDsOnly(data);
    case MSG_DISABLEDS:
      return disableDs(data);
    default:
      condition_code_ = TWCC_BADPROTOCOL;
      return TWRC_FAILURE;
  }
}
TW_INT16 TwainDataSource::handleDatEvent(TW_UINT16 msg, pTW_EVENT data) {
  if (data == nullptr) {
    condition_code_ = TWCC_BADVALUE;
    return TWRC_FAILURE;
  }
  if (msg == MSG_PROCESSEVENT) {
    return processEvent(data);
  }
  condition_code_ = TWCC_BADPROTOCOL;
  return TWRC_FAILURE;
}
TW_INT16 TwainDataSource::handleDatStatus(TW_UINT16 msg, pTW_STATUS data) {
  if (data == nullptr) {
    condition_code_ = TWCC_BADVALUE;
    return TWRC_FAILURE;
  }
  if (msg == MSG_GET) {
    data->ConditionCode = condition_code_;
    condition_code_ = TWCC_SUCCESS;
    return TWRC_SUCCESS;
  }
  condition_code_ = TWCC_BADPROTOCOL;
  return TWRC_FAILURE;
}
TW_INT16 TwainDataSource::handleDatPendingXfers(TW_UINT16 msg,
                                                  pTW_PENDINGXFERS data) {
  switch (msg) {
    case MSG_ENDXFER:
      return endXfer(data);
    case MSG_GET:
      return getXfer(data);
    case MSG_RESET:
      return resetXfer(data);
    default:
      condition_code_ = TWCC_BADPROTOCOL;
      return TWRC_FAILURE;
  }
}
TW_INT16 TwainDataSource::handleDatEntryPoint(TW_UINT16 msg,
                                               pTW_ENTRYPOINT data) {
  if (msg == MSG_SET) {
    setEntryPoints(data);
    return TWRC_SUCCESS;
  }
  condition_code_ = TWCC_BADPROTOCOL;
  return TWRC_FAILURE;
}
TW_INT16 TwainDataSource::handleDatXferGroup(TW_UINT16 msg, pTW_UINT32 data) {
  if (data == nullptr) {
    condition_code_ = TWCC_BADVALUE;
    return TWRC_FAILURE;
  }
  if (msg == MSG_GET) {
    *data = DG_IMAGE;
    return TWRC_SUCCESS;
  }
  if (msg == MSG_SET) {
    if (*data != DG_IMAGE) {
      condition_code_ = TWCC_BADVALUE;
      return TWRC_FAILURE;
    }
    return TWRC_SUCCESS;
  }
  condition_code_ = TWCC_BADPROTOCOL;
  return TWRC_FAILURE;
}
TW_INT16 TwainDataSource::handleDatImageLayout(TW_UINT16 msg,
                                                  pTW_IMAGELAYOUT data) {
  if (data == nullptr) {
    condition_code_ = TWCC_BADVALUE;
    return TWRC_FAILURE;
  }
  switch (msg) {
    case MSG_GET:
    case MSG_GETCURRENT: {
      data->Frame.Left = floatToFix32(0.0f);
      data->Frame.Top = floatToFix32(0.0f);
      data->Frame.Right = floatToFix32(8.5f);
      data->Frame.Bottom = floatToFix32(11.0f);
      data->DocumentNumber = 1;
      data->PageNumber = 1;
      data->FrameNumber = 1;
      return TWRC_SUCCESS;
    }
    case MSG_SET: return TWRC_SUCCESS;
    default:
      condition_code_ = TWCC_CAPUNSUPPORTED;
      return TWRC_FAILURE;
  }
}
TW_INT16 TwainDataSource::handleDatImageInfo(TW_UINT16 msg, pTW_IMAGEINFO data) {
  if (msg == MSG_GET) {
    if (state_ == DsState::kEnabled && pending_xfers_.Count != 0) {
      state_ = DsState::kXferReady;
      xfer_pending_ = false;
      getImageInfo(&image_info_);
    }
    return getImageInfo(data);
  }
  condition_code_ = TWCC_BADPROTOCOL;
  return TWRC_FAILURE;
}
// Handles native image transfer.  Validates state transitions:
// kEnabled + pending → promote to kXferReady first;
// kEnabled + no pending → TWRC_CANCEL (no images);
// kXferReady → call transfer() then wrap result as DIB.
TW_INT16 TwainDataSource::handleDatImageNativeXfer(TW_UINT16 msg,
                                                      TW_HANDLE& data) {
  if (msg != MSG_GET) {
    condition_code_ = TWCC_BADPROTOCOL;
    return TWRC_FAILURE;
  }
  if (state_ == DsState::kEnabled && pending_xfers_.Count != 0) {
    state_ = DsState::kXferReady;
    xfer_pending_ = false;
    getImageInfo(&image_info_);
  }
  if (state_ == DsState::kEnabled && pending_xfers_.Count == 0) {
    condition_code_ = TWCC_SUCCESS;
    return TWRC_CANCEL;
  }
  if (state_ != DsState::kXferReady) {
    DS_LOG_FMT("ds: NativeXfer - wrong state %d\n", static_cast<int>(state_));
    condition_code_ = TWCC_SEQERROR;
    return TWRC_FAILURE;
  }
  DS_LOG("ds: NativeXfer - calling transfer()\n");
  TW_INT16 twrc = transfer();
  DS_LOG_FMT("ds: NativeXfer - transfer() returned %d\n", twrc);
  if (twrc == TWRC_SUCCESS) {
    twrc = getDibImage(data);
    DS_LOG_FMT("ds: NativeXfer - getDibImage() returned %d\n", twrc);
    if (twrc == TWRC_SUCCESS) {
      twrc = TWRC_XFERDONE;
      state_ = DsState::kXferring;
    }
  }
  return twrc;
}
// Opens a TWAIN connection.  Rejects if already connected to an app
// or if not in kLoaded state.  Resets the virtual scanner on success.
TW_INT16 TwainDataSource::openDs(pTW_IDENTITY origin) {
  if (app_.Id != 0) {
    condition_code_ = TWCC_MAXCONNECTIONS;
    return TWRC_FAILURE;
  }
  if (state_ != DsState::kLoaded) {
    condition_code_ = TWCC_SEQERROR;
    return TWRC_FAILURE;
  }
  app_ = *origin;
  if (!scanner_.resetScanner()) {
    condition_code_ = TWCC_BUMMER;
    return TWRC_FAILURE;
  }
  state_ = DsState::kOpen;
  return TWRC_SUCCESS;
}
TW_INT16 TwainDataSource::closeDs() {
  if (state_ == DsState::kLoaded) {
    return TWRC_SUCCESS;
  }
  if (image_data_ != nullptr) {
    dsmFree(image_data_);
    image_data_ = nullptr;
  }
  scanner_.unlock();
  std::memset(&app_, 0, sizeof(app_));
  state_ = DsState::kLoaded;
  return TWRC_SUCCESS;
}
// Enables the DS for scanning.  Wraps the image index, applies current
// capability settings to the scanner, acquires the next image, and
// notifies the application via MSG_XFERREADY.  On any failure, the
// DS falls back to kOpen state via the fail label.
TW_INT16 TwainDataSource::enableDs(pTW_USERINTERFACE data) {
  if (state_ != DsState::kOpen) {
    condition_code_ = TWCC_SEQERROR;
    return TWRC_FAILURE;
  }
  state_ = DsState::kEnabled;
  canceled_ = false;
  xfer_pending_ = false;
  scanner_.wrapImageIndex();
  scanner_.lock();
  if (!updateScannerFromCaps()) {
    goto fail;
  }
  if (!scanner_.acquireImage()) {
    goto fail;
  }
  pending_xfers_.Count = 1;
  xfer_pending_ = true;
  if (doXferReadyEvent()) {
    xfer_pending_ = false;
  }
  return TWRC_SUCCESS;
fail:
  condition_code_ = TWCC_BADVALUE;
  state_ = DsState::kOpen;
  return TWRC_FAILURE;
}
TW_INT16 TwainDataSource::enableDsOnly(pTW_USERINTERFACE /*data*/) {
  if (state_ != DsState::kOpen) {
    condition_code_ = TWCC_SEQERROR;
    return TWRC_FAILURE;
  }
  state_ = DsState::kEnabled;
  return TWRC_SUCCESS;
}
TW_INT16 TwainDataSource::disableDs(pTW_USERINTERFACE /*data*/) {
  if (state_ != DsState::kEnabled) {
    condition_code_ = TWCC_SEQERROR;
    return TWRC_FAILURE;
  }
  scanner_.unlock();
  state_ = DsState::kOpen;
  return TWRC_SUCCESS;
}
// Processes application events.  If there is a pending transfer and
// the DS is still in kEnabled state, promotes to kXferReady and fills
// image info so the app can retrieve it.
TW_INT16 TwainDataSource::processEvent(pTW_EVENT data) {
  if (state_ < DsState::kEnabled) {
    condition_code_ = TWCC_SEQERROR;
    return TWRC_FAILURE;
  }
  if (xfer_pending_ && state_ == DsState::kEnabled) {
    state_ = DsState::kXferReady;
    xfer_pending_ = false;
    getImageInfo(&image_info_);
    data->TWMessage = MSG_XFERREADY;
    return TWRC_DSEVENT;
  }
  data->TWMessage = MSG_NULL;
  return TWRC_NOTDSEVENT;
}
// Fills a TW_IMAGEINFO structure from current capability settings and
// the scanner's loaded image dimensions.  Pixel type determines BPP
// (1 for BW, 8 for gray, 24 for RGB) and samples-per-pixel.
TW_INT16 TwainDataSource::getImageInfo(pTW_IMAGEINFO info) {
  if (state_ < DsState::kXferReady) {
    condition_code_ = TWCC_SEQERROR;
    return TWRC_FAILURE;
  }
  std::memset(info, 0, sizeof(TW_IMAGEINFO));
  int pt = TWPT_RGB;
  caps_.getCurrentValue(ICAP_PIXELTYPE, pt);
  float xr = 300.0f;
  float yr = 300.0f;
  caps_.getCurrentValue(ICAP_XRESOLUTION, xr);
  caps_.getCurrentValue(ICAP_YRESOLUTION, yr);
  info->XResolution = floatToFix32(xr);
  info->YResolution = floatToFix32(yr);
  info->ImageWidth = scanner_.getImageWidth();
  info->ImageLength = scanner_.getImageHeight();
  static const int kBpp[] = {1, 8, 24};
  static const int kSamp[] = {1, 1, 3};
  int pi = (pt == TWPT_BW ? 0 : pt == TWPT_GRAY ? 1 : 2);
  info->PixelType = static_cast<TW_UINT16>(pt);
  info->BitsPerPixel = static_cast<TW_INT16>(kBpp[pi]);
  info->SamplesPerPixel = static_cast<TW_INT16>(kSamp[pi]);
  for (int i = 0; i < kSamp[pi]; ++i) {
    info->BitsPerSample[i] = 8;
  }
  info->Planar = FALSE;
  info->Compression = TWCP_NONE;
  return TWRC_SUCCESS;
}
TW_INT16 TwainDataSource::endXfer(pTW_PENDINGXFERS xf) {
  if (!(state_ == DsState::kXferReady || state_ == DsState::kXferring)) {
    condition_code_ = TWCC_SEQERROR;
    return TWRC_FAILURE;
  }
  if (canceled_) {
    canceled_ = false;
    pending_xfers_.Count = 0;
  }
  pending_xfers_.Count = 0;
  scanner_.unlock();
  state_ = DsState::kEnabled;
  if (xf == nullptr) {
    condition_code_ = TWCC_BADVALUE;
    return TWRC_CHECKSTATUS;
  }
  *xf = pending_xfers_;
  return TWRC_SUCCESS;
}
TW_INT16 TwainDataSource::getXfer(pTW_PENDINGXFERS xf) {
  if (state_ < DsState::kOpen) {
    condition_code_ = TWCC_SEQERROR;
    return TWRC_FAILURE;
  }
  if (xf == nullptr) {
    condition_code_ = TWCC_BADVALUE;
    return TWRC_CHECKSTATUS;
  }
  *xf = pending_xfers_;
  return TWRC_SUCCESS;
}
TW_INT16 TwainDataSource::resetXfer(pTW_PENDINGXFERS xf) {
  if (state_ == DsState::kLoaded) {
    condition_code_ = TWCC_SEQERROR;
    return TWRC_FAILURE;
  }
  pending_xfers_.Count = 0;
  state_ = DsState::kEnabled;
  scanner_.unlock();
  if (xf == nullptr) {
    condition_code_ = TWCC_BADVALUE;
    return TWRC_FAILURE;
  }
  *xf = pending_xfers_;
  return TWRC_SUCCESS;
}
// Transfers image data from the VirtualScanner into an internal buffer.
// Reads strip-by-strip (up to 64000 bytes per call), rounding down to
// whole scanlines to avoid partial-line artifacts.
TW_INT16 TwainDataSource::transfer() {
  if (canceled_) {
    canceled_ = false;
    return TWRC_CANCEL;
  }
  if (state_ != DsState::kXferReady) {
    condition_code_ = TWCC_SEQERROR;
    return TWRC_FAILURE;
  }
  if (!(image_info_.ImageWidth && image_info_.ImageLength)) {
    condition_code_ = TWCC_CAPUNSUPPORTED;
    return TWRC_FAILURE;
  }
  DWORD bpr = BYTES_PERLINE(image_info_.ImageWidth,
                            image_info_.BitsPerPixel);
  DWORD sz = bpr * image_info_.ImageLength;
  if (image_data_ != nullptr) {
    dsmFree(image_data_);
    image_data_ = nullptr;
  }
  image_data_ = dsmAlloc(sz);
  if (image_data_ == nullptr) {
    condition_code_ = TWCC_LOWMEMORY;
    return TWRC_FAILURE;
  }
  BYTE* p = static_cast<BYTE*>(dsmLockMemory(image_data_));
  DWORD rem = sz;
  do {
    DWORD n = std::min(static_cast<DWORD>(64000), rem) / bpr * bpr;
    if (n == 0) {
      n = bpr;
    }
    DWORD got = 0;
    if (!scanner_.getScanStrip(p, n, got) || got == 0) {
      break;
    }
    p += got;
    rem -= got;
  } while (rem > 0);
  dsmUnlockMemory(image_data_);
  return TWRC_SUCCESS;
}
// Sends MSG_XFERREADY to the application via the DSM.
// On success, sets internal state to kXferReady and caches image info.
bool TwainDataSource::doXferReadyEvent() {
  if (state_ != DsState::kEnabled) {
    return false;
  }
  TW_UINT16 rc = callDsmEntry(
      getIdentity(), &app_, DG_CONTROL, DAT_NULL, MSG_XFERREADY, nullptr);
  if (TWRC_SUCCESS == rc) {
    state_ = DsState::kXferReady;
    getImageInfo(&image_info_);
    return true;
  }
  return false;
}
bool TwainDataSource::doCloseDsOkEvent() {
  if (state_ != DsState::kEnabled) {
    condition_code_ = TWCC_SEQERROR;
    return false;
  }
  TW_UINT16 rc = callDsmEntry(
      getIdentity(), &app_, DG_CONTROL, DAT_NULL, MSG_CLOSEDSOK, nullptr);
  return TWRC_SUCCESS == rc;
}
bool TwainDataSource::doCloseDsRequestEvent() {
  if (state_ != DsState::kEnabled) {
    return false;
  }
  callDsmEntry(
      getIdentity(), &app_, DG_CONTROL, DAT_NULL, MSG_CLOSEDSREQ, nullptr);
  return true;
}
// Copies current capability values (pixel type, resolution) into the
// virtual scanner's settings so subsequent image acquisition uses them.
bool TwainDataSource::updateScannerFromCaps() {
  ScannerSettings s = scanner_.getSettings();
  caps_.getCurrentValue(ICAP_PIXELTYPE, s.pixel_type);
  float xr = 300.0f;
  float yr = 300.0f;
  caps_.getCurrentValue(ICAP_XRESOLUTION, xr);
  caps_.getCurrentValue(ICAP_YRESOLUTION, yr);
  s.x_resolution = xr;
  s.y_resolution = yr;
  scanner_.setSettings(s);
  return true;
}
// Wraps the raw image data into a complete DIB (Device Independent Bitmap)
// with BITMAPINFOHEADER, optional color palette, and pixel data.
// BW (1 bpp) gets a 2-entry B/W palette; gray (8 bpp) gets 256 gray entries.
// RGB (24 bpp) uses no palette.  Pixel rows are stored bottom-up.
// R/B channels are swapped in RGB mode (FreeImage stores BGR internally).
TW_INT16 TwainDataSource::getDibImage(TW_HANDLE& h) {
  if (image_data_ == nullptr) {
    condition_code_ = TWCC_BADVALUE;
    return TWRC_FAILURE;
  }
  h = nullptr;
  WORD bpp = image_info_.BitsPerPixel;
  DWORD w = image_info_.ImageWidth;
  DWORD hgt = image_info_.ImageLength;
  DWORD sbpr = BYTES_PERLINE(w, bpp);
  DWORD dbpr = BYTES_PERLINE(w, bpp);
  WORD nc = (bpp == 1 ? 2 : bpp == 8 ? 256 : 0);
  DWORD ps = sizeof(RGBQUAD) * nc;
  DWORD dsz = sizeof(BITMAPINFOHEADER) + ps + dbpr * hgt;
  TW_HANDLE hd = dsmAlloc(dsz);
  if (hd == nullptr) {
    condition_code_ = TWCC_LOWMEMORY;
    return TWRC_FAILURE;
  }
  auto* bi = static_cast<PBITMAPINFOHEADER>(dsmLockMemory(hd));
  if (bi == nullptr) {
    dsmFree(hd);
    condition_code_ = TWCC_LOWMEMORY;
    return TWRC_FAILURE;
  }
  bi->biSize = sizeof(BITMAPINFOHEADER);
  bi->biWidth = static_cast<LONG>(w);
  bi->biHeight = static_cast<LONG>(hgt);
  bi->biPlanes = 1;
  bi->biBitCount = bpp;
  bi->biCompression = 0;
  bi->biSizeImage = dbpr * hgt;
  bi->biXPelsPerMeter = static_cast<LONG>(
      fix32ToFloat(image_info_.XResolution) * 39.37f + 0.5f);
  bi->biYPelsPerMeter = static_cast<LONG>(
      fix32ToFloat(image_info_.YResolution) * 39.37f + 0.5f);
  bi->biClrUsed = nc;
  bi->biClrImportant = nc;
  dsmUnlockMemory(hd);
  BYTE* db = static_cast<BYTE*>(dsmLockMemory(hd));
  if (db == nullptr) {
    dsmFree(hd);
    condition_code_ = TWCC_LOWMEMORY;
    return TWRC_FAILURE;
  }
  BYTE* dst = db + sizeof(BITMAPINFOHEADER);
  if (nc == 2) {
    auto* pal = reinterpret_cast<RGBQUAD*>(dst);
    pal[0] = {0, 0, 0, 0};
    pal[1] = {0xFF, 0xFF, 0xFF, 0};
    dst += ps;
  } else if (nc == 256) {
    auto* pal = reinterpret_cast<RGBQUAD*>(dst);
    for (int i = 0; i < 256; ++i) {
      pal[i] = {static_cast<BYTE>(i), static_cast<BYTE>(i),
                static_cast<BYTE>(i), 0};
    }
    dst += ps;
  } else {
    dst += ps;
  }
  BYTE* src = static_cast<BYTE*>(dsmLockMemory(image_data_));
  if (src == nullptr) {
    dsmUnlockMemory(hd);
    dsmFree(hd);
    condition_code_ = TWCC_LOWMEMORY;
    return TWRC_FAILURE;
  }
  for (DWORD r = 0; r < hgt; ++r) {
    BYTE* sr = src + sbpr * (hgt - r - 1);
    if (bpp == 24) {
      for (DWORD c = 0; c < w; ++c) {
        dst[c * 3] = sr[c * 3 + 2];
        dst[c * 3 + 1] = sr[c * 3 + 1];
        dst[c * 3 + 2] = sr[c * 3];
      }
    } else {
      std::memcpy(dst, sr, sbpr);
    }
    if (dbpr > sbpr) {
      std::memset(dst + sbpr, 0, dbpr - sbpr);
    }
    dst += dbpr;
  }
  dsmUnlockMemory(image_data_);
  dsmUnlockMemory(hd);
  h = hd;
  return TWRC_SUCCESS;
}