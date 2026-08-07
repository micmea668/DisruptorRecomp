DISRUPTOR RECOMPILED — WINDOWS x64 MODERNISATION TEST 12
========================================================

RECORDED RESULT (2026-08-07)
----------------------------

The Test 11-style fractures and stretched facets are gone. Neither Exact
Geometry Only nor Exact Geometry + Fractional Camera produced a noticeable
reduction in wobble compared with the Test 9 Baseline. The procedure below is
retained as the historical test protocol.

Test 12 is the conservative repair for Test 11's fractured geometry. Test 11
could match a polygon vertex by rounded screen coordinate alone. That value is
not unique, so unrelated 3D vertices were sometimes paired and adjacent
triangles separated.

This build accepts presentation correction only when every vertex is tied to
the exact RAM address where its GTE projection was stored. A quad is accepted
as one four-vertex unit; if any vertex is unproven, both triangles use the
original integer renderer. Fractional mouse yaw uses the same exact gate, so
CPU-built HUD and other 2D polygons are never camera-warped.

Disruptor's original integer game state, collision, aiming, AI, save data and
59.94 Hz simulation remain authoritative. Test 9 widescreen, modern controls
and 4x rendering are unchanged.

TEST ORDER
----------

Use the same first-level scene for every run.

1. Run "Play Disruptor - Exact Geometry Only.cmd".
2. Middle-click in gameplay and rotate slowly while watching the ceiling,
   wall/floor seams, door frames and the bright green steps.
3. If there are no fractures, close normally and run
   "Play Disruptor - Exact Geometry + Fractional Camera.cmd".
4. Repeat tiny turns and a slow 360-degree rotation. Frame blending is disabled
   so any camera distortion cannot be confused with interpolation.
5. Run "Play Disruptor - Test 9 Baseline.cmd" in the same view.
6. If both exact modes are clean, the adaptive-smoothing launcher is an
   optional final comparison.

Stop a corrected run if geometry fractures again; the completed logs are still
useful. Afterward run "Collect Modernisation Results.cmd" and upload its ZIP
privately. Please report:

* whether every Test 11-style crack and stretched facet is gone;
* whether exact geometry reduces wobble compared with the baseline;
* whether the fractional-camera run improves the smallest turns;
* any HUD, weapon, door, enemy or widescreen-edge movement;
* performance, audio or control regressions.

MODERN CONTROL MAP
------------------

    W / S                 Move forward / backward
    A / D                 Strafe left / right
    Mouse left / right    Fire / activate current psionic
    Space                 Jump / menu cancel
    E                     Use
    F                     Activate current psionic
    Q / R                 Choose weapon / choose psionic
    Mouse X1 / X2         Choose weapon / choose psionic
    Tab                   Map
    P                     Pause
    Arrow keys            Original movement and menu fallback
    Enter                 Fire / menu confirm

Middle-click toggles mouse capture; Esc and focus loss release it. Edit
mouse-aim.ini to change horizontal sensitivity or inversion.

DISC SETUP
----------

You can drag a CUE file onto any launcher. Alternatively, copy your verified
636,350,064-byte BIN to input\Disruptor (USA).bin. The matching CUE is already
present. Use the original MODE2/2352 BIN/CUE; this private test accepts BIN/CUE,
not CHD.

Verified BIN SHA-256:
3b49f9874e30c613ca9d17720716764cd76d0ac968c0acd0f53159366c0cf3a4

PRIVACY
-------

Do not publish the executable, disc image or result ZIP. The executable embeds
translated retail code, and capture history may also be retail-derived. The
source checkpoint excludes retail data, generated translations, captured
overlays, binaries and test logs.
