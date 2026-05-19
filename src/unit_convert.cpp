// Unit conversion utilities for TWAIN fixed-point (FIX32) values.
// All cross-unit conversions go through inches as the canonical
// intermediate unit to minimize rounding error accumulation.

#include "unit_convert.h"
#include <cmath>

// Converts a value between TWAIN unit systems.  First converts the source
// value to inches (the canonical intermediate), then from inches to the
// target unit system.  Pixel conversions require a DPI resolution value.
float convertUnits(float value, int from_units, int to_units, float resolution) {
    if (from_units == to_units) {
        return value;
    }
    // Step 1: source units → inches.
    double inches = 0.0;
    switch (from_units) {
        case TWUN_INCHES:
            inches = value;
            break;
        case TWUN_CENTIMETERS:
            inches = value / 2.54;
            break;
        case TWUN_PICAS:
            inches = value / 6.0;
            break;
        case TWUN_POINTS:
            inches = value / 72.0;
            break;
        case TWUN_TWIPS:
            inches = value / 1440.0;
            break;
        case TWUN_PIXELS:
            if (resolution != 0.0f) {
                inches = value / resolution;
            }
            break;
        default:
            inches = value;
            break;
    }
    double result = inches;
    switch (to_units) {
        case TWUN_INCHES:
            break;
        case TWUN_CENTIMETERS:
            result = inches * 2.54;
            break;
        case TWUN_PICAS:
            result = inches * 6.0;
            break;
        case TWUN_POINTS:
            result = inches * 72.0;
            break;
        case TWUN_TWIPS:
            result = inches * 1440.0;
            break;
        case TWUN_PIXELS:
            result = inches * resolution;
            break;
        default:
            break;
    }
    return static_cast<float>(result);
}
TW_FIX32 convertUnitsFix32(TW_FIX32 value, int from_units, int to_units, float resolution) {
    if (from_units == to_units) {
        return value;
    }
    return floatToFix32(convertUnits(fix32ToFloat(value), from_units, to_units, resolution));
}
TW_FRAME convertUnitsFrame(TW_FRAME value, int from_units, int to_units,
                           float x_resolution, float y_resolution) {
    TW_FRAME result = value;
    if (from_units != to_units) {
        result.Left = convertUnitsFix32(value.Left, from_units, to_units, x_resolution);
        result.Top = convertUnitsFix32(value.Top, from_units, to_units, y_resolution);
        result.Right = convertUnitsFix32(value.Right, from_units, to_units, x_resolution);
        result.Bottom = convertUnitsFix32(value.Bottom, from_units, to_units, y_resolution);
    }
    return result;
}
// Converts a float to TWAIN's FIX32 format: Whole contains the integer
// part, Frac contains the fractional part scaled to 0..65535.
TW_FIX32 floatToFix32(float value) {
    TW_FIX32 result;
    result.Whole = static_cast<TW_INT16>(value >= 0 ? std::floor(value) : std::ceil(value));
    result.Frac = static_cast<TW_UINT16>(
        std::abs(static_cast<double>(value) - result.Whole) * 65536.0 + 0.5);
    return result;
}
// Converts a TWAIN FIX32 back to float: Whole + Frac / 65536.
// Handles negative Whole values by negating the fractional portion.
float fix32ToFloat(TW_FIX32 value) {
    float frac = static_cast<float>(value.Frac) / 65536.0f;
    if (value.Whole < 0) {
        frac = -frac;
    }
    return static_cast<float>(value.Whole) + frac;
}
