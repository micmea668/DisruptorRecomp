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
| Presentation-only precise geometry | Stable experiment; no visible wobble benefit | Test 12 removed Test 11 fractures and stretching, but neither exact geometry nor fractional camera produced a noticeable improvement over Test 9 |
| Modern keyboard/mouse actions | Complete for tested path | Test 4 confirmed the first-launch keybind-order fix; modern keys no longer overlap the legacy preset |
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

Modernisation Test 12 removes the ambiguous lookup entirely. A vertex is now
eligible only when its GP0 packet address and packed value match the exact RAM
address at which an SWC2 stored that GTE projection. All vertices must match,
and each GP0 quad is preflighted as a four-vertex unit before either raster
triangle receives correction. Fractional yaw uses the same exact verdict, so
CPU-built 2D polygons and incomplete world packets remain fully canonical.

The live Test 12 comparison confirms that the Test 11 fractures and stretched
facets are gone. Neither exact geometry alone nor the fractional-camera mode
looked noticeably better for wobble than the Test 9 baseline. Exact provenance
therefore makes the optional presentation path safe, but the current experiment
does not justify presenting it as a visible modernisation improvement.

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
the restored portal invariants. Test 9 remains the visual baseline after the
later Test 12 experiment removed its regressions without reducing wobble.

## Framework note

The pinned PSXRecomp base revision is recorded in PSXRECOMP_PIN. This project
also depends on the reviewed `psxrecomp-overlay/` files containing the validated
packaged-execution/performance path plus the CPU-projection widescreen hook.
The build scripts apply those files automatically after checking out the pin.
An unrelated local edit to
recompiler/seeds/openbios_elf_seeds.json is intentionally excluded.
