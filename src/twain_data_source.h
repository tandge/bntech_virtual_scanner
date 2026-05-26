// TWAIN Data Source main controller.
// Implements the full TWAIN DS state machine including DS opening,
// capability negotiation, image acquisition, and native transfer.
// Routes incoming (DG, DAT, MSG) triples from the DSM to the
// appropriate handler functions.

#ifndef TWAIN_DATA_SOURCE_H_
#define TWAIN_DATA_SOURCE_H_
#include "twain.h"
#include "capability.h"
#include "virtual_scanner.h"

// TWAIN protocol Data Source states per the TWAIN specification.
// Starts at kLoaded (3) since states 1-2 are managed by the DSM.
enum class DsState { kLoaded = 3, kOpen, kEnabled, kXferReady, kXferring };

class TwainDataSource {
 public:
  explicit TwainDataSource(const TW_IDENTITY& app_id);
  ~TwainDataSource();

  // Central dispatch for all DG/DAT/MSG triples received by the DS.
  TW_UINT16 dsEntry(pTW_IDENTITY origin, TW_UINT32 dg,
                     TW_UINT16 dat, TW_UINT16 msg, TW_MEMREF data);

  // Initializes capabilities and copies the static identity.
  TW_INT16 initialize();

  // Returns a pointer to this DS's identity block.
  pTW_IDENTITY getIdentity() { return &identity_; }

  // Static TWAIN identity descriptor shared across all instances.
  static TW_IDENTITY s_the_identity;

 private:
  // --- DG_CONTROL / DAT_xxx Handlers ---

  // DAT_IDENTITY: Returns DS identity, opens/closes the DS connection.
  TW_INT16 handleDatIdentity(pTW_IDENTITY origin, TW_UINT16 msg, pTW_IDENTITY data);

  // DAT_CAPABILITY: Routes to CapabilityManager.  Also handles MSG_RESETALL.
  TW_INT16 handleDatCapability(TW_UINT16 msg, pTW_CAPABILITY data);

  // DAT_USERINTERFACE: MSG_ENABLEDS starts scanning, MSG_DISABLEDS stops.
  TW_INT16 handleDatUserInterface(TW_UINT16 msg, pTW_USERINTERFACE data);

  // DAT_EVENT: Dispatches application events (MSG_PROCESSEVENT).
  TW_INT16 handleDatEvent(TW_UINT16 msg, pTW_EVENT data);

  // DAT_STATUS: Returns the last condition code, clearing it on read.
  TW_INT16 handleDatStatus(TW_UINT16 msg, pTW_STATUS data);

  // DAT_PENDINGXFERS: Manages the pending-transfer count (end/get/reset).
  TW_INT16 handleDatPendingXfers(TW_UINT16 msg, pTW_PENDINGXFERS data);

  // DAT_ENTRYPOINT: Saves the DSM entry points for memory management.
  TW_INT16 handleDatEntryPoint(TW_UINT16 msg, pTW_ENTRYPOINT data);

  // DAT_XFERGROUP: Returns/sets the transfer group (always DG_IMAGE).
  TW_INT16 handleDatXferGroup(TW_UINT16 msg, pTW_UINT32 data);

  // DAT_IMAGELAYOUT: Returns a fixed US Letter (8.5x11 in) layout.
  TW_INT16 handleDatImageLayout(TW_UINT16 msg, pTW_IMAGELAYOUT data);

  // --- DG_IMAGE / DAT_xxx Handlers ---

  // DAT_IMAGEINFO: Returns pixel type, resolution, and dimensions.
  TW_INT16 handleDatImageInfo(TW_UINT16 msg, pTW_IMAGEINFO data);

  // DAT_IMAGENATIVEXFER: Performs the actual image transfer as DIB.
  TW_INT16 handleDatImageNativeXfer(TW_UINT16 msg, TW_HANDLE& data);

  // DAT_IMAGEFILEXFER: Returns the saved file path for file-based transfer.
  TW_INT16 handleDatImageFileXfer(TW_UINT16 msg, pTW_SETUPFILEXFER data);

  // DAT_SETUPFILEXFER: Accepts / returns the file-save path from the app.
  TW_INT16 handleDatSetupFileXfer(TW_UINT16 msg, pTW_SETUPFILEXFER data);

  // DAT_SETUPMEMXFER: Returns the DS's preferred / min / max buffer sizes
  // for memory-mode strip transfers.
  TW_INT16 handleDatSetupMemXfer(TW_UINT16 msg, pTW_SETUPMEMXFER data);

  // DAT_IMAGEMEMXFER: Streams the next strip of pixel data into the
  // application-supplied buffer.  Returns TWRC_XFERDONE on the last strip.
  TW_INT16 handleDatImageMemXfer(TW_UINT16 msg, pTW_IMAGEMEMXFER data);

  // --- State Machine ---

  // Opens a TWAIN connection from an application.
  TW_INT16 openDs(pTW_IDENTITY origin);

  // Closes the connection, freeing all resources.
  TW_INT16 closeDs();

  // Enables the DS: reads capabilities, acquires image, sends XFERREADY.
  TW_INT16 enableDs(pTW_USERINTERFACE data);

  // Enables the DS without performing a scan (ShowUI-only mode).
  TW_INT16 enableDsOnly(pTW_USERINTERFACE data);

  // Disables the DS, returning to kOpen state.
  TW_INT16 disableDs(pTW_USERINTERFACE data);

  // --- Event / Transfer ---

  // Checks for pending xfers and emits MSG_XFERREADY if ready.
  TW_INT16 processEvent(pTW_EVENT data);

  // Fills the TW_IMAGEINFO struct from current caps and scanner state.
  TW_INT16 getImageInfo(pTW_IMAGEINFO info);

  // Ends the current transfer, unlocks scanner, returns to kEnabled.
  TW_INT16 endXfer(pTW_PENDINGXFERS xfers);

  // Returns the current pending-transfer count.
  TW_INT16 getXfer(pTW_PENDINGXFERS xfers);

  // Resets the pending-transfer count and scanner state.
  TW_INT16 resetXfer(pTW_PENDINGXFERS xfers);

  // Copies image data from VirtualScanner into internal buffer via strips.
  TW_INT16 transfer();

  // Notifies the application that image data is ready.
  bool doXferReadyEvent();

  // Notifies the application that the DS closed normally.
  bool doCloseDsOkEvent();

  // Requests the application to close the DS.
  bool doCloseDsRequestEvent();

  // Reads current capability values and applies them to the scanner settings.
  bool updateScannerFromCaps();

  // Wraps raw image data into a DIB header + palette + pixel data.
  TW_INT16 getDibImage(TW_HANDLE& h_image);

  // Allocates DIB memory and fills BITMAPINFOHEADER + color palette.
  TW_HANDLE allocAndFillDibHeader();

  // Copies pixel rows from the internal image buffer into the DIB,
  // performing R/B channel swap for 24-bit images.
  bool copyDibPixelData(BYTE* dib_pixels);

  CapabilityManager caps_;
  VirtualScanner scanner_;
  DsState state_;
  TW_IDENTITY identity_;
  TW_IDENTITY app_;
  TW_INT16 condition_code_;
  TW_PENDINGXFERS pending_xfers_;
  TW_IMAGEINFO image_info_;
  TW_HANDLE image_data_;
  bool canceled_;
  bool xfer_pending_;
  // Path supplied by the application via DAT_SETUPFILEXFER / MSG_SET.
  // Used to pre-fill the settings UI and to pick the actual save location
  // when ShowUI=FALSE.  Empty until the application provides one.
  std::string app_file_path_;
  // Byte offset into image_data_ for the next memory-mode strip.
  // Reset when a new transfer starts; advances as strips are handed out.
  TW_UINT32 mem_xfer_offset_;
};

#endif
