// TWAIN capability manager implementation.
// Uses GlobalAlloc for TWAIN container memory; the DSM entry point
// memory functions are not guaranteed to be available during capability
// negotiation.

#include "capability.h"
#include "unit_convert.h"
#include <cstring>
#include <algorithm>
#include <windows.h>

void CapabilityManager::addCap(TW_UINT16 id, TW_UINT16 type, TW_UINT16 con_type,
                                TW_UINT32 qs, int def, std::vector<int> sv) {
  CapEntry e;
  e.cap_id = id;
  e.item_type = type;
  e.get_con_type = con_type;
  e.query_support = qs;
  e.default_value = def;
  e.current_value = def;
  e.supported_values = sv;
  caps_[id] = e;
}

void CapabilityManager::initialize() {
  caps_.clear();
  addCap(CAP_SUPPORTEDCAPS,  TWTY_UINT16, TWON_ARRAY,       kCapGet, 0);
  addCap(ICAP_XFERMECH,       TWTY_UINT16, TWON_ONEVALUE,    kCapAll, TWSX_NATIVE,    {TWSX_NATIVE, TWSX_FILE, TWSX_MEMORY});
  addCap(ICAP_PIXELTYPE,      TWTY_UINT16, TWON_ENUMERATION, kCapAll, TWPT_RGB,       {TWPT_BW, TWPT_GRAY, TWPT_RGB});
  addCap(ICAP_XRESOLUTION,    TWTY_FIX32,  TWON_ENUMERATION, kCapAll, 300,            {150, 200, 300, 600});
  addCap(ICAP_YRESOLUTION,    TWTY_FIX32,  TWON_ENUMERATION, kCapAll, 300,            {150, 200, 300, 600});
  // Resolution values are pixels per ICAP_UNITS.  Explicitly advertise inches
  // so applications that save the native transfer use DPI/PPI instead of a
  // screen-default fallback such as 96 DPI.
  addCap(ICAP_UNITS,          TWTY_UINT16, TWON_ONEVALUE,    kCapAll, TWUN_INCHES,      {TWUN_INCHES});
  addCap(CAP_FEEDERENABLED,   TWTY_BOOL,   TWON_ONEVALUE,    kCapGet, FALSE,          {FALSE});
  addCap(ICAP_PIXELFLAVOR,    TWTY_UINT16, TWON_ONEVALUE,    kCapAll, TWPF_CHOCOLATE, {TWPF_CHOCOLATE});
  addCap(ICAP_IMAGEFILEFORMAT, TWTY_UINT16, TWON_ENUMERATION, kCapAll, TWFF_PNG, {TWFF_TIFF, TWFF_BMP, TWFF_JFIF, TWFF_PNG});
  addCap(CAP_UICONTROLLABLE,  TWTY_BOOL,   TWON_ONEVALUE,    kCapGet, TRUE,           {TRUE});
}

CapEntry* CapabilityManager::findCap(TW_UINT16 cap_id) {
  auto it = caps_.find(cap_id);
  return it != caps_.end() ? &it->second : nullptr;
}

bool CapabilityManager::getCurrentValue(TW_UINT16 cap_id, int& value) const {
  auto it = caps_.find(cap_id);
  if (it == caps_.end()) return false;
  value = it->second.current_value;
  return true;
}

bool CapabilityManager::setCurrentValue(TW_UINT16 cap_id, int value) {
  auto it = caps_.find(cap_id);
  if (it == caps_.end()) return false;
  CapEntry& e = it->second;
  if (!e.supported_values.empty()) {
    bool ok = false;
    for (int v : e.supported_values) {
      if (v == value) { ok = true; break; }
    }
    if (!ok) return false;
  }
  e.current_value = value;
  return true;
}

void CapabilityManager::resetAll() {
  for (auto& p : caps_) {
    p.second.current_value = p.second.default_value;
  }
}

std::vector<TW_UINT16> CapabilityManager::getSupportedCaps() const {
  std::vector<TW_UINT16> result;
  for (const auto& p : caps_) {
    if (p.first != CAP_SUPPORTEDCAPS) result.push_back(p.first);
  }
  return result;
}

TW_INT16 CapabilityManager::handleCapability(TW_UINT16 msg, pTW_CAPABILITY cap) {
  if (!cap) return TWRC_FAILURE;

  if (msg == MSG_QUERYSUPPORT) {
    CapEntry* e = findCap(cap->Cap);
    if (!e) return TWRC_FAILURE;
    cap->ConType = TWON_ONEVALUE;
    cap->hContainer = GlobalAlloc(GPTR,sizeof(TW_ONEVALUE));
    if (!cap->hContainer) return TWRC_FAILURE;
    auto* p = static_cast<pTW_ONEVALUE>(GlobalLock(cap->hContainer));
    p->ItemType = TWTY_UINT32;
    p->Item = e->query_support;
    GlobalUnlock(cap->hContainer);
    return TWRC_SUCCESS;
  }

  if (msg == MSG_RESETALL) {
    resetAll();
    return TWRC_SUCCESS;
  }

  CapEntry* e = findCap(cap->Cap);
  if (!e) return TWRC_FAILURE;

  if (msg == MSG_GET || msg == MSG_GETCURRENT || msg == MSG_GETDEFAULT) {
    cap->ConType = e->get_con_type;
    cap->hContainer = buildContainer(e, msg);
    return cap->hContainer ? TWRC_SUCCESS : TWRC_FAILURE;
  }
  if (msg == MSG_SET) return applyContainer(e, cap->ConType, cap->hContainer);
  if (msg == MSG_RESET) {
    e->current_value = e->default_value;
    cap->ConType = e->get_con_type;
    cap->hContainer = buildContainer(e, MSG_GETCURRENT);
    return cap->hContainer ? TWRC_SUCCESS : TWRC_FAILURE;
  }
  return TWRC_FAILURE;
}

static void writeFix32Item(void* dst, int val) {
  TW_FIX32 f = floatToFix32(static_cast<float>(val));
  std::memcpy(dst, &f, sizeof(TW_FIX32));
}

// Builds the appropriate TWAIN container for a GET/GETCURRENT/GETDEFAULT response.
// Supports TW_ARRAY (for CAP_SUPPORTEDCAPS), TW_ONEVALUE, and TW_ENUMERATION.
TW_HANDLE CapabilityManager::buildContainer(CapEntry* e, TW_UINT16 msg) {
  if (!e) return nullptr;
  int val = (msg == MSG_GETDEFAULT) ? e->default_value : e->current_value;

  if (e->cap_id == CAP_SUPPORTEDCAPS) {
    // Special case: return the list of all other supported caps as an array.
    auto caps = getSupportedCaps();
    TW_UINT32 n = static_cast<TW_UINT32>(caps.size());
    TW_UINT32 sz = sizeof(TW_ARRAY) + (n - 1) * sizeof(TW_UINT16);
    TW_HANDLE h = GlobalAlloc(GPTR,sz);
    if (!h) return nullptr;
    auto* a = static_cast<pTW_ARRAY>(GlobalLock(h));
    a->ItemType = TWTY_UINT16;
    a->NumItems = n;
    auto* items = reinterpret_cast<TW_UINT16*>(a->ItemList);
    for (TW_UINT32 i = 0; i < n; ++i) items[i] = caps[i];
    GlobalUnlock(h);
    return h;
  }

  if (e->get_con_type == TWON_ONEVALUE) {
    TW_HANDLE h = GlobalAlloc(GPTR,sizeof(TW_ONEVALUE));
    if (!h) return nullptr;
    auto* p = static_cast<pTW_ONEVALUE>(GlobalLock(h));
    p->ItemType = e->item_type;
    if (e->item_type == TWTY_FIX32) {
      writeFix32Item(&p->Item, val);
    } else if (e->item_type == TWTY_BOOL) {
      p->Item = val ? TRUE : FALSE;
    } else {
      p->Item = static_cast<TW_UINT32>(val);
    }
    GlobalUnlock(h);
    return h;
  }

  if (e->get_con_type == TWON_ENUMERATION) {
    TW_UINT32 n = static_cast<TW_UINT32>(e->supported_values.size());
    if (!n) n = 1;
    TW_UINT32 isz = (e->item_type == TWTY_FIX32) ? sizeof(TW_FIX32) : sizeof(TW_UINT16);
    TW_UINT32 sz = sizeof(TW_ENUMERATION) + n * isz;
    TW_HANDLE h = GlobalAlloc(GPTR,sz);
    if (!h) return nullptr;
    auto* pe = static_cast<pTW_ENUMERATION>(GlobalLock(h));
    pe->ItemType = e->item_type;
    pe->NumItems = n;
    TW_UINT32 ci = 0;
    for (TW_UINT32 i = 0; i < n; ++i) {
      int sv = i < e->supported_values.size() ? e->supported_values[i] : val;
      if (e->item_type == TWTY_FIX32) {
        reinterpret_cast<TW_FIX32*>(pe->ItemList)[i] = floatToFix32(static_cast<float>(sv));
      } else {
        reinterpret_cast<TW_UINT16*>(pe->ItemList)[i] = static_cast<TW_UINT16>(sv);
      }
      if (sv == val) ci = i;
    }
    pe->CurrentIndex = ci;
    pe->DefaultIndex = 0;
    GlobalUnlock(h);
    return h;
  }
  return nullptr;
}

// Parses a SET container (ONEVALUE or ENUMERATION) and applies the value
// after validating it against the supported-values list.
TW_INT16 CapabilityManager::applyContainer(CapEntry* e, TW_UINT16 con, TW_HANDLE hc) {
  if (!e || !hc || !(e->query_support & TWQC_SET)) return TWRC_FAILURE;
  auto* d = static_cast<BYTE*>(GlobalLock(hc));
  if (!d) return TWRC_FAILURE;

  int nv = 0;
  if (con == TWON_ONEVALUE) {
    auto* p = reinterpret_cast<pTW_ONEVALUE>(d);
    if (e->item_type == TWTY_FIX32) {
      TW_FIX32 f;
      std::memcpy(&f, &p->Item, sizeof(f));
      nv = static_cast<int>(fix32ToFloat(f) + 0.5f);
    } else if (e->item_type == TWTY_BOOL) {
      nv = p->Item ? TRUE : FALSE;
    } else {
      nv = static_cast<int>(p->Item);
    }
  } else if (con == TWON_ENUMERATION) {
    auto* pe = reinterpret_cast<pTW_ENUMERATION>(d);
    TW_UINT32 idx = pe->CurrentIndex;
    if (idx >= pe->NumItems) {
      GlobalUnlock(hc);
      return TWRC_FAILURE;
    }
    if (e->item_type == TWTY_FIX32) {
      nv = static_cast<int>(fix32ToFloat(
          reinterpret_cast<TW_FIX32*>(pe->ItemList)[idx]) + 0.5f);
    } else {
      nv = static_cast<int>(reinterpret_cast<TW_UINT16*>(pe->ItemList)[idx]);
    }
  } else {
    GlobalUnlock(hc);
    return TWRC_FAILURE;
  }
  GlobalUnlock(hc);
  return setCurrentValue(e->cap_id, nv) ? TWRC_SUCCESS : TWRC_FAILURE;
}
