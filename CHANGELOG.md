# Changelog

## 0.2.0
* SAMPLES is now a file browser, so samples can live on the external drive instead of the main card. It lists the folder you are in (folders first, then `.wav` files), starts in the folder you used last (else `/media`, else `/`), and goes anywhere from the root `/`.
* New buttons: BACK and FWD (folder history), UP, ROOT, OPEN / LOAD. Tap a row: a folder opens, `..` goes up, a WAV loads and becomes the source. The data wheel / WAV FILE stepper walks the list; stepping onto a WAV loads it, OPEN / LOAD opens a folder.
* The folder you are in is shown at the top of the page; the count shows folders and WAV files. RESCAN re-reads the folder (use it after plugging in a drive).
* A preset, and the saved project, remember the WAV by folder and name (older presets with only a name still load, from the folder you are in). If the drive is not connected the preset loads without the sample.
* The WAV list is no longer limited to 64 files: a folder shows up to 1023 entries.
* Nothing else changed: same sound, same other pages. The new parameters are added at the end of the list, so older projects keep their settings.

## 0.1.3
* License changed to the PolyForm Noncommercial License 1.0.0 (free to use, change and share for any noncommercial purpose; not for sale). The `LICENSE` file is in the repo and the release zip carries the license in its manifest. Earlier releases were published under no license file and are not covered by it.
* The plugin is the same as 0.1.2: the plugin file is byte for byte identical, so nothing in the sound or the pages changed.

## 0.1.2
* Verified working on an Akai Force (2026-11-07).
* Knobs use the 126 px, 128-frame picture format that other Force plugins run on, to stop the knob picture sliding out of its window as the value changes. The knobs are smaller than in 0.1.1.
* Knobs sit a little lower on the pages.

## 0.1.1
* New SAMPLES page: a list of your WAV files (tap to load), RESCAN, PLAY AS SOURCE, the WAV folder shown on screen, and the file count.
* The WAV FILE stepper stops at the last file.
* Knob picture reduced to 60 frames (did not fix the sliding on the Force).

## 0.1.0
* First build: 10 sources, 5 reorderable blocks, 20 second buffer, 16 voices, output bus, polymetric arp, macros, monitor and solo, 22 factory presets, user presets.
