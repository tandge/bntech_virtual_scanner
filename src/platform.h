// Global Windows platform configuration.  Force-included via /FI (MSVC)
// or -include (GCC) to ensure consistency across all translation units.

#ifndef PLATFORM_H_
#define PLATFORM_H_
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // Reduces windows.h footprint.
#endif
#ifndef NOMINMAX
#define NOMINMAX  // Prevents min/max macro pollution from Windows SDK.
#endif
#include <winsock2.h>
#include <windows.h>
#endif  
