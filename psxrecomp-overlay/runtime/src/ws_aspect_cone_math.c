#include "ws_aspect_cone_math.h"

#include <math.h>

int32_t psx_ws_widen_angle_units(int32_t vanilla, uint32_t full_turn,
                                 int extent_pixels) {
    if (extent_pixels <= 0 || vanilla == 0) return vanilla;
    if (full_turn < 4u || (full_turn & 3u) != 0u) return vanilla;

    const int sign = vanilla < 0 ? -1 : 1;
    int64_t magnitude = vanilla;
    if (magnitude < 0) magnitude = -magnitude;
    const int64_t maximum = (int64_t)full_turn / 4 - 1;
    if (magnitude > maximum) magnitude = maximum;

    const double tau = 6.283185307179586476925286766559;
    const double angle = (double)magnitude * tau / (double)full_turn;
    const double scale = (160.0 + (double)extent_pixels) / 160.0;
    const double widened = atan(tan(angle) * scale);
    long result = lround(widened * (double)full_turn / tau);
    if (result < 1) result = 1;
    if ((int64_t)result > maximum) result = (long)maximum;
    return (int32_t)(sign * result);
}

uint32_t psx_ws_widen_angle_q12(uint32_t vanilla, int extent_pixels) {
    if (vanilla >= 1024u) vanilla = 1023u;
    return (uint32_t)psx_ws_widen_angle_units(
        (int32_t)vanilla, 4096u, extent_pixels);
}

int32_t psx_ws_scale_horizontal_320(int32_t value, int extent_pixels) {
    if (extent_pixels <= 0 || value == 0) return value;
    const int64_t denominator = 160 + (int64_t)extent_pixels;
    int64_t numerator = (int64_t)value * 160;
    numerator += numerator >= 0 ? denominator / 2 : -denominator / 2;
    return (int32_t)(numerator / denominator);
}

int psx_ws_aspect_cone_contains(int32_t x, int32_t z, int32_t y,
                                int32_t fx, int32_t fz, int32_t fy,
                                uint32_t threshold, int extent_pixels) {
    if (threshold == 0 || threshold >= 1024 || extent_pixels < 0)
        return 0;

    const double fhd = hypot((double)fx, (double)fz);
    if (fhd < 1.0) return 0;

    /* Camera-local coordinates. The Q12 scale cancels from the comparisons.
     * The right vector is horizontal in world space; the derived up vector is
     * perpendicular to both right and the pitched camera-forward vector. */
    const double forward =
        ((double)fx * x + (double)fz * z + (double)fy * y) / 4096.0;
    if (forward <= 0.0) return 0;
    const double horizontal =
        ((double)fz * x - (double)fx * z) / fhd;
    const double vertical_num =
        (double)fx * fy * x -
        fhd * fhd * y +
        (double)fz * fy * z;
    const double vertical = vertical_num / (fhd * 4096.0);

    const double cosine = (double)threshold / 1024.0;
    const double tangent =
        sqrt((1.0 - cosine * cosine) / (cosine * cosine));
    if (fabs(vertical) > forward * tangent) return 0;

    const double aspect_scale = (160.0 + extent_pixels) / 160.0;
    return fabs(horizontal) <= forward * tangent * aspect_scale;
}
