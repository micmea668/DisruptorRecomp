/*
 * Disruptor's weapon list in widescreen (SLUS-00224), built by
 * 0x8003D6C0..0x8003DFA0, and its psionics list in the opposite corner.
 *
 * The list is one panel of a dozen primitives from column 18 to 126: three
 * polygons of its frame, the frame's caps, an icon and three ammunition
 * digits per weapon, and a name. The widescreen HUD squash picks a pivot per
 * primitive by thirds of the screen, and the panel crosses the first boundary
 * at column 107. Its right cap and last digit went to the centre and the rest
 * to the left edge, while the polygons were not squashed at all.
 *
 * The game links its HUD sprites to the node just below the ordering table
 * of the world, so whatever that layer holds inside the panel's box is the
 * panel and takes the panel's pivot. The name is text: the game prints it
 * afterwards as a list of its own, from a node on its stack. There are two
 * tables, one per frame buffer, and the word below names either.
 */

#include "gpu.h"
#include "mod_plugins.h"

#include <cstdint>

namespace {

constexpr uint32_t kTablePointer = 0x80071490u;  /* gp + 0x344 */
constexpr int kHudLayers = 1;
/* x0, y0, x1, y1, pivot x, pivot y: the weapon list, then the psionics list. Left out: the ammunition box from column 12, the psionic charge from row 170. */
constexpr int32_t kPanels[2][6] = {{16, 10, 128, 196, 0, 0}, {232, 10, 298, 170, 320, 0}};
constexpr int kTextStrip = 128;  /* a printed line is one picture this wide: NO of the pause menu begins at column 169 and reaches 296 */

PSX_MOD_CONSTRUCTOR(register_disruptor_hud_widgets) {
    gpu_ws_set_hud_widgets(kTablePointer, kHudLayers, kPanels[0], 2);
    gpu_ws_set_hud_text_strip(kTextStrip);
}

}  // namespace
