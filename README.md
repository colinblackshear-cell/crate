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

## The shared sound library

`~/Music/Crate/Sounds` is a plain folder of WAV files, so you can also manage it in Finder or drag
sounds into Logic. What the tools know about each sound is stored inside its WAV file, so it
survives renames:

- tempo, length in beats and root note go in the standard ACID chunk, which Logic reads
- tags and the name of the tool that made the sound go in the INFO chunk

Every tool works at 44.1 kHz, the MPC2000XL's sample rate, so sounds move to the MPC bank builder
without resampling.

## Installing a build

Each GitHub build has a **Sample-Editor-mac** download with three zips: the app, the AU and the VST3.

1. Unzip them. Put `Sample Editor.app` in Applications, `Sample Editor.component` in
   `~/Library/Audio/Plug-Ins/Components`, and `Sample Editor.vst3` in `~/Library/Audio/Plug-Ins/VST3`.
2. Apple hasn't signed the builds, so macOS blocks them the first time. Either open
   **System Settings > Privacy & Security** and click **Open Anyway**, or run:

   ```
   sudo xattr -cr "/Applications/Sample Editor.app" ~/Library/Audio/Plug-Ins/Components/"Sample Editor.component"
   killall -9 AudioComponentRegistrar
   ```
3. The first time you press Record, macOS asks for microphone access. In the app, choose your input
   under **Options > Audio/MIDI Settings**.

## Building

You need CMake 3.22 or later and a C++17 compiler (Xcode on the Mac). The first configure
downloads JUCE 8 and Signalsmith Stretch.

```
cmake -S . -B build -G Xcode
cmake --build build --config Release
ctest --test-dir build -C Release
```

## Layout

```
core/          shared by every tool: sound files, edits, effects, the sound library, common UI
tools/editor/  the Sample Editor (app + AU + VST3)
tests/         headless tests for the core and the editor
```

A new tool goes in `tools/<name>/` with its own `CMakeLists.txt`. It links `crate_core` and uses
the library and theme from `core/`.
