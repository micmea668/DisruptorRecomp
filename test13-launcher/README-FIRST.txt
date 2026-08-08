DISRUPTOR MODERNISATION TEST 13 - PHASE A
=========================================

This is a source-only launcher template. It does not include the retail game,
generated game code, BIOS, DLLs, or DisruptorRecompiled.exe.

PACKAGE PREPARATION
-------------------

Place the privately built Test 13 files beside Play Disruptor.ps1:

  DisruptorRecompiled.exe
  BUILD-INFO.txt
  CHECKSUMS.sha256
  game-widescreen.toml
  keybinds-modern.ini
  mouse-aim.ini
  required runtime DLLs

BUILD-INFO.txt must contain this exact field, using the real 40-character
source revision used for the build:

  source_commit=<40 lowercase hexadecimal characters>

CHECKSUMS.sha256 must include a standard SHA-256 line for the executable:

  <64 hexadecimal characters> *DisruptorRecompiled.exe

The launcher recalculates that hash before every run. It also rejects a run
unless the runtime confirms the actual 4x OpenGL pipeline became ready, and it
rejects the software-renderer fallback explicitly.

COMPARISON ORDER
----------------

1. Play Disruptor - Test 9 Baseline.cmd
2. Play Disruptor - Exact Geometry Only.cmd
3. Play Disruptor - Current X-Only Camera.cmd
4. Play Disruptor - Full XY Camera.cmd
5. Play Disruptor - Coverage Tint.cmd

Use the same save, route, viewing direction, and slow mouse turns each time.
Frame interpolation is disabled in every mode. The coverage-tint mode is a
diagnostic map: tinted polygons are the ones accepted by exact provenance.

The launchers create logs, session files, saves, and a copied keybinds.ini.
Those files are private test output and must not be committed or published.
