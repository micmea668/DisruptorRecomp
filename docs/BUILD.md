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

## Without the game

A machine without the game can still check a change:

```sh
DISRUPTOR_WITHOUT_GAME=1 ./build.sh
```

This compiles every source except the translated game code, leaves the runtime
unlinked, and runs the tests that read no retail data. It needs Ninja. The
`build` workflow runs it on Linux and under MSYS2 MinGW-w64 for every pull
request.

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
