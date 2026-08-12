# Bring-up and modernisation status

| Milestone | Status | Evidence / blocker |
| --- | --- | --- |
| Identify exact executable and disc | Complete | SLUS-00224 hashes and Mode 2 layout recorded in DISC.md |
| Translate resident executable | Complete | 579 seeded functions plus audited generated continuations |
| Boot native runtime through OpenBIOS | Complete | Retail boot and game-entry paths execute |
| Capture and execute runtime overlays | Complete for tested path | Private captured overlay set covers the validated first-level route |
| Menus, intro, and FMV | Complete for tested path | Native Windows user tests reach gameplay reliably |
| First mission | Complete | User played through the first level |
| Stable frame pacing and audio | Complete for tested path | Performance Test 7 held 60 FPS throughout and audio was clean |
| 4x geometry rendering | Complete for tested path | 120 steady samples: 59.52–60.37 Hz, mean 59.94 Hz |
| Horizontal mouse integration | Complete for tested path | User reports generally smooth mouse aim; native one-byte yaw quantization remains visible on the smallest motions |
| Presentation-only precise geometry | Accepted for the tested path; minor edge residual | The scratchpad continuation almost completely removed the ramp gaps. The ongoing retest reached 4,416,280/4,775,680 accepted candidates (92.474%), with 96.045% in a stable interval. An occasional one-pixel screen-edge sliver remains and is accepted for now; canonical VRAM remains unchanged |
| Presentation-only perspective textures | Accepted for the tested path; mild localized residual | User reports textures are definitely better. The run applied perspective correction to 3,509,462 triangles (92.496% of world triangles); remaining provenance fallbacks stay affine. Canonical VRAM, HUD and sprites remain unchanged |
| Modern keyboard/mouse actions | Complete for tested path | Test 4 confirmed the first-launch keybind-order fix; modern keys no longer overlap the legacy preset |
| In-game settings/dev menu | Implemented and live-validated with persistence | User testing passed menu input blocking, mouse-capture release/restore, live geometry/texture toggles, sensitivity, interpolation activation and settings restoration across launches. Reviewed preferences merge into executable-adjacent `settings.toml` through atomic replacement; explicit launch flags retain precedence and interpolation activation remains session-only. The 4:3 exact-geometry surface provisioning and framebuffer-clear regressions are fixed and live-validated. Windows x64 build and all 12 CTests pass. Exact-geometry 0%/live-coverage alternation is deferred as [GitHub issue #1](https://github.com/micmea668/DisruptorRecomp/issues/1) |
| Vertical mouse look | Not started | Original game has no pitch state; requires camera and aiming extension |
| 16:9 projection and protected HUD | Complete for tested path | Test 9 user result confirms stable rotation and correct geometry at both widescreen edges |
| Saves and later-level coverage | In progress | Memory-card behavior and full campaign regression remain unverified |
| Source repository checkpoint | Complete | Initial source-only Git checkpoint excludes disc data, generated retail translations, captures, binaries, and captured overlays |

## Latest evidence

Modernisation Test 11 proved the separate presentation surface itself worked,
but its rounded packed-SXY fallback was not a valid identity for projected
vertices. The user's exact A/B result showed fractures in geometry-only mode
and additional stretching with fractional yaw. Telemetry recorded 229,316
accepted triangles out of 915,279 candidates in the geometry-only run, while
the Test 9 baseline remained clean. Adjacent primitives were therefore mixing
unrelated or incomplete sidecar coordinates rather than exposing an error in
the authoritative renderer.

Modernisation Test 12 removed the ambiguous lookup entirely. A vertex was
eligible only when its GP0 packet address and packed value matched the exact RAM
address at which an SWC2 stored that GTE projection. All vertices had to match,
and each GP0 quad was preflighted as a four-vertex unit before either raster
triangle received correction. Fractional yaw used the same exact verdict, so
CPU-built 2D polygons and incomplete world packets remained fully canonical.

The live Test 12 image had none of the Test 11 fractures or stretched facets,
but a later audit of its logs changed the interpretation: every corrected mode
reported `geometry_precise=0`. Exact geometry world-triangle totals reached
3,348,701, exact-camera totals reached 353,481, and full/adaptive totals reached
2,954,311, yet none was accepted as precise. The image stayed safe because the
all-or-nothing gate always chose canonical geometry. Test 12 therefore did not
test whether retained precision reduces wobble.

Modernisation Test 13 Phase A is implemented locally as the next controlled
experiment. It validates complete GP0 source addresses before RAM mirroring,
rejects saturated or non-subpixel retained coordinates, gives canonical and
presentation positions separate OpenGL attributes, and optionally applies the
full X/Y yaw homography around the active drawing-buffer horizon. Versioned
diagnostics break coverage down by failure reason, polygon type, opcode, depth,
screen region and subpixel magnitude; an independent mirror-only tint exposes
accepted polygons directly. Rectangle fast paths remain canonical but now
appear explicitly in the denominator.

The first integrated Phase A Windows run recorded 2,934,528 candidate vertices,
zero accepted vertices and 2,913,892 store misses. A resident-code audit found
why: Disruptor's two projection paths use MFC2 SXY2 followed by ordinary SW, and
the executable contains no SWC2. The current build now snapshots the precise
projection at two reviewed MFC2 instructions and publishes it from seven
reviewed post-write SW hooks only for `SLUS-00224`. Each hook pins the PC and
raw instruction, requires the RAM word to have committed, and requires the
written value still to match the captured SXY2. This accepts the six unchanged
routes while keeping the potentially clipped/repacked route fail-closed when
it differs. It is deliberately a bounded audit of these resident paths rather
than a general proof of arbitrary MIPS register data flow.

The next gameplay run proved that the source-store route executes and accepts
provenance. At frame 6000 it recorded 3,180,703 registered-store attempts,
3,089,210 accepts and 48,527 packed-value rejections. The later consumer still
accepted zero of 2,252,468 candidate vertices, with 2,246,520 store misses, and
the user reported no visible difference. This is evidence that the first
bridge reaches the game's intermediate data, not evidence that precise
geometry reaches the renderer or reduces wobble.

Static tracing located the remaining copy. The two MFC2 paths and seven source
stores populate a 12-byte projected-vertex buffer. Four exact LWs at
`0x80046c4c`, `0x80046c50`, `0x80046c54`, and `0x80046c60` read its SXY words;
four SWs at `0x80046d04`, `0x80046d08`, `0x80046d10`, and `0x80046d0c` place
them into the GP0 `0x3c` packet. The first copy bridge is bounded to those
reviewed PCs and raw opcodes and publishes propagated provenance only after a
matching destination word commits. It does not use packed-coordinate equality
as a fallback and does not attempt general-purpose GPR lineage or taint
tracking.

The next live run proved that bridge reached the renderer. At frame 9660 the
cumulative totals were 3,644,820 candidate vertices and 3,133,256 accepted
(85.965%). The user reported that geometry wobble looked improved or absent;
texture warping remained, as expected, because texture interpolation is a
separate feature. The ramp screenshot also exposed thin seams. In the stable
view, 117 GP0 `0x3c` quads were corrected while 69 whole quads fell back to
canonical coordinates, with no partial-quad acceptance.

The remaining counters identify a second path exactly. Each stable update lost
nine registered source stores per scratch input, matching the loop that writes
`0x1f800084..0x1f8000c4` at eight-byte stride. Five packet constructors then
use 20 exact LW/SW pairs. The current build accepts only those nine aligned
scratch slots after a real scratchpad commit, keeps main-RAM and scratchpad
tokens in separate domains, and propagates them through the 20 reviewed pairs.
Together with the four original pairs, the route table contains 24 finite
copies. This build passes the generated-code audit, the expanded fail-closed
state-machine test, and all nine root CTests. The same-ramp retest almost
completely removed the gaps. At frame 13,740 the ongoing run had accepted
4,416,280 of 4,775,680 candidates (92.474%); one stable 60-frame interval
accepted 20,400 of 21,240 (96.045%). The user accepts the fleeting one-pixel
screen-edge sliver as a minor residual rather than broadening raster geometry.

The integrated Windows x64 build, all shaders and all nine root CTests pass,
including main-RAM/scratchpad commit-domain tests, all nine scratch slots, all
24 one-shot packet-copy routes, replay/speculation/timeline rejection, and
exact generated hook counts. Live validation of the scratchpad continuation is
accepted with the minor residual above; the formal private five-mode comparison
remains pending.

The next experiment adds perspective-correct texture interpolation without
changing polygon positions or canonical GPU output. It is independently gated
by `PSX_TEXTURE_CORRECTION`, requires exact geometry provenance plus a stricter
valid-depth proof, and preflights all four vertices before either half of a
textured quad is enabled. The OpenGL presentation mirror interpolates `u/z`,
`v/z`, and `1/z`; canonical VRAM, UI and sprite paths remain affine. The new
Windows x64 executable and all ten root CTests pass. In the live same-view A/B,
the user reported that perspective textures were definitely better, with only
mild wobble in certain areas. All 3,509,462 exact-geometry triangles also
received valid perspective depth, covering 92.496% of world triangles; the
dominant residual was the deliberately affine store-miss path. Fractional-camera
yaw is kept affine as a whole-polygon fallback until its additional depth
denominator can be preflighted across every quad vertex.

The geometry-provenance contract audit, modified GPU C99 compile, canonical GTE
tests, five mouse/registration configurations, presentation-yaw math test and
all twelve embedded OpenGL shader parses pass. Private regeneration audits 579
resident functions, 12,797 dispatch addresses and ten captured-overlay entry
identities with no unsupported instructions or unresolved targets. The static
Windows x64 GUI executable imports only Windows system libraries.

Performance Test 7 was the decisive runtime fix: the user reported a solid
60 FPS throughout with clean audio. Modernisation Test 1 then retained that
behavior at 4x internal geometry resolution. Ignoring its three startup and
external-pause intervals, the 4x run recorded 120 one-second cadence samples
from 59.52 to 60.37 Hz, averaging 59.94 Hz, with no audio underruns or
overflows.

The same test's guided control capture isolated 0x80077624. During held-left
input it changed on 118 samples, with 116 positive deltas; during held-right it
changed on 119 samples, with 118 negative deltas; neutral movement was minimal.
Static tracing then confirmed that this byte is copied with the three player
coordinates, feeds the game's sine/cosine lookups, and receives the original
turn routine's signed controller delta.

The first mouse checkpoint therefore writes relative horizontal mouse motion
directly to this 8-bit wrapping yaw instead of pulsing digital turn buttons.
The user found it generally smooth, especially across larger motions. The
smallest turn remains visibly stepped because one yaw unit is 1.40625 degrees;
interpolated rendering is a later, separate improvement.

The next control pass maps Disruptor's real digital-pad actions to WASD,
keyboard actions, and captured mouse buttons while preserving a connected
controller's sampled state. Modernisation Test 3 exposed a first-launch order
bug: when input.ini did not yet exist, the runtime returned immediately after
creating it and never loaded the packaged keybinds.ini, leaving the compiled
legacy keys active for that session. Keyboard bindings now initialize before
that branch. A clean executable-directory test confirms the modern preset is
loaded while input.ini is created, and the existing mocked SDL/SIO tests still
cover mapping, controller merging, capture gating, configuration, direction,
fractional accumulation, wrap, inversion, safety clamps, and release behavior.

Modernisation Test 3 proved the host native-wide compositor could expose a
16:9 surface, but Disruptor's own MIPS portal renderer remained [0,320], leaving
the revealed sides empty. Tests 4-6 attempted to widen that renderer's branches,
clamps and packed spans to [-53,373]. Those builds showed severe static-world
disappearance while doors remained visible.

Modernisation Test 4 exposed severe centre-screen geometry popping after the
manual portal-window pass. Test 5 corrected a genuine signedness defect in the
packed span handoff: the left coordinate occupies the upper half of a packed
word, and its wall-renderer consumer at 0x80041184 must use LH once native-wide
can make that value negative. The user's Test 5 result showed no visual
improvement, proving that this was necessary but not sufficient.

The Test 6 rotation result identifies the architectural problem: portal spans
are selected from camera yaw, and Disruptor's static-world decompressor assumes
unsigned 0..320 clip coordinates throughout. Moving those spans negative can
change or corrupt the selected world while movement alone preserves the bad
state. Doors use a separate object/GTE path, matching the observed exception.

Test 7 therefore removes every native-wide portal mutation and restores the
retail unsigned load at 0x80041184. It uses the classic widescreen path instead:
the GTE squashes horizontal projection by 3/4, and nine exact CPU projection
sites do the same for portal bounds, per-room billboards and point effects.
All guest portal/outcode limits remain original and the final 320x240 image is
stretched to 16:9, restoring proportions. HUD sprites are pre-corrected; BIOS,
movies and full-screen 2D scenes retain 4:3 presentation. Generated-code audits
enforce all nine projection hooks and the restored portal invariants.

The Windows Test 7 result validates that replacement: the user completed a
full 360-degree turn and moved without turning; previously broken camera
angles no longer lost or froze the static world. The remaining empty edge
strips are therefore upstream participation/culling, not downstream portal
corruption. Because the source-only checkpoint intentionally excludes the
retail executable image, Modernisation Test 8 keeps the byte-identical Test 7
runtime and privately captures the verified resident range
0x80011200-0x80056938 from the user's own disc. That image is the input for an
exact, guarded aspect-cone patch in the next visual build; it must never be
published or committed.

The first Test 8 results showed that its live-memory transport could not work:
the stable Test 7 executable is a Release build with `PSX_DEBUG_TOOLS=OFF`, so
`debug_server_init` is compiled out even though the unused command strings are
still linked. The capture helper correctly timed out without writing code.
Test 8B replaces that transport with a read-only MODE2/2352 extractor. It
validates the full BIN identity, parses the ISO9660 root, reconstructs
`SLUS_002.24;1`, validates its complete PS-X EXE identity/header, and writes
only 0x80011200-0x80056938. Synthetic raw-sector tests verify the full extent
and slice calculation. No additional gameplay run is required.

The corrected Test 8B result supplied the verified 0x45738-byte resident image.
Static tracing identifies camera yaw +/-32 in `func_80040E68` as the two rays
that seed portal traversal. Test 9 replaces only those guarded ADDIU results
with aspect-derived offsets (about +/-38 at 16:9). Two object funnels repeat
the horizontal side-plane test, so four guarded signed SLTs receive the matching
horizontal scale; adjacent vertical tests remain retail-exact.

All guest portal/outcode limits remain original, the nine GTE-matching CPU
projection hooks remain active, and the final 320x240 image is stretched to
16:9 with a pre-corrected HUD. A runtime A/B can use the yaw rays alone if the
later object funnel proves too broad. Generated-code audits enforce exactly two
yaw helpers, four horizontal side-plane helpers, nine projection hooks, and
the restored portal invariants. Test 9 remains the visual baseline: Test 12's
canonical fallback removed Test 11's regressions, but its zero precise coverage
means it supplied no evidence about wobble reduction.

## Framework note

The pinned PSXRecomp base revision is recorded in PSXRECOMP_PIN. This project
also depends on the reviewed `psxrecomp-overlay/` files containing the validated
packaged-execution/performance path plus the CPU-projection widescreen hook.
The build scripts apply those files automatically after checking out the pin.
An unrelated local edit to
recompiler/seeds/openbios_elf_seeds.json is intentionally excluded.
