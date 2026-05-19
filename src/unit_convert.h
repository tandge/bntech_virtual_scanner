// TWAIN unit conversion and FIX32 arithmetic utilities.

#ifndef UNIT_CONVERT_H_
#define UNIT_CONVERT_H_
#include "twain.h"

// Converts a float value between TWAIN unit systems (inches, cm, pixels, etc.).
float convertUnits(float value, int from_units, int to_units, float resolution);

// TW_FIX32 wrapper for convertUnits.
TW_FIX32 convertUnitsFix32(TW_FIX32 value, int from_units, int to_units, float resolution);

// Converts all four corners of a TW_FRAME between unit systems.
TW_FRAME convertUnitsFrame(TW_FRAME value, int from_units, int to_units,
                           float x_resolution, float y_resolution);

// Converts a float to TWAIN's TW_FIX32 fixed-point format (whole + frac/65536).
TW_FIX32 floatToFix32(float value);

// Converts a TW_FIX32 back to float.
float fix32ToFloat(TW_FIX32 value);

#endif  
