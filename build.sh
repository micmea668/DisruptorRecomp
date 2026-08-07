#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
FRAMEWORK="$ROOT/psxrecomp"
PIN=$(tr -d '[:space:]' < "$ROOT/PSXRECOMP_PIN")

for command in git cmake python3; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "Missing prerequisite: $command" >&2
        exit 1
    }
done
[ -f "$ROOT/input/SLUS_002.24" ] || {
    echo "Copy the verified SLUS_002.24 into input/ before building." >&2
    exit 1
}

if [ ! -d "$FRAMEWORK/.git" ]; then
    git clone https://github.com/mstan/psxrecomp.git "$FRAMEWORK"
fi
git -C "$FRAMEWORK" fetch --quiet origin "$PIN"
git -C "$FRAMEWORK" checkout --detach "$PIN"
python3 "$ROOT/tools/apply_framework_overlay.py" --framework "$FRAMEWORK"
python3 "$ROOT/tools/patch_openbios_seeds.py" --framework "$FRAMEWORK"

GENERATOR=""
if command -v ninja >/dev/null 2>&1; then GENERATOR="-G Ninja"; fi

# shellcheck disable=SC2086
cmake -S "$FRAMEWORK/recompiler" -B "$FRAMEWORK/recompiler/build" $GENERATOR \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$FRAMEWORK/recompiler/build" --parallel \
    --target psxrecomp-game psxrecomp-bios

cd "$ROOT"
python3 tools/inspect_exe.py
python3 tools/extract_code_image.py
python3 tools/generate_seeds.py
"$FRAMEWORK/recompiler/build/psxrecomp-game" --config game.toml
python3 tools/audit_codegen.py

cd "$FRAMEWORK"
bash tools/regen_bios.sh --config bios/OpenBIOS.toml

cd "$ROOT"
# shellcheck disable=SC2086
cmake -S . -B build $GENERATOR -DCMAKE_BUILD_TYPE=Release \
    -DPSX_RECOMP_UI=OFF -DPSX_ENABLE_VULKAN=OFF
cmake --build build --parallel --target psx-runtime

echo "Build complete. Add the matching BIN/CUE, then run ./run.sh."
