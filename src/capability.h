// TWAIN capability negotiation module.
// Manages reading and writing of scanner capabilities (ICAP_* and CAP_*)
// through the TWAIN protocol's DAT_CAPABILITY triples.

#ifndef CAPABILITY_H_
#define CAPABILITY_H_
#include "twain.h"
#include <vector>
#include <map>

// Query support bitmask shortcuts.
enum { kCapGet = TWQC_GET, kCapSet = TWQC_SET,
       kCapAll = TWQC_GET | TWQC_SET | TWQC_GETCURRENT | TWQC_GETDEFAULT | TWQC_RESET };

// A single TWAIN capability entry holding current value, default,
// supported values, and negotiated container/item type information.
struct CapEntry {
  TW_UINT16 cap_id;
  TW_UINT16 item_type;
  TW_UINT16 get_con_type;
  TW_UINT32 query_support;
  int current_value;
  int default_value;
  std::vector<int> supported_values;
};

// Central manager for all DS-supported TWAIN capabilities.
// Handles MSG_GET, MSG_SET, MSG_RESET, MSG_RESETALL, and MSG_QUERYSUPPORT.
// Builds containers (ONEVALUE, ENUMERATION, ARRAY) for responses.
class CapabilityManager {
 public:
  CapabilityManager() {}

  // Registers the 8 supported capabilities with default values.
  void initialize();

  // Routes a TWAIN message to the appropriate handler (GET, SET, etc.).
  TW_INT16 handleCapability(TW_UINT16 msg, pTW_CAPABILITY cap);

  // Reads the current value of a capability as int.
  bool getCurrentValue(TW_UINT16 cap_id, int& value) const;
  // Convenience overload for bool-typed caps.
  bool getCurrentValue(TW_UINT16 cap_id, bool& value) const {
    int v; return getCurrentValue(cap_id, v) && (value = (v != 0), true);
  }
  // Convenience overload for float/FIX32-typed caps.
  bool getCurrentValue(TW_UINT16 cap_id, float& value) const {
    int v; return getCurrentValue(cap_id, v) && (value = static_cast<float>(v), true);
  }

  // Sets the current value of a capability, rejecting unsupported values.
  bool setCurrentValue(TW_UINT16 cap_id, int value);
  // Convenience overload for bool-typed caps.
  bool setCurrentValue(TW_UINT16 cap_id, bool value) {
    return setCurrentValue(cap_id, value ? TRUE : FALSE);
  }

  // Resets all capabilities to their default values.
  void resetAll();

  // Returns the list of supported capability IDs (excluding self-referencing CAP).
  std::vector<TW_UINT16> getSupportedCaps() const;

 private:
  // Finds a capability entry by ID, returning nullptr if not registered.
  CapEntry* findCap(TW_UINT16 cap_id);

  // Registers a single capability during initialization.
  void addCap(TW_UINT16 id, TW_UINT16 type, TW_UINT16 con_type,
              TW_UINT32 qs, int def, std::vector<int> sv = {});

  // Builds a TWAIN container (ONEVALUE, ENUMERATION, or ARRAY) for GET responses.
  TW_HANDLE buildContainer(CapEntry* entry, TW_UINT16 msg);

  // Parses a TWAIN container from a SET request and applies the new value.
  TW_INT16 applyContainer(CapEntry* entry, TW_UINT16 con_type, TW_HANDLE hc);

  std::map<TW_UINT16, CapEntry> caps_;
};

#endif
