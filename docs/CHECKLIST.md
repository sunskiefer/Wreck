# Wreck 0.1.2 – pass / fail checklist

**Update 2026-11-08 (0.2.0):** the SAMPLES page is now a file browser (UP, ROOT, BACK, FWD, OPEN / LOAD, folder path, WAV from any folder). Offline only: an x86 build of the engine was driven through folders, history, loading and preset save/load with AddressSanitizer, and the ARM build and release zip passed the installer and catalog checks. NOT RUN on a Force: browsing the external drive (where it is mounted is not known), loading a WAV from it, the new buttons and the data wheel on the folder list, and the 8-row list highlight while moving between folders.

**Update 2026-11-08:** 0.1.3 has the same plugin file as 0.1.2 (identical `wreck.so`); only the license changed. The notes below describe 0.1.2.

**Update 2026-11-07:** 0.1.2 has been verified working on an Akai Force (reported by the maintainer). The NOT RUN rows below were written before that test and have not been individually re-ticked.

PASS = checked in this build. NOT RUN = needs the Force (or a person) and has not been done. FAIL = known problem.

## Build and package

| Item | Result |
|---|---|
| ARM cross-build (armv7 hard-float, Zig) of `wreck.so` | PASS |
| Highest glibc symbol 2.17 (limit 2.31) | PASS |
| 199 parameters, `params.lock` regenerated (plugin never released, so safe) | PASS |
| 14 pages generated (new SAMPLES page), 0 skin overlap warnings | PASS |
| Page markers written into all 14 page backgrounds (overlay strip) | PASS |
| `Wreck-0.1.2-mpc-armv7.zip` built by `release.py` (188 files, 2.7 MB) | PASS |
| `sh -n install.sh`, `sh -n uninstall.sh` | PASS |
| `catalog_check.py --catalog` | PASS (0 warnings) |
| `data/` shipped with `wav/`, `user/`, `layout.txt`; `data` marked as user data so updates keep it | PASS |

## Look

| Item | Result |
|---|---|
| Pages rendered offline at 1280×628 match the approved mock-up: header, chain strip, buffer box, big text, 126 px gold-rim knobs from your images, gold toggle borders | PASS (offline renders) |
| Knob filmstrip 126 × 16128 px, 128 frames, `numFrames` 127: the same picture format as other plugins that run on the Force (the 176 px strips of 0.1.0 and 0.1.1 scrolled on the Force) | PASS offline; turning in place on the Force NOT RUN |
| Live values and names drawn by MPC at the right size on the Force | NOT RUN |
| Knobs turn in place on the Force (no up-and-down drift) | NOT RUN |
| SAMPLES page: files listed, tap loads, RESCAN, PLAY AS SOURCE | NOT RUN |
| Waveform strip + playhead in the buffer box (overlay) | NOT RUN |

## Sound / behaviour (engine)

| Item | Result |
|---|---|
| Compiles with no warnings that matter; no allocation or file I/O in `render` (by code reading) | PASS |
| Audio on a Force: source → chain → buffer → voices → bus | NOT RUN |
| CPU on a Force (`bench.sh`) | NOT RUN |
| All 22 factory presets load and sound as intended | NOT RUN |
| User preset save and load (`U0001.wreck`…) on the Force's storage | NOT RUN |
| WAV loading from `data/wav/` on the Force | NOT RUN |
| Arp with the four lanes on a project tempo | NOT RUN |
| Q-Link order on every page (bank 1 / bank 2) | NOT RUN |
| Framework host test, "tick 1 steps one option" for `src` | FAIL / unresolved (see UNVERIFIED.md) |
| Framework host test, "process() accumulates" | not re-run (fix applied: DIRECT default 0) |

## Not done

* Install on the Force: not done.
* Push to GitHub, release, or the plugin catalog: not done.
* Panel Editor: not produced (see UNVERIFIED.md).
