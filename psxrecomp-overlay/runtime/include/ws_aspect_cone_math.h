#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Test whether a camera-relative vector is inside a rectangular view cone.
 *
 * Coordinates use Tomba 2's X,Z,Y ordering. The camera forward vector is Q12.
 * `threshold` is the game's Q10 cosine threshold (the SLTI immediate after its
 * normalized dot product). `extent_pixels` expands only the horizontal
 * half-angle from the vanilla 160-pixel half-width; the vertical half-angle is
 * unchanged.
 */
int psx_ws_aspect_cone_contains(int32_t x, int32_t z, int32_t y,
                                int32_t fx, int32_t fz, int32_t fy,
                                uint32_t threshold, int extent_pixels);

/* Widen a positive PSX 12-bit angular half-extent by the same horizontal
 * projection scale used by a 320-wide 4:3 viewport. `extent_pixels` is the
 * extra per-side reveal (including any configured guard). Identity is exact
 * when extent_pixels is zero. Results stay below a 90-degree half-angle. */
uint32_t psx_ws_widen_angle_q12(uint32_t vanilla, int extent_pixels);

/* Generic signed counterpart for games whose wrapping yaw uses a different
 * unit system. `full_turn` is the number of integer units in 360 degrees.
 * The sign is preserved and the magnitude is widened by the same 320-wide
 * projection geometry. */
int32_t psx_ws_widen_angle_units(int32_t vanilla, uint32_t full_turn,
                                 int extent_pixels);

/* Scale a signed camera-horizontal coordinate into the original 320-wide
 * projection before an unchanged side-plane comparison. Identity at zero
 * extent; symmetric nearest-integer rounding for negative values. */
int32_t psx_ws_scale_horizontal_320(int32_t value, int extent_pixels);

#ifdef __cplusplus
}
#endif
