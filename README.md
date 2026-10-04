# OStim Auto Align

SKSE plugin for OStim Standalone 7.5+ (Skyrim AE 1.6.1170). Aligns actors automatically by what the
scene does: hand on the shaft for a handjob, shaft in the vagina/anus for sex, mouth on the shaft for
oral, lips together for kissing.

## How it works

1. On every OStim scene change (`ostim_thread_scenechanged`, player and NPC scenes) it reads the
   scene's actions from OStim's scene file (`handjob`, `vaginalsex`, `blowjob`, `kissing`...) and
   picks the main one; other actions between the same two actors count with less weight.
2. After the animation settles it samples the body parts those actions use for one loop:
   `NPC Genitals01-06` (plus the glans tip, read from the genital mesh), `VaginaB1 -> VaginaDeep1`,
   `NPC Anus00 -> NPC Anus Deep1`, hand + finger joints, `Mouth00`/`MouthU`, toes, breasts, scrotum.
3. It solves for the smallest change that brings them together: one actor's offset (sliding costs
   less than lifting), a little scale (grows a standing/kneeling actor from the feet instead of making
   them float), and the schlong's SOSBend. Bends are predicted from measured TNG shapes
   (`tng_bend_shapes.json`), not tried live.
4. If it's at least `minImprovement` better, it hands the values to OStim's own alignment (OStim's
   thread API). OStim stores them per actor pair + scene in `My Games\Skyrim Special Edition\OStim\alignment.json`
   and applies them instantly the next time, so each scene is measured once. The OStim align menu
   still works for tweaks.
5. It measures again and undoes the change if it didn't help.

It keeps a record of what it did in `My Games\Skyrim Special Edition\OStim\autoalign.json`. A scene
whose alignment differs from both OStim's default and that record was aligned by hand: it is left
alone. Delete an entry (or the file) to have a scene measured again; to undo the alignments
themselves, reset them in OStim's align menu.

Settings: `Data\SKSE\Plugins\OStimAutoAlign\settings.json`. Log: `My Games\Skyrim Special Edition\SKSE\OStimAutoAlign.log`.

## Limits

- One constant correction per scene and actor pair: motion that tilts hips during thrusts can still
  leave the shaft a little off on some frames. Hands that are open in the animation stay open.
- Only the main action's two actors are moved (3+ actor scenes: the others are left as they are).
- Bend shapes were measured on TNG's female (ERF futa) and male behaviors; other schlong frameworks
  fall back to whichever table fits the measured shaft best.

## Building

VS 2022 Build Tools (C++ workload, CMake, Ninja) and a vcpkg checkout (`VCPKG_ROOT`, or `..\vcpkg`).
CommonLibSSE-NG is a git submodule.

    git clone https://github.com/nyxvel/OStimAutoAlign
    cd OStimAutoAlign
    git submodule update --init
    powershell -ExecutionPolicy Bypass -File build.ps1

Packages `dist\OStim Auto Align <version>.zip`; nothing is copied into the game.

## License

MIT (`LICENSE`). CommonLibSSE-NG is MIT too. `src/OStimAPI.h` re-declares part of OStim Standalone's
thread API (OStimNG, github.com/VersuchDrei/OStimNG) so the plugin can call it.
