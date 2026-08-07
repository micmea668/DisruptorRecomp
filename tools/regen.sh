#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOL="$ROOT/psxrecomp/recompiler/build/psxrecomp-game"

if [ ! -x "$TOOL" ]; then
    echo "PSXRecomp has not been built. Run ./build.sh first." >&2
    exit 1
fi
if [ ! -f "$ROOT/input/SLUS_002.24" ]; then
    echo "Missing input/SLUS_002.24." >&2
    exit 1
fi

cd "$ROOT"
python3 tools/inspect_exe.py
python3 tools/extract_code_image.py
python3 tools/generate_seeds.py
"$TOOL" --config game.toml
python3 tools/audit_codegen.py
