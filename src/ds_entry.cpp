// DLL entry point for the TWAIN Data Source.
// DS_Entry is the single exported function invoked by the TWAIN DSM.
// Manages a list of active Data Source instances, one per application.

#include "twain_data_source.h"
#include <windows.h>
#include <list>
#include <memory>
#include <cstring>

// A single application connection: identity pair + Data Source instance.
struct DsInst {
  TW_IDENTITY app_id;
  std::unique_ptr<TwainDataSource> ds;
};

// Linked list of active DS instances (one per connected application).
static std::list<DsInst> g_ds_list;

// DLL module handle, set during DLL_PROCESS_ATTACH.
HINSTANCE g_hinstance = nullptr;

extern "C" TW_UINT16 FAR PASCAL DS_Entry(
    pTW_IDENTITY pOrigin,
    TW_UINT32    dg,
    TW_UINT16    dat,
    TW_UINT16    msg,
    TW_MEMREF    pData) {
  TwainDataSource* ds = nullptr;
  if (pOrigin != nullptr) {
    for (auto& inst : g_ds_list) {
      if (inst.app_id.Id == pOrigin->Id) {
        ds = inst.ds.get();
        break;
      }
    }
  }
  if (ds == nullptr) {
    if (dg == DG_CONTROL && dat == DAT_IDENTITY && msg == MSG_GET) {
      TwainDataSource::s_the_identity.Id =
          reinterpret_cast<pTW_IDENTITY>(pData)->Id;
      std::memcpy(pData, &TwainDataSource::s_the_identity,
                  sizeof(TwainDataSource::s_the_identity));
      return TWRC_SUCCESS;
    }
    if (dg == DG_CONTROL && dat == DAT_IDENTITY && msg == MSG_CLOSEDS) {
      return TWRC_SUCCESS;
    }
    auto new_ds = std::make_unique<TwainDataSource>(*pOrigin);
    if (new_ds == nullptr || TWRC_SUCCESS != new_ds->initialize()) {
      return TWRC_FAILURE;
    }
    DsInst inst;
    inst.app_id = *pOrigin;
    inst.ds = std::move(new_ds);
    g_ds_list.push_back(std::move(inst));
    ds = g_ds_list.back().ds.get();
  }
  TW_INT16 result = ds->dsEntry(pOrigin, dg, dat, msg, pData);
  if (result == TWRC_SUCCESS &&
      dg == DG_CONTROL && dat == DAT_IDENTITY && msg == MSG_CLOSEDS &&
      ds != nullptr) {
    for (auto it = g_ds_list.begin(); it != g_ds_list.end(); ++it) {
      if (it->app_id.Id == pOrigin->Id) {
        g_ds_list.erase(it);  // unique_ptr auto-deletes TwainDataSource.
        break;
      }
    }
  }
  return result;
}
BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID) {
  switch (reason) {
    case DLL_PROCESS_ATTACH:
      g_hinstance = hModule;
      VirtualScanner::g_hinstance = hModule;
      break;
    case DLL_PROCESS_DETACH:
      g_hinstance = nullptr;
      break;
    default:
      break;
  }
  return TRUE;
}