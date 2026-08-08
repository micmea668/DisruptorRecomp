# Modernisation Test 12 — exact geometry provenance

This is a source-only continuity checkpoint, not a standalone build or public
release. It excludes the retail disc and PS-X executable, private Test 8B
resident-code capture, generated translated game C, captured overlays, BIOS
binaries, Windows executable, builds, saves, screenshots and test logs.

Base PSXRecomp revision:
854a658372b304c222a0d0cbbae16c66264b785e

## Repository contents

* The repository root contains the source-only Disruptor project,
  configuration, guarded code-generation hooks, modern controls and
  deterministic tests.
* psxrecomp-overlay/ contains every modified framework file required on top of
  the pinned revision.
* test12-launcher/ contains the source-only Windows launch and result scripts.

Test 11 finding and Test 12 repair:

* Test 11's geometry fractures came from a fallback cache keyed only by packed,
  rounded SXY. Multiple unrelated GTE projections can share that integer pixel,
  so the lookup did not prove vertex identity.
* Test 12 removes that lookup from GPU presentation. A vertex is eligible only
  when its GP0 packet word and exact RAM source address match a recorded SWC2
  projection store.
* All vertices of a polygon must match. Each GP0 quad is preflighted as one
  four-vertex unit, and both raster triangles receive the same verdict.
* Fractional camera yaw is gated by that exact verdict. CPU-projected 2D art,
  untracked polygons and incomplete packets use the canonical integer renderer
  with no camera transform.
* The existing counters now measure all candidate triangles versus exact-only
  accepted triangles, allowing live coverage to be evaluated without accepting
  ambiguous data.

The original PSX path remains authoritative for VRAM, gameplay, collision, AI,
aiming and saves. The optional corrected image is still a separate presentation
surface, preserving the ability to expose affine texture mapping, integer
vertex wobble and frame blending as independent settings later.

## Live result

The Test 12 comparison removed every reported Test 11-style fracture and
stretched facet. A later audit of the archived telemetry changed the meaning of
that result: every corrected mode reported zero accepted precise geometry. The
canonical fallback produced the clean image, so the comparison did not exercise
the retained coordinates or exact-gated fractional camera. It validates removal
of Test 11's ambiguous rounded-SXY lookup as a fail-closed safety repair, but it
provides no evidence that retained precision reduces wobble.

Retained Test 9 architecture:

* The corrected upstream yaw cone and four object-side-plane tests widen world
  participation for 16:9 without changing the fragile 0..320 portal domain.
* Nine CPU-projection sites match the GTE's horizontal projection correction.
* HUD sprites are pre-corrected; BIOS, menus and FMVs stay pillarboxed at 4:3.
* Modern WASD/mouse controls merge with controller input, and mouse X writes
  directly to the verified horizontal yaw byte.

Validation completed for this source checkpoint:

* The geometry-provenance contract audit proves the ambiguous packed-SXY
  fallback is absent, yaw is provenance-gated, and all four quad handlers
  preflight as a unit. Archived live telemetry later established that this
  checkpoint's SWC2-only producer path had zero coverage in Disruptor.
* The modified GPU translation unit compiles cleanly as C99.
* Canonical GTE register/provenance tests pass.
* Mouse input, registration and fractional-yaw boundary tests pass in all five
  configurations.
* Presentation-yaw math tests pass.
* All twelve embedded OpenGL shaders pass a GLSL parser.
* Private regeneration audits 579 resident functions, 12,797 dispatch
  addresses and ten captured-overlay entry identities with no unsupported
  overlay instructions or unresolved targets.
* The private Windows release cross-compiles and links as a PE32+ x86-64 GUI
  executable with a 64 MiB stack and only Windows system DLL imports.

The build scripts apply psxrecomp-overlay/ to matching paths in the pinned
framework before building. A developer must supply the supported USA disc and
privately regenerate every excluded game-derived source. Never commit or
publish the private capture, disc data, generated translations, captured
overlays, BIOS binaries or private executable.
