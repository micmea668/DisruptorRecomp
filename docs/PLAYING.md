# Getting started

This Windows x64 alpha requires your own **Disruptor (USA, SLUS-00224)** disc
image. Game assets are not included. You do not need to build the project,
extract its executable, or supply a PlayStation BIOS to use the release.

Join the [community Discord](https://discord.gg/aeTQjaQUr) to discuss the project
and share feedback.

## Setup

1. Download the Windows x64 ZIP from the
   [GitHub releases](https://github.com/micmea668/DisruptorRecomp/releases).
   Choose the Windows ZIP asset, rather than GitHub's source-code archives.
2. Use **Extract All** and open the extracted folder. Keep its files together
   in a writable location, such as your Games folder.
3. On Windows 10 or 11 x64, install the latest
   [Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe)
   if it is not already installed. The GPU driver must support OpenGL 3.3.
4. Double-click **DisruptorLauncher.exe**, select **Browse...** in the
   **Disc** row, and locate your USA **MODE2/2352** CUE or BIN. Dropping the
   image on the launcher window does the same. Keep a selected CUE beside
   the BIN it references. Filenames do not need to match the package's names.
   A raw image named `.iso` or `.img` is also accepted if its complete bytes
   match the supported dump. A cooked 2048-byte ISO cannot be used.
5. The launcher copies the image into `input/discs/SLUS-00224/`, creates the
   CUE, and checks the complete SHA-256 of the copied data. Your originals are
   kept. Leave **Start the game after importing** checked to boot immediately.
   On later runs, select **Play game**; the disc is verified before every boot.
   **Play Disruptor.cmd** opens the same launcher.
6. At the game menu, use the arrow keys and **Enter** to select **New Game**,
   or choose **Practice Mode** to try a training mission.

The launcher's **Display**, **Enhancements**, **Controls** and **Audio** tabs
hold the settings of the in-game menu. A change is saved to `settings.toml`
at once and applies the next time the game starts. **Original** and
**Enhanced** set a group of them in one step, and **Reset settings** returns
them to those of a new installation. The tabs are locked while the game runs.
If the game stops with an error, the launcher shows the end of `startup.log`.

OpenBIOS is included. A PAL or Japanese disc cannot be the game disc, but it
can give the game its language: see [Other languages](#other-languages). Keep
the raw BIN/CUE format; do not convert the disc to a cooked ISO.

Older releases without `DisruptorLauncher.exe` use manual setup: copy the BIN
and CUE into `input/`, name the CUE `Disruptor (USA).cue`, and run
`Play Disruptor.cmd`. New launcher builds also recognize and import this old
layout automatically. Allow about 610 MiB of additional free space for an
import. A repair preserves previous installed files in an `input/discs/.previous-*`
folder; these backups can be removed manually once the repaired copy works.

## Controls

| Action | Default key or button |
| --- | --- |
| Move / strafe | W / S / A / D |
| Turn | Mouse, after capture; arrow keys also work |
| Capture / release mouse | Middle-click; Escape also releases |
| Fire / menu confirm | Left mouse / Enter |
| Psionic attack | Right mouse / F |
| Jump / menu cancel | Space |
| Use | E |
| Choose weapon / psionic | Q / R |
| Map / pause | Tab / P |
| Settings | Backquote (`` ` ``); Escape closes |
| Fullscreen | Alt+Enter |
| Skip a logo or movie | Escape |
| Fast-forward | Hold ] |
| Save / load state | Shift+F1–F12 / F1–F12 |
| Name a save | Type it: letters, digits, Space, Backspace and Delete. Enter moves to DONE |

Modern mouse/keyboard controls, geometry correction, and perspective textures
are enabled initially. Vertical mouse look is experimental and starts off;
enable it in **Settings → Controls** if desired.

**Settings → Enhancements** offers 1x–8x resolution, widescreen options,
HUD size for widescreen, and experimental 60 FPS gameplay and in-between
frames. Both frame options start off; in-between frames need exact geometry.
The **Shadows** setting offers Vanilla and Improved shapes when geometry
correction is enabled. HUD size ranges from 50% to 100% in widescreen and
leaves the weapon size unchanged.

**Skip the logos and the title movie** in Enhancements skips the boot intro
automatically. It starts off; the other movies still play normally. Escape
can skip the logo or movie currently on screen when the settings menu is closed.

The default is 4x at 4:3; lower the scale if performance is poor. Menus and
movies remain at 4:3. Controls, display, and audio preferences are saved in
`settings.toml` beside the executable.

## Other languages

The game can take its language from a second disc image that you own. In the
launcher, press **Add disc...** in the **Language** row, or drop the image on
the window, and pick the image of the French, German or Japanese release. The
launcher checks that it fits your US disc and then uses it where it is. It is
not copied, so keep it there. The **Language** list keeps every disc you have
added: choose one of them or **English** before **Play game**. **Remove**
takes a disc off the list and leaves the file alone. The US disc is still
required and stays the game disc.

The same choice is in the game: open **Settings → System**, press **Choose a
disc image**, then press **Restart now**. **English** there removes the
second disc again.

The French and German discs give their movies, speech, menus, messages and
the hints shown while a mission reloads. The Japanese disc gives its movies,
speech and hints. Its menus are in English on that disc as well. A few long
French and German menu lines are shortened to fit the screen.

Save states belong to the language they were made in. With a language disc
the twelve slots are kept in a folder of their own, `saves/German` for the
German disc, so the slots start empty after a change of language and the
earlier ones are back when you return to it. A state holds what the game has
read of its disc, and the discs are not cut alike. Memory card saves do not
depend on the language.

## Saves and updates

Use the game's memory-card save system for long-term progress. Memory cards and
save states are stored in `saves/`; back up that folder before installing a new
release. Save states are specific to their build and may not load after an
update. **Save states from v0.1.0-alpha.1 and alpha.2 are incompatible with
v0.1.0-alpha.3.**
Use a memory-card save or start a new game, then create new save states.
The original password system is also available in the game.

To avoid importing again, copy `input/discs/` into the new extracted build.
The launcher verifies it on startup. Keep `settings.toml` too if you want to
retain your controls and display preferences.

God Mode resets off on launch. Granting all weapons and psionics marks the game
as cheated, including subsequent game saves.

## Troubleshooting

- **Missing MSVCP140 or VCRUNTIME140 DLL:** install the latest x64 Visual C++
  Redistributable linked above, then relaunch. See Microsoft's
  [runtime download guidance](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/).
- **Disc not found:** open the launcher and browse to your original image.
  If selecting a CUE, keep its referenced BIN in the location named by its
  `FILE` line. Import from an extracted, writable build folder.
- **Import rejected:** the launcher requires the complete, unmodified USA
  raw dump. Cooked ISO files, other regions, missing CUE tracks, offset tracks,
  and multi-track layouts are rejected. Selecting a correct raw BIN directly
  also works; the launcher generates its CUE.
- **Import cancelled or interrupted:** your source files remain intact.
  Browse to the image again to retry. Incomplete data is never used to boot.
- **Wrong disc or boot failure:** only the supported USA revision works. Its
  BIN is 636,350,064 bytes and has SHA-256
  `3b49f9874e30c613ca9d17720716764cd76d0ac968c0acd0f53159366c0cf3a4`.
- **Slow rendering:** lower resolution to 2x or 1x in Enhancements and update
  your graphics driver. Try turning off experimental 60 FPS gameplay and
  in-between frames if they are enabled.
- **Mouse does not turn:** enter gameplay and middle-click to capture it.
- **Other problems:** include the level, reproduction steps, render settings,
  and `startup.log` in a [bug report](https://github.com/micmea668/DisruptorRecomp/issues).
  Do not attach your game image, game assets, or BIOS files.

This is an early alpha. The first mission and training-level path have been
tested, including the recent 8x / 32:9 corruption fix. Full campaign coverage
remains incomplete, and minor geometry or texture artifacts can remain.
