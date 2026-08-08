# Modernisation Test 13 — Phase A geometry audit

This is a source-only experimental checkpoint. It does not contain a retail
disc, PS-X executable, generated translation, captured overlay, BIOS, Windows
executable, save, screenshot, or runtime log.

Phase A keeps Test 9 as the visual baseline and retains Test 12's independent
presentation surface. It does not change gameplay, collision, AI, culling,
saves, or the authoritative PS1 VRAM image.

## Purpose

Test 12 removed the fractures and stretched facets caused by Test 11's
ambiguous rounded-SXY lookup, but its archived telemetry later showed zero
accepted precise triangles in every corrected mode. The safe image came from
the canonical fallback, so Test 12 did not actually measure whether retained
precision reduces wobble. Phase A makes the next comparison diagnostically
useful before attempting a wider camera-space redesign.

The source changes:

- validate the complete GP0 source address before RAM normalization, so a
  direct write to GP0 MMIO at `0x1f801810`, an unmapped KSEG2/KSEG3 address,
  or an unaligned word source cannot alias a nearby RAM entry;
- accept GTE projection provenance only after the matching word write actually
  commits to main RAM, so cache-isolated, replayed, or deliberately suppressed
  writes cannot manufacture or invalidate provenance;
- cover both SWC2 and Disruptor's reviewed MFC2-SXY2-to-ordinary-SW routes. The
  latter are serial-gated to `SLUS-00224`: two audited MFC2 instructions
  snapshot the precise projection; six audited post-write SW hooks publish to
  main RAM and one publishes only to nine aligned scratchpad slots. Raw
  instruction words, the appropriate committed address domain, and the written
  packed coordinate are checked;
- preserve that published provenance across Disruptor's bounded packet-copy
  seam. Four audited LWs at `0x80046c4c`, `0x80046c50`, `0x80046c54`, and
  `0x80046c60` read SXY words from the 12-byte projected-vertex buffer; four
  audited SWs at `0x80046d04`, `0x80046d08`, `0x80046d10`, and `0x80046d0c`
  write them into a GP0 `0x3c` packet. A second finite continuation recognizes
  20 LW/SW pairs from the nine scratchpad slots into five packet variants.
  Propagation is pinned to raw instructions and a matching committed
  destination write, rather than inferred from a later packed-coordinate
  match;
- reject saturated projections and any retained 16.16 coordinate whose integer
  component differs from the packet's packed SXY;
- carry canonical GP0 coordinates and presentation-only precise coordinates in
  separate OpenGL vertex attributes;
- optionally apply the same yaw homography denominator to X and Y, with the Y
  horizon following the active drawing framebuffer;
- report exact-provenance coverage by rejection reason, primitive class,
  opcode, depth, raw screen region, and correction magnitude;
- count textured-quad rectangle fast paths explicitly in the coverage
  denominator; and
- optionally tint exact-accepted polygons magenta on the presentation mirror.

All new visual behavior is opt-in. `PSX_GEOMETRY_FULL_YAW` and
`PSX_GEOMETRY_COVERAGE_TINT` default to off. The tint never reaches canonical
VRAM.

## Stable diagnostic ordering

Each `geometry_diag version=1` line contains both named scalar fields and
comma-separated arrays. Array positions are part of the version-1 contract.

Rejection reasons, indices 0–14:

1. no command source;
2. source-address overflow;
3. tracking disabled;
4. speculative lookup;
5. source is not RAM;
6. source is not word-aligned;
7. store miss;
8. store collision;
9. invalid projection;
10. packed-value mismatch;
11. zero depth;
12. saturated projection;
13. precise integer does not match packed SXY;
14. an otherwise valid vertex discarded by the polygon's all-or-nothing gate;
    and
15. textured-quad rectangle fast path.

Primitive classes, indices 0–7, are flat triangle, textured flat triangle,
Gouraud triangle, textured Gouraud triangle, then the same four quad classes.
Opcode arrays cover `0x20` through `0x3f`. Depth buckets are `<256`, `<1024`,
`<4096`, `<16384`, `<32768`, and `>=32768`. Screen buckets are a row-major 3×3
grid followed by off-screen. Correction buckets are zero, `<0.25`, `<0.5`,
`<0.75`, and `<1` pixel using `max(abs(dx), abs(dy))`.

Every candidate vertex is either accepted or represented in the rejection
array. `partial_polygon_rejections` counts polygons in which at least one—but
not every—vertex resolved; `partial_quad_rejections` is its quad subset.
Correction sums use saturating arithmetic, and
`correction_accumulators_saturated=1` makes loss of moment precision explicit
instead of allowing an unsigned wrap.

The named scalar `provenance_store_uncommitted_rejections` counts eligible
RAM-address provenance callbacks whose immediately preceding 32-bit memory
write did not commit to main RAM. This exposes cache-isolated, suppressed, and
lockstep-replay stores without allowing any of them to manufacture provenance.
The commit proof is consume-once and is discarded before speculative or
tracking-disabled callbacks return.

Three additional scalars audit the reviewed ordinary-store path:
`provenance_registered_store_attempts` counts callbacks from a registered SW
instruction, including callbacks later rejected for lacking a matching commit;
`provenance_registered_store_accepts` counts captured MFC2 snapshots whose
packed SXY2 and committed destination were accepted into the cache; and
`provenance_registered_store_packed_rejections` counts altered or clipped
coordinates deliberately left on the canonical path.

This game-specific bridge is intentionally bounded. It checks the two source
instructions, six main-RAM stores, one nine-slot scratchpad store, and 24 copy
pairs in the supported resident image. It is not a
general GPR lineage or taint tracker for arbitrary self-modified code. Any raw
instruction mismatch fails generation or registration, and any missing
snapshot, write commit, address match, or packed-value match fails back to
canonical geometry.

The source-store bridge ends in intermediate projected-vertex buffers, not the
final GPU packet. Its bounded continuations recognize only the four main-RAM
pairs and 20 scratchpad pairs listed above. They carry an already-established
cache entry across a copy and publish it only after the matching packet word
commits. This is not a generic packed-SXY fallback and does not infer arbitrary
GPR data flow. Failure of a pinned opcode, source entry, copied value, or
destination commit leaves the packet canonical.

The `cumulative` scope records all candidates after the feature is enabled.
The `latest_live` scope records only the most recent frame eligible to present
the corrected gameplay surface, preventing menus and offscreen texture work
from being mistaken for live coverage.

## Private comparison procedure

Do not prepare a test package from an unidentified working tree. Once the
source is reviewed and intentionally committed, build privately from the
supported disc and captured overlay set. Put the resulting executable and its
runtime files beside `test13-launcher/Play Disruptor.ps1`.

Create `BUILD-INFO.txt` with the actual source revision:

```text
source_commit=<40 lowercase hexadecimal characters>
```

Create `CHECKSUMS.sha256` with the executable's SHA-256:

```text
<64 hexadecimal characters> *DisruptorRecompiled.exe
```

The launcher recalculates the hash and refuses to run if either identity file
is missing or invalid. It also requires the actual
`GL GPU pipeline ready (internal scale 4x` line and rejects the software
fallback.

Use one save, route, viewing direction, and mouse sensitivity. Enter live
gameplay, middle-click to capture the mouse, remain there for at least one
second, and make the same slow turns in this order:

1. Test 9 baseline;
2. exact geometry only;
3. current X-only fractional camera;
4. full X/Y fractional camera; and
5. exact-provenance coverage tint.

Frame interpolation is disabled in every mode. Compare wall/floor/ceiling
edges at the center and sides, doors, enemies, weapon geometry, smallest yaw
steps, cracks or stretching, performance, and audio. The tint run answers
which visible surfaces are actually eligible; it is not a proposed final
rendering style.

## Validation boundary

The source-level geometry-provenance, Phase A, and perspective-texture contracts
pass, the Test 13 PowerShell parses successfully, and the X/Y homography tests
cover identity, direction, horizon behavior, inverse transforms, and yaw-byte
continuity. The integrated Windows x64 build and all ten root CTests also pass.

The first live Phase A diagnostic run reached gameplay but accepted zero of
2,934,528 candidate vertices; 2,913,892 were store misses. That result exposed
Disruptor's MFC2-plus-ordinary-SW packet construction and led to the bounded
two-capture/seven-store route now in the current build. Generated-code auditing
confirms exactly those nine hooks.

A second gameplay run proved that route is active but also bounded its result.
At frame 6000 it recorded 3,180,703 registered-store attempts, 3,089,210
accepts, and 48,527 packed-value rejections. The GPU side still accepted zero
of 2,252,468 candidate vertices; 2,246,520 were store misses, and the user saw
no visual difference. The accepted source stores therefore did not establish
that precise geometry reached a GP0 consumer or reduced wobble.

Static tracing then identified the four-LW/four-SW copy seam between the
12-byte projected-vertex buffer and the final GP0 `0x3c` packet. The bounded
raw-opcode- and commit-gated propagation reached 3,133,256 of 3,644,820
candidate vertices (85.965%) in the next live run, and geometry looked steadier.
That run exposed thin ramp seams: a stable view contained 117 corrected GP0
`0x3c` quads beside 69 whole canonical quads.

The remaining source-store deficit was exactly divisible by the nine-iteration
scratchpad projection loop. Static tracing found the nine slots at
`0x1f800084..0x1f8000c4` and 20 exact LW/SW packet-copy pairs. The current build
adds a separate scratchpad commit domain and those 20 routes (24 copy routes in
total). Generated auditing, the expanded fail-closed test, Windows x64 linking,
and all nine root CTests pass. The same-ramp retest almost completely removed
the gaps. At frame 13,740 the ongoing run had accepted 4,416,280 of 4,775,680
candidates (92.474%); a stable 60-frame interval accepted 20,400 of 21,240
(96.045%). A fleeting one-pixel sliver can still appear at the screen edge, and
the user accepts that minor residual for now rather than broadening or welding
polygons. The original private captured static overlay is also not present, so
the formal five-mode comparison remains open.

Even if full X/Y yaw helps, retained precision still begins after the PS1's
fixed-point matrix work, IR saturation, reciprocal approximation, and Test 9
projection squash. Phase A does not recover camera-space precision, CPU-built
portal geometry, or exact per-projection `OFX`, `OFY`, and `H`.
