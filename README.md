# DisruptorRecomp

A native PC static recompilation project for **Disruptor (USA, SLUS-00224)**,
built on [PSXRecomp](https://github.com/mstan/psxrecomp).

The original PlayStation MIPS code is translated ahead of time to C locally and
compiled into a native x64 executable. PSX hardware services are supplied by a
purpose-built compatibility runtime, with the redistributable PCSX-Redux
OpenBIOS backend. It is therefore a native static recompilation—not an emulator
wrapper—and it is not a source-code decompilation or conventional source port.
It is not yet a public release.

No proprietary PlayStation BIOS is bundled or required.

## Current result

- The supported disc boots through OpenBIOS into natively translated resident
  game code and captured runtime overlays.
- Menus, intros, FMVs, audio, input, and the complete first level have been
  exercised on native Windows.
- The corrected packaged execution path holds approximately 59.94 updates per
  second through menus, movies, and gameplay, with clean audio in the latest
  user validation.
- OpenGL geometry supersampling renders at 4x internal scale (2560x1920 at
  authentic 4:3). A Windows test supplied 120 steady-state one-second samples
  from 59.52 to 60.37 Hz, averaging 59.94 Hz.
- Opt-in modern controls combine direct horizontal mouse turning with WASD,
  mouse fire/psionic buttons, and conventional keyboard action bindings.
- An experimental projection-and-stretch 16:9 mode now widens Disruptor's
  upstream yaw frustum while preserving its original portal-coordinate domain.
  The HUD is corrected, and menus and movies remain pillarboxed.
- Modernisation Test 12 adds an opt-in presentation surface whose corrected
  vertices must match the exact RAM addresses of their GTE projection stores.
  Whole quads and fractional camera yaw share the same all-or-nothing gate;
  the ambiguous rounded-screen-coordinate lookup from Test 11 is gone.
- Live Test 12 comparison confirms that the Test 11 fractures and stretched
  facets are gone. The exact-geometry and fractional-camera modes did not,
  however, produce a noticeable reduction in wobble over the Test 9 baseline.
- Linux and Windows x64 builds compile successfully against the pinned
  framework plus the documented local runtime overlay.

See [STATUS.md](STATUS.md) for the evidence and open work,
[DISC.md](DISC.md) for supported binary identities, and
[docs/MODERNISATION-TEST-12.md](docs/MODERNISATION-TEST-12.md) for the latest
presentation experiment and verdict.

## Repository layout

- `src/`, `tools/`, `tests/`, and the root-level TOML files contain
  project-authored source, generation tools, deterministic tests, and guarded
  patch configuration.
- `psxrecomp-overlay/` is the reviewed framework overlay applied automatically
  on top of the revision in `PSXRECOMP_PIN`; the explicit
  `PSXRECOMP_OVERLAY_FILES.txt` manifest prevents local artifacts from being
  copied into the framework.
- `test12-launcher/` preserves the source-only launch and collection scripts
  used for the Test 12 comparison.
- `input/`, `generated/`, `psxrecomp/`, and build directories are local-only
  and excluded from Git.

## Files you must supply

Copy these from your own Disruptor disc into input/:

1. SLUS_002.24 — SHA-256 must be
   48e8c3143b7f5de10340c9d4a9bac8cb7e97c15eda7a0897d3cf337ad96cb2c4.
2. The original Mode 2 .bin track and its .cue file. The default expected cue
   filename is Disruptor (USA).cue.

The verified BIN is 636,350,064 bytes with SHA-256
3b49f9874e30c613ca9d17720716764cd76d0ac968c0acd0f53159366c0cf3a4.
See DISC.md for the CUE and transfer-archive hashes.

Do not convert the disc to a cooked 2048-byte ISO: PlayStation streaming audio
and video depend on the original Mode 2 sector layout.

## Windows build

Install Git, CMake 3.20+, Python 3, and either Visual Studio 2022 with Desktop
development with C++ or an MSYS2 MinGW-w64 toolchain. Ninja is recommended.

From PowerShell:

    powershell -ExecutionPolicy Bypass -File .\build.ps1
    .\run.ps1

The build script verifies the supplied executable, checks out the pinned
framework, applies the reviewed framework overlay, generates the private
translated code and OpenBIOS sources, audits the result, and builds
DisruptorRecompiled.exe.

The validated private first-level path also uses a captured static overlay.
When a private regeneration has produced `generated/overlays_static.c`, CMake
detects and links it automatically. That retail-derived file is deliberately
absent from this repository; a checkout without it is not equivalent to the
validated private package.

Modern controls and 16:9 rendering are opt-in:

    .\run.ps1 -ModernControls -Widescreen

Enter gameplay, middle-click to capture, move left/right to turn, and
middle-click or press Esc to release. Edit mouse-aim.ini to adjust sensitivity
or invert the horizontal direction. The features can also be tested separately
with `-ModernControls`, `-MouseAim`, or `-Widescreen`.

## Linux development build

With GCC, CMake, Python and optionally Ninja installed:

    chmod +x build.sh run.sh tools/regen.sh
    ./build.sh
    ./run.sh

Use `./run.sh --modern-controls --widescreen` for the combined opt-in test.
Linux is primarily a compiler and diagnostic target; Windows remains the
release target.

## Mouse implementation

Dynamic left/neutral/right correlation and static instruction tracing identify
0x80077624 as Disruptor's 8-bit wrapping player/camera yaw. The game copies it
with player coordinates, indexes sine/cosine tables from it, and adds its normal
controller-derived turn delta directly to it.

src/disruptor_mouse_aim.cpp converts relative host mouse X movement into that
same yaw. It preserves fractional motion and lets the original camera,
collision, movement, and renderer consume the result. The same opt-in hook can
merge modern keyboard and mouse actions into the emulated digital pad after
normal controller sampling, so a connected controller continues to work.

The modern layout is W/S forward/back, A/D strafe, Space jump, E use, F
psionic, Q/R weapon/psionic selection, Tab map, P pause, left mouse fire, and
right mouse psionic. Arrow keys and Enter remain menu fallbacks. Mouse actions
only reach the game while the mouse is captured.

The current module is horizontal only. Vertical look needs a separate
game-specific camera and weapon-aim extension because the original game has no
pitch state. Menu pointer support is also deferred.

## Widescreen implementation

game-widescreen.toml selects the classic projection-and-stretch 16:9 path.
Disruptor's static world uses a yaw-dependent CPU portal projection in addition
to ordinary GTE geometry. The corrected private Test 8B capture identified the
upstream visibility seeds at 0x80040FE8 and 0x80040FF0: wrapping eight-bit
camera-yaw rays at +/-32 units (a 45-degree 4:3 half-field). At 16:9 those
offsets widen geometrically to about +/-38 units. Four signed horizontal
side-plane comparisons at 0x8003B8EC, 0x8003B8F8, 0x8003BD00, and 0x8003BD0C
receive the matching aspect adjustment; their vertical counterparts remain
unchanged.

Nine exact final screen-X sites still receive the same 3/4 projection squash
as the GTE, and the resulting 320x240 gameplay image is presented at 16:9.
Every downstream portal span, clamp, outcode, packed coordinate, and wall load
remains in the original unsigned [0,320] domain. This avoids the
rotation-dependent corruption caused by feeding native-wide coordinates into
the static-world renderer. Every helper is an identity in 4:3. The HUD is
proportion-corrected before presentation, while depth-24 FMVs, BIOS output,
and full-screen 2D menu scenes remain pillarboxed at 4:3.

`PSX_WS_FRUSTUM_MODE=yaw` is a diagnostic A/B that enables only the upstream
yaw-ray widening; the default `full` mode also adjusts the four later object
side-plane tests. The private capture was analysis input only and remains
excluded from source, generated output, and all public artifacts.

## Validation boundary

This checkpoint establishes native play through the first level, stable
real-time pacing and audio, 4x rendering, and a user-validated horizontal mouse
integration point. Windows testing confirms the first-launch keyboard overlap
is fixed. Tests 4-6 showed that native-wide portal-span changes cause static
world geometry to disappear according to camera rotation; Test 7 restored
stability, and Test 9 safely widened only the upstream yaw frustum and matching
object side planes while keeping every portal invariant. Test 11 then exposed
an ambiguous rounded-SXY association in the optional presentation surface.
Test 12 replaces it with exact packet-address provenance and passed static
regeneration, deterministic tests, shader parsing, Windows x64 linking, and a
live A/B comparison. That comparison removed the fractures and stretching but
showed no noticeable improvement in wobble over Test 9. Later levels,
save/load behavior, vertical look, and broader regression testing remain open.

## Development rules

- Never edit generated/*.c; change configuration, seeds, project code, or the
  framework and regenerate.
- Keep input/, generated/, build output, saves, captures, and translated
  overlays local.
- Keep modern features opt-in until their authentic path is validated.
- Compare regressions against the same SLUS-00224 disc in a reference PS1
  emulator and fix the first observable divergence.
- Run the deterministic tests and inspect the complete staged file list before
  committing. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Copyright

The distributable source repository may contain build glue, factual addresses,
tests, and analysis metadata. It must not contain the game disc, extracted
assets, proprietary SDK files, generated translations of retail code, or
captured overlays. Users must generate those privately from a legitimately
owned copy. The framework overlay is derived from PSXRecomp and retains its
upstream licensing terms; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
No separate redistribution license has yet been selected for the
project-authored source.
