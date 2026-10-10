# Building from source

## Requirements

- The verified Disruptor USA executable and BIN/CUE files in `input/`; see
  [DISC.md](../DISC.md).
- Git, Python 3, CMake 3.20+, and a C++20 toolchain.
- Windows x64: Visual Studio with **Desktop development with C++**, or
  MSYS2 MinGW-w64. Ninja is recommended.
- Linux: GCC and the platform development libraries needed to build SDL.
  Linux is a development target; Windows is the primary tested platform.

The first build needs network access to fetch the pinned PSXRecomp revision and
dependencies. OpenBIOS is built locally; no proprietary PlayStation BIOS is
required.

## Windows

Run from an x64 developer PowerShell at the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
.\run.ps1
```

The build verifies the executable, applies the framework overlay, generates and
audits the translated code, builds the runtime, and runs the tests. The runtime
is produced in `build/`, or `build/Release/` for a multi-configuration generator.
Set `DISRUPTOR_BUILD_DIRECTORY` to build into and run from another folder of the
repository instead. `build.sh` and `run.sh` read it too.

Windows builds also produce `DisruptorLauncher.exe` beside the runtime and
stage its configuration, help, and initial settings there. The launcher is
compiled with a static MSVC runtime so setup can open before the game runtime's
VC++ redistributable is installed. It imports existing disc dumps; physical
drive reading is outside the current scope. Release packaging includes it
automatically through `tools/package_release.py` and still excludes game data.

For launcher-only iteration after configuration:

```powershell
cmake --build build --config Release --target disruptor-launcher disruptor-disc-import-test
ctest --test-dir build -C Release -R disruptor_disc_import --output-on-failure
```

`DisruptorLauncher.exe --verify IMAGE` checks an image without copying it.
`--import IMAGE --root FOLDER` performs the normal import without starting the
game; `--check --root FOLDER` verifies installed data and required build files.
These diagnostic modes return zero only on success and report to redirected
stdout/stderr. The GUI always resolves the build folder from its executable,
so shortcuts work even when opened from a different working directory.

Supported revisions are defined in `src/launcher/disc_import.cpp`. Future PAL
and Japanese entries must include the full raw-disc size/hash and a matching
game configuration and compiled runtime support. Do not enable a region by
serial alone; the current game hooks and translated code are USA-specific.

Optional launch switches can be combined:

```powershell
.\run.ps1 -ModernControls -Widescreen -GeometryCorrection -PerspectiveTextures
.\run.ps1 -ModernControls -VerticalLook
```

`-MouseAim` enables horizontal mouse aim without the modern action bindings.
Use the in-game settings menu for render scale, other aspect ratios, fullscreen,
audio, and control preferences.

## Linux

```sh
chmod +x build.sh run.sh tools/regen.sh
./build.sh
./run.sh --modern-controls --widescreen --geometry-correction --perspective-textures
```

`--mouse-aim` and `--vertical-look` are also available.

## In-between frames

The presentation-only frame interpolator is built by default, but starts off
until enabled in **Settings → Enhancements**. Configure with
`-DDISRUPTOR_FRAME_INTERPOLATION=OFF` to leave it out.

## Generated game code

The build scripts generate the translated resident game code locally from your
verified executable. That retail-derived code is excluded from this source
repository. The Windows alpha reads game assets and runtime-loaded code from
the player's disc; it does not require loose game executables or captured files.

If a private regeneration produces `generated/overlays_static.c`, CMake can
also link that optional static overlay. The current alpha does not use it.
Keep generated game translations, disc files, captures, and binaries outside
Git; publish runtime binaries as release assets rather than source commits.

## Incremental build and tests

After the initial setup, use the same compiler environment:

```sh
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Changes to `psxrecomp-overlay/` must be applied before rebuilding:

```sh
python tools/apply_framework_overlay.py --framework psxrecomp
```

The framework revision is pinned in `PSXRECOMP_PIN`, and the reviewed overlay
files are listed in `PSXRECOMP_OVERLAY_FILES.txt`. Runtime preferences live in
`settings.toml` beside the executable; game files, generated code, and build
directories are ignored by Git.

## Replay capsules

Ctrl+F12 in the game starts a recording and Ctrl+F12 again ends it. The game
writes a folder under `capsules/` beside `settings.toml`: a save state, the
input of every frame after it, the settings and the build it was made with.
A recording ends by itself after ten minutes, or when a save state is loaded.
The word REC shows in a corner for a second when a recording starts, and REC
saved when it ends.

```sh
python tools/replay_capsule.py capsules/1791553413 --pictures out
```

This plays the capsule back without a window or sound, writes each frame as a
PNG and exits with 0 when the replay stayed in step with the recording. A
replay is the same run every time. It is not the recorded run bit for bit: a
loaded state takes its interrupts a few instructions apart, so some frames
differ from the recording in a few bytes of the game's memory, from one in ten
to seven in ten in the runs measured. A replay counts as in step when it has the
recording's memory at some frame of its last 60.

A capsule holds the pad buttons, the heading the mouse wrote and the pitch of
the vertical look. It does not hold analog sticks, cheats or anything changed
in the settings menu while it was recorded. It plays in the build that made
it and with the settings it was made with, and it is not recorded in a network
game. The pictures are of the software frame, which a wide aspect squashes:
the stretched and in-between frames need a window.
