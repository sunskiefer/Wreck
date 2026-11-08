# Wreck – user guide

**A self-sampling instrument for the Akai Force and MPC OS (VST2, armv7).**
Vendor: L'Cronx. Plugin UID: `AnWr`. Version 0.2.0.

Wreck listens to itself. A *source* (noise, clicks, a WAV file, a maths-driven generator) runs through a chain of five
effect blocks and a limiter. The result is recorded all the time into a 20 second circular buffer. The notes you play are
**voices** that read that buffer back: slower, faster, higher, lower, reversed, from fresh material or from older material.
Whatever the voices play can be fed back into the front of the chain, so the instrument keeps eating and re-eating its own
sound. Behind the voices sits an output bus (bouncing-ball delay, lo-fi, stutter), and an arpeggiator with four independent
lanes plays the voices for you.

The name on the panel is always **WRECK**.

---

## 1. Install

Requires MPC OS 3.x on an armv7 Force/MPC (Gen1). It is not marked compatible with MPC OS 2.x.

1. Unzip `Wreck-0.1.2-mpc-armv7.zip`. It contains `INSTALL.md` (the full steps), `install.sh` / `uninstall.sh` (run on the
   device as root; MPC is stopped and restarted and `MPC.settings` is backed up) and the plugin folder under `portable/`.
2. Either run `install.sh`, or copy the folder `L'Cronx - VST - Wreck` from `portable/` into `/sdcard/Synths` by hand, as
   described in `INSTALL.md`. The folder already contains `wreck.so`, the skin and the `data` folder.
3. Restart MPC OS. Add **Wreck** from the plugin list on a track.

Folders inside `data/`:

| Folder | Purpose |
|---|---|
| `data/wav/` | Optional. The **SAMPLES** page browses the whole drive; this folder is just one place you can browse to. |
| `data/user/` | Your saved presets (`U0001.wreck`, `U0002.wreck`, …). Plain text, one per preset. |
| `data/layout.txt` | Where the waveform strip is drawn on screen. Leave it alone unless you know why. |

Preset files store the **folder and name** of the WAV file, not its position, so adding or removing files never changes what a preset loads. The drive must be connected when you load such a preset.

---

## 2. The signal path in one picture

```
 SOURCE ─▶ BLOCK ─▶ BLOCK ─▶ BLOCK ─▶ BLOCK ─▶ BLOCK ─▶ LIMITER ─▶ [ 20 s BUFFER ]  ← always recording
   ▲        (the five blocks run in the order YOU choose on the CHAIN page)                │
   │                                                                                       ▼
   │                                                                       up to 16 VOICES read it back
   │                                                                       (speed, pitch, direction, attack, release)
   └──────────── FEEDBACK (what the voices play goes back to the source input) ◀───────────┤
                                                                                           ▼
                           OUTPUT BUS:  STUTTER ▶ LO-FI ▶ BOUNCING BALL ▶ DC block ▶ LEVEL ▶ soft clip ▶ out
                           (Direct, Monitor and Solo add taps from inside the chain to this bus.)
```

Things worth knowing:

* The buffer records the signal **after the limiter**, so what you hear on playback is the processed chain, not the dry source.
* Changing the order or settings of the chain changes what is *being recorded now*. Notes already in the buffer stay as they were
  recorded, and slide away as the 20 seconds wrap around. That is where the "wreck" comes from.
* Voices always read **behind** the write head, so a note never plays something that has not been recorded yet.
* The output bus is *after* the voices, so STUTTER / LO-FI / BALL affect the playback, not the recording.

---

## 3. The screen

The Force shows Wreck's pages as native MPC tabs. The Q-Link knobs control the current page's controls in the order listed
in each section below: the first eight are Q-Link bank 1, and any further ones (the OUTPUT and ARP pages have nine) are
bank 2.

Every page shares the same furniture:

* **Header** – `WRECK` and the page title.
* **Chain strip** – seven chips (SRC, then the five blocks, then LIM) that show what is where. The chip for the current
  page is lit gold, and each chip shows a value from its block.
* **Buffer box** – a small waveform of the 20 second buffer with a moving **playhead** (the write position) and one
  marker for every sounding voice.

Colour: black ground, gold only where something is active or a value is set. White text is a name or a number.

---

## 4. The pages

### INPUT – the source
Q-Links: SOURCE LEVEL, RATE, FEEDBACK, ATTRACTOR, PREVIEW, GATE, TUNE, WAV FILE.

**Source grid** – ten generators, tap one:

| Source | What it makes |
|---|---|
| NOISE | White noise through a low-pass that follows RATE. |
| CLICKS | Random impulses, about 8 a second at RATE centre. |
| WAV | The loaded file, looped. RATE is playback speed. |
| FM PING | Bell-like pings at random pitches from a pentatonic set. |
| HARMONOGRAPH | Two damped pendulums per axis; when they die they are struck again. |
| CLIFFORD | The Clifford attractor map, one step per tick. |
| SUPERFORMULA | The radius of a superformula shape drifting around while its symmetry slowly changes. |
| SPIROGRAPH | A hypotrochoid traced as a waveform. |
| MOIRE | Two gratings drifting apart and beating against each other. |
| ATTRACTORS | One of 15 chaotic systems integrated at audio rate (see ATTRACTOR). |

**Knobs**

* **SOURCE LEVEL** – source volume into the chain. Centre-right (0.75) is unity, full is +12 dB.
* **RATE** – how fast the source runs, 1/16× to 16×. This is the main "pitch" of every generator.
* **FEEDBACK** – how much of the voices' output is sent back to the *input* of the chain (up to 90 %). Start low; the limiter
  catches it but the sound gets dense quickly.

**Steppers** (use the `<` `>` arrows, or the Q-Link)

* **ATTRACTOR** – 1…15: Lorenz, Rössler, Chen, Lu, Thomas, Halvorsen, Aizawa, Dadras, Sprott B, C, D, E, F, G, H. Only used by the ATTRACTORS source.
* **WAV FILE** – on the SAMPLES page; chooses an entry (folder or file) in the folder you are browsing. Use with the WAV source.

**Toggles**

* **PREVIEW** – adds the raw source straight to the output so you can hear what it is doing without waiting for the buffer.
* **GATE** – the source only sounds while a key is held (with a slow release), so notes start and stop sound.
* **TUNE** – the source's RATE follows the last note played (60 = unchanged), so the source becomes playable.

### SAMPLES – browse and load your own WAV files
Q-Links: WAV FILE, OPEN / LOAD, UP, BACK.

The page is a file browser. Samples can stay on the external drive; nothing has to be copied to the main card.

1. The line at the top shows the folder you are in. The list below shows its folders (`/NAME`) first, then its `.wav` files
   (16/24/32-bit PCM or 32-bit float, up to 30 s each), eight rows at a time; the chosen row has the gold border. The first row `..` goes one folder up.
2. **Tap a folder** to open it. **Tap a WAV** to load it; tapping also switches the INPUT source to WAV.
3. **UP** goes to the parent folder (the cursor lands on the folder you came out of). **ROOT** jumps to `/`, the very top of the tree.
   **BACK** and **FWD** walk through the folders you have visited, like a browser.
4. The first time, Wreck starts in `/media` (or `/` if there is none). Wreck does not know where your Force mounts the external drive,
   so look for it from ROOT (for example under `/media` or `/mnt`). After that it opens in the folder you used last.
5. The **WAV FILE** stepper (`<` `>`, the Q-Link, or the data wheel) moves the gold border through all entries of the folder, page after page.
   Stepping onto a WAV loads it. **OPEN / LOAD** does the same as tapping the chosen row: it opens a folder or loads the WAV and makes it the source.
   **PLAY AS SOURCE** only switches the source to WAV.
6. **RESCAN** reads the folder again; use it after plugging in a drive or copying files. The count at the bottom left shows the folders and WAV files here
   (EMPTY, or CANNOT OPEN if a folder cannot be read; go UP or BACK).
7. Hidden files (names starting with a dot) are not listed. A folder shows up to 1023 entries.

On the INPUT page the WAV source speed is **RATE**. A user preset remembers the file by folder and name.

### CHAIN – the order of the five blocks
Q-Links: POSITION 1…5, then the five block ON switches.

Five position cards, one per slot. Each card lists the five blocks (DRIVE, ECHO, FILTER, SHIFT, SMEAR); tap the one that
should sit in that slot. The signal runs slot 1 → slot 5. Two slots can hold the same block; a block can also be left out
of every slot. Below each card is a `<BLOCK> ON` toggle: this is the same switch as on the block's own page.

Default order: DRIVE → ECHO → FILTER → SHIFT → SMEAR (SHIFT is off by default).

Order matters. Drive before filter is a different sound from filter before drive; smear before drive turns the reverb tail
into the thing that gets distorted.

### DRIVE, ECHO, FILTER, SHIFT, SMEAR – the blocks
Each block page has five knobs plus **BLOCK ON** and **SOLO**.
Q-Links: the five knobs, then BLOCK ON, then SOLO.

**SOLO** on a block page mutes the whole output except for what that block puts out (see Monitor).

**DRIVE** – a soft-clipping saturator.
* DRIVE – amount of gain into the clipper (the curve gets steep at the top).
* TONE – low-pass on the saturated signal, so the harshness can be tamed.
* BIAS – shifts the waveform before clipping for asymmetric (even-harmonic) distortion. DC is removed again.
* MIX – dry/wet.
* LEVEL – output level of the block.

**ECHO** – a stereo delay with filtered feedback.
* TIME – 10 ms to 1.5 s.
* FEEDBACK – how much comes back. Capped below self-oscillation.
* TONE – low-pass inside the loop, so repeats get darker.
* MIX – dry/wet.
* SPREAD – offsets the right channel's delay time and cross-feeds the two sides, which widens the echoes.

**FILTER** – a resonant low-pass (state-variable) filter.
* CUT – 20 Hz upward; the filter itself tops out at about 14 kHz.
* RES – resonance.
* ENV – bipolar: the filter's cutoff follows the loudness of the signal, up (+) or down (−).
* MIX / LEVEL – dry/wet and output level.

**SHIFT** – a pitch shifter (two cross-fading read taps).
* PITCH – −24 to +24 semitones. Default +7.
* FINE – −50 to +50 cents.
* FEEDBACK – feeds the shifted signal back through the shifter, which builds climbing or falling spirals.
* MIX / LEVEL.

**SMEAR** – a small reverb with a moving size.
* SIZE – room size.
* DECAY – how long it rings.
* DAMP – how quickly the highs die in the tail.
* MIX / LEVEL.

### VOICES – how notes read the buffer
Q-Links: SPEED, PITCH, DIRECTION, ATTACK, RELEASE, PITCH MODE, VOICES.

* **SPEED** – playback speed, ¼× to 4×. This is separate from pitch: in FREE mode speed *is* pitch; in KEY mode the key sets pitch.
* **PITCH** – ±24 semitones added to every voice.
* **DIRECTION** – FWD, REV, PING (alternates), RAND (picks per note).
* **ATTACK / RELEASE** – 1 ms to 2 s, exponential.
* **PITCH MODE** – where a note starts and how it is tuned:
  * **FREE** – keys do not change pitch; velocity picks how far back in the buffer the note starts (hard = fresh, soft = older).
  * **KEY** – like FREE, but the key transposes (middle C is unchanged).
  * **LOCK** – the *key chooses the place in the buffer* (low keys = recent, high keys = older); pitch is not transposed.
  * **PLAY** – the voice starts at the live edge of the buffer and is transposed by the key. Closest to a normal keyboard.
* **VOICES** – 1…16 voices. When all are busy, the oldest releasing voice is stolen first.

### OUTPUT – the bus
Q-Links: BALL TIME, BOUNCE, LO-FI RATE, STUTTER SIZE, MIX, BALL ON, LO-FI ON, STUTTER ON, SYNC.

Order inside the bus: STUTTER → LO-FI → BOUNCING BALL.

* **BALL TIME** – first gap of the bouncing-ball delay: 1/32 … 1/1.
* **BOUNCE** – how fast the gaps shrink. Low = even repeats; high = repeats that accelerate like a ball coming to rest. Eight taps in total, alternating sides.
* **LO-FI RATE** – sample-rate reduction (1.5 kHz to 44.1 kHz) with matching low-pass and bit-depth reduction.
* **STUTTER SIZE** – 1/32 … 1/4. When STUTTER ON is engaged, the last slice of that size is captured and looped.
* **MIX** – how much of the bouncing-ball repeats are added.
* **BALL ON / LO-FI ON / STUTTER ON** – the three effects.
* **SYNC** – when on, BALL TIME and STUTTER SIZE are note values at the project tempo. When off they are at 120 BPM.

### ARP – four polymetric lanes
Q-Links: ARP ON, LATCH, RATE, RANGE, ORDER, NOTE LEN, VEL LEN, GATE LEN, OCT LEN.

When ARP is on, the notes you hold are played by the arp (the plugin plays its own voices; MPC does not forward MIDI sent from a plugin).

* **ARP ON / LATCH** – latch keeps the arp running after you let go.
* **RATE** – 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32.
* **RANGE** – 1 to 4 octaves.
* **ORDER** – UP, DOWN, UP-DN, RANDOM, PLAYED.
* **Four lanes of 16 steps**, each with its own length:
  * **NOTE** – a step either plays or rests.
  * **VEL** – high (full) or low velocity.
  * **GATE** – long (90 % of the step) or short (35 %).
  * **OCT** – plays the step an octave up, or not.
  * The lanes loop independently. With lengths of 16, 5, 7 and 3 the pattern takes a very long time to repeat. That is the polymetric part.
* **LEN steppers** – each lane's length, 1…16. Defaults: NOTE 16, VEL 5, GATE 7, OCT 3.

### MACROS
Q-Links: CHAOS, SPACE, GRIT, BODY.

Four big knobs that move several things at once:

| Macro | What it does |
|---|---|
| **CHAOS** | Slow random walks on source rate, voice speed, shift pitch and filter cutoff. The mod wheel adds to it. |
| **SPACE** | Raises echo feedback and mix, reverb size and mix. |
| **GRIT** | Raises drive and drive mix, lowers the lo-fi rate, and crushes lo-fi bit depth. |
| **BODY** | Warms the sound: darker drive tone, lower filter cutoff. |

### MONITOR – listen to the inside
Q-Links: seven MON toggles (SRC, B1…B5, LIM), DIRECT, LEVEL.

Seven tap cards, one for each point in the chain. Each has:
* **MON** – adds that tap to the output so you can hear it. One at a time.
* **SOLO** – mutes everything except the solo-ed taps. Several can be solo-ed at once. SOLO overrides MON and DIRECT.

Also:
* **DIRECT** – adds the chain's input signal (source + feedback) to the output.
* **LEVEL** – master volume. Centre-right is unity.

### PRESETS
Q-Links: BANK, PRESET, LOAD, SAVE, INIT.

* **BANK** – FACTORY or USER.
* **PRESET** – the big stepper chooses the preset.
* **LOAD** – loads the one shown.
* **SAVE** – saves the current sound to the USER bank as the next free number (`U0001`, `U0002`, …). There is no limit.
* **INIT** – resets everything to defaults.

User presets store every control and the **folder and name** of the WAV file.

---

## 5. Factory presets (22)

Rust Bucket · Tape Ghost · Slow Collapse · Glass Teeth · Basement Choir · Drift Wreck · Chrome Dust · Tin Cathedral ·
Undertow · Static Bloom · Pendulum · Hollow Engine · Wet Concrete · Frozen Radio · Spiral Stairs · Black Ice · Bent Signal ·
Moth Light · Scrap Orchestra · Low Tide · Fault Line · Last Transmission

---

## 6. Playing it – a few starting recipes

1. **First sound.** Load *Tape Ghost*. Hold a key. Raise **FEEDBACK** a little and listen to the sound feeding on itself.
2. **Play it like a sampler of the last 20 seconds.** VOICES → PITCH MODE **PLAY**. Play a melody over any source.
3. **Scrub the past.** PITCH MODE **LOCK**. Low keys play the latest material, high keys older material.
4. **Make a loop machine.** Turn ARP on, LATCH on, pick lane lengths that do not divide each other, and let it run while you change blocks.
5. **Break it.** CHAOS up. SOLO one block to hear what it alone is doing, then put it somewhere else in the CHAIN.
6. **Rhythm out of nothing.** SOURCE: CLICKS, ECHO TIME short, FEEDBACK high, OUTPUT STUTTER 1/16 with SYNC on.

---

## 7. Musical approaches

Wreck is a sound design tool. It is built so you can start from almost nothing (noise, a click, a file of your own) and keep
reshaping it for hours. Nothing is hidden: every stage of the chain has its own page, you can listen to any stage on MONITOR,
and the pages are plain and large so you can work fast and keep your attention on the sound.

A way of working that suits it:

1. **Pick a source and set its RATE.** The source decides the raw character. Slow RATE gives drift and rumble, fast gives hiss and shimmer.
2. **Shape it in the chain.** Add one block at a time. Use SOLO to hear what a block does by itself, and change the order on the CHAIN page when it sounds too tidy.
3. **Let it record.** The 20 second buffer is always listening. Wait a few seconds so there is something to play.
4. **Play it.** Choose a PITCH MODE and play. The voices read the buffer, so what you play is what the chain made a moment ago.
5. **Feed it back.** Raise FEEDBACK a little at a time, so the voices' sound goes back into the chain and changes what is recorded next.
6. **Keep what you like.** SAVE as a user preset. Use INIT when a patch has become a mess, and start again from a different source.

### Ideas by goal

**An evolving drone bed.** SOURCE: ATTRACTORS (try Lorenz or Thomas), RATE low. SMEAR with a long DECAY and a high MIX, ECHO with moderate FEEDBACK.
PITCH MODE PLAY, ATTACK and RELEASE long (around 1 s or more). Hold a chord and nudge CHAOS very slightly so the pitch and filter wander.

**An instrument from your own recording.** Put a WAV on your drive, pick it on SAMPLES, and press PLAY AS SOURCE. Keep RATE at 1.00 X, set PITCH MODE PLAY,
then add FILTER and DRIVE. You are now playing a texture made from your recording, as it sounded a moment ago, and as the chain changes it.

**Rhythm out of nothing.** SOURCE: CLICKS. ECHO with a short TIME and high FEEDBACK turns the clicks into pitched repeats. Turn ARP on, LATCH on,
and set the lanes to different lengths (for example NOTE 16, VEL 5, GATE 7, OCT 3). The lanes drift against each other, so the pattern keeps changing.
Add STUTTER at 1/16 with SYNC on for a locked, glitchy feel.

**Bells and metal.** SOURCE: FM PING. SHIFT at +7 or +12 with some FEEDBACK makes rising, ringing spirals. A short SMEAR keeps them tight; raise DECAY for a metal-room tail.

**Dark, slow, wrecked tape.** SOURCE: NOISE with a low RATE, DRIVE for grit, FILTER with a low CUT and some RES. Set VOICES SPEED to 0.5 X or lower and DIRECTION to REV or PING.
Turn LO-FI ON on the OUTPUT page and lower its RATE.

**Scrubbing the past.** PITCH MODE LOCK. Low keys play the newest material, high keys play material from further back. Play a slow line up the keyboard to move backwards through the last 20 seconds.

**Pitched stabs from any sound.** PITCH MODE PLAY with short ATTACK and RELEASE. The note you play transposes the buffer, so any texture becomes a playable stab.
Use VELOCITY: in FREE and KEY modes a soft note starts from older material, a hard one from the newest.

**Layering textures.** Run two instances on two tracks: one as a slow bed (long ATTACK, low SPEED), one as a short, rhythmic voice (ARP on). Save both as presets.

**Learning what a block really does.** On MONITOR turn on MON for one stage at a time, or SOLO a block. Listening to the source, then each block, then the limiter teaches you more than any number on a knob.

### Things to try when you are stuck

* Swap two blocks on the CHAIN page and listen again.
* Turn a block OFF instead of lowering its MIX; the chain strip shows OFF.
* Change only the source RATE, and nothing else, for a minute.
* Raise a MACRO (CHAOS, SPACE, GRIT or BODY) halfway and play with it as a performance control.
* Load a factory preset, then change the source. The preset's chain stays, so you get a new sound through a shaped path.

### Gain and safety

The chain ends in a limiter before the buffer, and the output has a soft clip, so extreme settings will not blow up the output. FEEDBACK above about 70 % gets dense quickly.
Lower the master LEVEL on MONITOR before you explore very loud settings.

---

## 8. Technical notes

* 44.1 kHz, stereo, 128-frame blocks. Voices, chain and output run in 32-bit float.
* No memory allocation, file access or waiting in the audio thread. WAV loading and preset files are handled by a worker thread.
* The buffer is 20 seconds (882,000 stereo frames).
* Built for armv7 hard-float with a glibc ceiling of 2.17.
* Parameter count: 205 (including internal arp lane steps, sample-list rows, browser buttons and Q-Link arrows).
