# Wreck 0.1.2 – unverified items, guesses, and what differs from the mock-up

**Update 2026-11-08:** 0.1.3 has the same plugin file as 0.1.2 (identical `wreck.so`); only the license changed. The notes below describe 0.1.2.

**Update 2026-11-07:** 0.1.2 has been verified working on an Akai Force (reported by the maintainer). The notes below were written before that test; they list what was changed and what the offline checks could not show.

## Changed in 0.1.1 and 0.1.2, not yet confirmed on the Force

* **Knobs scrolling out of place** (0.1.0 and 0.1.1, your video). In the video the knob picture is fine at 0 % and slides further out of its window the higher the value goes, which means MPC steps through the strip with a slightly different frame height than the picture has. My 0.1.1 fix (a smaller total picture, 60 frames) did not cure it, so that explanation was wrong. 0.1.2 uses the knob picture format other plugins already run on the Force: 126 px frames, 128 of them, `numFrames` 127 (126 x 16128 px). The knob is smaller than your mock-up (126 px instead of 176 px). If it still scrolls, I need another short video; then the cause is something else in Wreck's skin.
* **Loading samples** had no list, so you could not see or pick files. 0.1.1 added the SAMPLES page: a list of the WAV files, tap to load, RESCAN, PLAY AS SOURCE, the folder path on screen. Files must be in `/sdcard/Synths/L'Cronx - VST - Wreck/data/wav`; I did not add other folders because I do not know the Force's sample paths. Your video shows the page and its tab but no files loaded yet.
* **Chain strip numbers** (SRC 75, B5 75, LIM 75, OFF ...): the level of each stage in percent, OFF for a block that is off. My addition, not in your mock-up.

## Differences from your mock-up (decided, not hidden)

1. **OVERVIEW and `<` `>` buttons, and the overview tiles.** MPC's own tabs switch pages; a plugin cannot draw buttons that switch
   MPC tabs. They are not on the skin. A single page with modes would lose per-page Q-Links unless the engine routed generic
   Q-Links; I judged that too risky without a Force to test on.
2. **Preset list rows** (the 7-row list with FACTORY/USER tags) are replaced by a big preset stepper plus FACTORY/USER buttons and LOAD / SAVE / INIT.
3. **"11.4 S" live write-head time** in the buffer box is left out.
4. **SIMPLE / FULL VIEW** buttons are left out.
5. **Pages Block4 and Block5** of the mock-up were not in the files I had; SHIFT and SMEAR use the same layout as the other block pages.
6. The mock-up is 1280×800; the Force's plugin area is 1280×628. Pages were re-laid at 628 with the same type, gold and furniture, not copied verbatim.

## Things I added or guessed

* **VOICES count (`v_count`)** and the **FEEDBACK knob** on the INPUT page, the **ATTRACTOR** and **WAV FILE** steppers, and the chip values in the chain strip.
* The exact meaning of each macro (CHAOS / SPACE / GRIT / BODY) is mine.
* The factory presets' names and settings are mine and have not been listened to on hardware.
* 0.6 output trim and the soft clip level are guesses at a safe level.

## Unverified on hardware

* Everything under "NOT RUN" in CHECKLIST.md: audio, CPU, live text size, knob strip rendering, overlay strip, presets, WAV loading, Q-Link order.
* The text height rule (MPC height = 1.52 × CSS size) was measured earlier on other skins; Wreck's `vs=57` value text is based on that.
* The overlay (waveform + playhead) depends on MPC painting the 2×2 corner markers exactly as the page art says.
* **Host-test failure**: the framework's test reports that one tick on `src` does not step one option. The engine rounds option values, so a tiny nudge stays put. I believe the wrapper settles this on hardware but did not confirm it. If the Source grid does not step with the Q-Link on the Force, this is the cause.

## Packaging placeholders

* **Repo** `sunskiefer/Wreck` is a placeholder; the repository does not exist.
* **License** `MIT` is a placeholder. Your other plugin uses MIT with a credit clause; tell me what you want.
* Version is `0.1.0`; `params.lock` was regenerated because Wreck has never been released.
* MPC OS compatibility is "3.x only" (set by the skin's TUI format), not 2.x.

## Panel Editor

The editor from KutilXXL is not in this environment, so no Panel Editor was produced. The layout is `layout.conf` (generated
by `tools/make_pages.py` from `tools/make_data.py`) plus `data/layout.txt`; I can generate an editor project once I have the tool.

## No x86 tests

Per your rule, I did not use the x86 harness for sound checks. Nothing was run through speakers.
