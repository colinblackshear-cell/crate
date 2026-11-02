# Crate

A family of music tools that work together. Each one runs as a Mac app, as an Audio Unit in
Logic and as a VST3, and they all share one sound library. The plan is in the
[design doc](https://claude.ai/artifact/6vyRDxa4mE4HjedWpG4XMQ).

## Tools

### Sample Editor

Record from a mic or open a sound file, edit it, and save it to the shared library.

- **Get sound in:** record from any audio input, open WAV, AIFF, MP3 or FLAC, drag files onto the
  window, or click a sound in the library.
- **Edit:** drag across the waveform to select, drag a selection's edges to adjust it,
  shift-click to extend it, or double-click to select everything. Then cut, copy, paste, delete,
  trim to selection, silence, or trim quiet ends. **Snap to zero** moves cut points to where the
  wave crosses zero, so cuts don't click.
- **Fades:** fade in and out with four curve shapes. With nothing selected, a fade covers the
  set **Length** at the start or end of the sound. **Amount** sets how far the level drops
  (100% fades to silence, 50% only to half volume).
- **Level:** reverse, normalize and gain.
- **Effects panel:** pitch and speed, filter (low cut, high cut, resonance), compressor, drive,
  lo-fi (bits and sample rate), chorus, phaser, delay and reverb, plus presets such as Lo-fi
  sampler, Old radio, Dub echo and Big hall. Moving a control switches that effect on. Turn on
  **Hear effects** and press Play to listen while you adjust, then **Apply** the effects to the
  selection or to the whole sound.
- **Undo everything:** every edit, recording and opened file can be undone, up to 200 steps.
- **Save:** **Save to library** writes a WAV to `~/Music/Crate/Sounds`. **Export** saves a WAV or
  AIFF anywhere.

Keys: Space play/stop, R record, L loop, T trim to selection, I fade in, O fade out,
Delete delete, Cmd+Z / Cmd+Shift+Z undo/redo, Cmd+X/C/V cut/copy/paste, Cmd+A select all,
Cmd+O open, Cmd+S save to library, + / - zoom, F show all, Esc deselect.

In Logic, insert the Sample Editor as an Audio Unit effect on an audio track (it's listed under
Colin Blackshear). It records the track's input, plays your sound through the track, and saves
the sound with your project.

### MPC Bank Builder

Build kits for the Akai MPC2000XL and play them: a 4x4 pad grid laid out like the MPC, four
banks (A to D) of 16 pads, as many kits as you like, and export straight to the MPC's card.
It's the start of a virtual MPC; a sequencer comes next.

- **Fill pads:** drag WAV, AIFF, MP3, FLAC or MPC `.SND` files (or a whole folder) onto a pad, or
  click or drag sounds from the shared library. Several files fill the pads in order.
- **Chop:** **Chop across pads...** cuts a pad's sound at each hit, or into 2 to 16 equal slices,
  and puts each piece on its own pad from that one.
- **Play:** click a pad, or play a MIDI keyboard: pads use the same notes as on the MPC (A01 is
  note 37). Set each pad's volume, pan, tune and how it plays: **One shot** plays to the end,
  **Mono** cuts off the last hit, **Note off** stops when you let go.
- **Keep in time:** give the kit a BPM and turn on **Keep in time** to stretch every loop (keeping
  its pitch) to a whole number of beats at that tempo. Short hits aren't stretched, and any pad can
  be set to **Play normal**.
- **Export to card...** writes each kit as a folder (`BREAKS/BREAKS.PGM` plus its `.SND` files,
  8.3 names) to the MPC's SD card. On the MPC, go to **LOAD**, open the folder and load the
  `.PGM`. **Open kit...** opens a `.PGM` from the card again.

The memory readout shows how much of the MPC's sample memory a kit takes; pick your MPC's RAM next
to it. **Save as mono** halves a pad's size.

In Logic, add MPC Bank Builder as an instrument (AU Instruments > Colin Blackshear) on a software
instrument track. The track's MIDI plays the pads, and your kits and their sounds are saved with
the Logic project.

### Crate on your phone

A web app for recording on the go: open
[colinblackshear-cell.github.io/crate](https://colinblackshear-cell.github.io/crate/) in Safari, then
**Share > Add to Home Screen** so it opens like an app (and iOS keeps your sounds). It works with no
signal once it has opened once.

- **Record** from the phone's mic, with the phone's call processing turned off so field
  recordings sound natural. Drag across the waveform to select, then keep, delete, trim silence,
  fade, normalize or reverse. Undo goes back 50 steps.
- **Effects:** pitch (tape style), low and high cut with resonance, drive, bit crusher, sample
  rate reducer, delay and reverb. Hear them as you go, then apply.
- **Chop** at each hit, into equal slices, or at markers you tap onto the waveform.
- **Sounds** is the phone's library. **Share** sends a WAV to Files, iCloud Drive or AirDrop; save
  it to `Music/Crate/Sounds` on your Mac to use it in the other tools. **Import** brings in audio
  files and MPC `.SND` files.
- **Pads:** 4 banks of 16, laid out like the MPC. Tap to play. Turn on **Edit pads** to set a pad's
  volume, pan, tune and how it plays, or chop it across the pads after it.
- **Export for MPC** makes a zip of the kit's folder (`.PGM` plus `.SND` files). Unzip it in the
  Files app straight onto the MPC's SD card with an SD card reader, or AirDrop it to the Mac.

## The shared sound library

`~/Music/Crate/Sounds` is a plain folder of WAV files, so you can also manage it in Finder or drag
sounds into Logic. What the tools know about each sound is stored inside its WAV file, so it
survives renames:

- tempo, length in beats and root note go in the standard ACID chunk, which Logic reads
- tags and the name of the tool that made the sound go in the INFO chunk

Every tool works at 44.1 kHz, the MPC2000XL's sample rate, so sounds move to the MPC Bank Builder
without resampling.

## Installing a build

Each GitHub build has a download per tool, **Sample-Editor-mac** and **MPC-Bank-Builder-mac**, each
with three zips: the app, the AU and the VST3.

1. Unzip them. Put the `.app` in Applications, the `.component` in
   `~/Library/Audio/Plug-Ins/Components`, and the `.vst3` in `~/Library/Audio/Plug-Ins/VST3`.
2. Apple hasn't signed the builds, so macOS blocks them the first time. Either open
   **System Settings > Privacy & Security** and click **Open Anyway**, or run:

   ```
   sudo xattr -cr "/Applications/Sample Editor.app" ~/Library/Audio/Plug-Ins/Components/"Sample Editor.component"
   sudo xattr -cr "/Applications/MPC Bank Builder.app" ~/Library/Audio/Plug-Ins/Components/"MPC Bank Builder.component"
   killall -9 AudioComponentRegistrar
   ```
3. The first time you press Record, macOS asks for microphone access. In the apps, choose your
   audio devices under **Options > Audio/MIDI Settings**.

## Building

You need CMake 3.22 or later and a C++17 compiler (Xcode on the Mac). The first configure
downloads JUCE 8 and Signalsmith Stretch.

```
cmake -S . -B build -G Xcode
cmake --build build --config Release
ctest --test-dir build -C Release
```

Set `MPC_FIXTURES` to a folder of files saved by an MPC2000XL to also check that each one reads
and writes back byte for byte (CI uses the demo kits from [vMPC2000XL](https://github.com/izzyreal/mpc)).

## Layout

```
core/          shared by every tool: sound files, edits, effects, the sound library, common UI
tools/editor/  the Sample Editor (app + AU + VST3)
tools/mpc/     the MPC Bank Builder (app + AU instrument + VST3); mpc/ reads and writes .SND and .PGM
tools/phone/   Crate on your phone: a web app (plain JavaScript, no build step), published to GitHub Pages
tests/         headless tests for the core and the tools, the MPC file format tests, and
               tests/phone (run with `node --test tests/phone/*.test.mjs`)
```

A new tool goes in `tools/<name>/` with its own `CMakeLists.txt`. It links `crate_core` and uses
the library and theme from `core/`.
