#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RUNTIME="$ROOT/build/DisruptorRecompiled"
DISC="$ROOT/input/Disruptor (USA).cue"

[ -x "$RUNTIME" ] || { echo "Run ./build.sh first." >&2; exit 1; }
[ -f "$DISC" ] || { echo "The full Disruptor BIN/CUE is required." >&2; exit 1; }

MOUSE_AIM=0
MODERN_CONTROLS=0
WIDESCREEN=0
for arg in "$@"; do
    case "$arg" in
        --mouse-aim)       MOUSE_AIM=1 ;;
        --modern-controls) MODERN_CONTROLS=1; MOUSE_AIM=1 ;;
        --widescreen)      WIDESCREEN=1 ;;
        *)
            echo "Usage: ./run.sh [--mouse-aim] [--modern-controls] [--widescreen]" >&2
            exit 2
            ;;
    esac
done

export PSX_DISRUPTOR_CONTROL_PROBE=0
export PSX_DISRUPTOR_MODERN_CONTROLS=$MODERN_CONTROLS
if [ "$MOUSE_AIM" -eq 1 ]; then
    [ -f "$ROOT/mouse-aim.ini" ] || {
        echo "mouse-aim.ini is missing." >&2
        exit 1
    }
    export PSX_DISRUPTOR_MOUSE_AIM=1
    export PSX_FPS_TELEMETRY=0
    echo "Mouse aim enabled: middle-click in gameplay to capture; middle-click or Esc releases."
else
    export PSX_DISRUPTOR_MOUSE_AIM=0
fi

if [ "$MODERN_CONTROLS" -eq 1 ]; then
    cp "$ROOT/keybinds-modern.ini" "$ROOT/build/keybinds.ini"
    echo "Modern controls enabled: WASD, LMB fire, RMB psionic, Space jump, E use."
else
    cp "$ROOT/keybinds-original.ini" "$ROOT/build/keybinds.ini"
fi

GAME_CONFIG=game.toml
if [ "$WIDESCREEN" -eq 1 ]; then
    GAME_CONFIG=game-widescreen.toml
    echo "Experimental native-wide 16:9 enabled; menus and movies remain 4:3."
fi

echo 'OpenGL dev-menu builds: press the backquote (`) key; Escape closes it.'

cd "$ROOT"
exec "$RUNTIME" --game "$GAME_CONFIG" --disc "$DISC"
