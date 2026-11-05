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
- **Play it from the keys:** a MIDI keyboard, the keys along the bottom of the window, or your
  computer's letter keys (switch on **Play with computer keys**: A to L are the white keys,
  W E T Y U O P the black ones, Z and X change octave) play the sound across the keyboard. The
  **Root** key plays it as recorded; higher keys play it higher and faster. With a selection, the
  keys play just that part. **Plays** sets what a key does: *While held*, *Loop while held*, or
  *To the end* (like drum pads). **Attack** and **Release** fade each note in and out. The root
  note is saved with the sound in the library.
- **Smart chop:** **Find hits** puts a slice marker at each drum hit, note or word; **Sensitivity**
  finds more or fewer, and **Split evenly** spaces them equally instead. Drag a marker to move it,
  click the ruler to add one, and Option-click (or drag it off the waveform) to remove one.
  Double-click a slice to select it. The keys then play one slice each from C1 up (**Plays:
  Slices from C1**), and **Save slices** puts each one in the library as its own sound, ready for
  the MPC pads. The markers are saved with your project.
- **Undo everything:** every edit, recording and opened file can be undone, up to 200 steps.
- **Save:** **Save to library** writes a WAV to `~/Music/Crate/Sounds`. **Export** saves a WAV or
  AIFF anywhere.

Keys: Space play/stop, R record, L loop, T trim to selection, I fade in, O fade out,
Delete delete, Cmd+Z / Cmd+Shift+Z undo/redo, Cmd+X/C/V cut/copy/paste, Cmd+A select all,
Cmd+O open, Cmd+S save to library, + / - zoom, F show all, Esc deselect.

In Logic there are two versions, both listed under Colin Blackshear and both saving the sound with
your project:

- **Sample Editor Instrument** (AU Instruments): put it on a software instrument track to play
  your sound from Logic's keys, regions and Musical Typing. It has no audio input, so open a sound
  or pick one from the library rather than recording.
- **Sample Editor** (Audio Units effects): put it on an audio track to record the track's input.

In the app, MIDI keyboards are switched on automatically as soon as they're plugged in.

### MPC Bank Builder

Build kits for the Akai MPC2000XL and play them: a 4x4 pad grid laid out like the MPC, four
banks (A to D) of 16 pads, as many kits as you like, and export straight to the MPC's card.
It's a virtual MPC: kits, pads and a sequencer.

- **Fill pads:** drag WAV, AIFF, MP3, FLAC or MPC `.SND` files (or a whole folder) onto a pad, or
  click or drag sounds from the shared library. Several files fill the pads in order.
- **Trim and effects:** double-click a pad (or press **Trim and effects...**) to edit its sound.
  Drag across the waveform to pick a part, then trim to it, delete it, trim the quiet ends, fade
  in or out (four fade shapes), normalize or reverse. The effects are the Sample Editor's: pitch
  and speed, filter, compressor, drive, lo-fi, chorus, phaser, delay and reverb, with presets.
  Turn on **Hear effects** and press **Play** (or Space) to try them before **Apply** writes them
  into the pad, or into just the selected part. Undo and redo (Cmd+Z) work until you press Done.
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

#### The sequencer

The bar under the kit name is the transport, like the MPC's: pick a **sequence** (each has its
own tempo, length and up to 64 tracks), then **|<** (play from the start), **Play**, **Stop** and
**Rec**. Space plays and stops. The display reads bar.beat.tick, with 96 ticks a beat like the
MPC2000XL.

- **Record:** press **Rec** and play the pads: click them, play a MIDI keyboard, or use the
  computer keys (Z X C V, A S D F, Q W E R, 1 2 3 4 are the 16 pads on screen, bottom row first,
  like the MPC). From stop it counts in a bar with the metronome; while playing, **Rec** punches
  in and out. It records onto the selected track and adds to what's there (overdub).
- **Timing** corrects what you record to 1/8, 1/16, 1/32 or triplets, with **Swing** (50% is
  straight, 66% is a triplet feel). **Fix timing** snaps a track you already recorded.
- **Note repeat:** hold a pad while it plays and it repeats on every step of the timing grid.
- **Steps:** shows the sequence as a grid for the 16 pads of the bank you're on. Click or drag
  across steps to add or remove hits; right-click a step for soft or hard hits. The selected
  track is bright and the others are faint. Click a pad's name to play it.
- **Tracks** can be named, muted and added (up to 64). **Undo** and **Redo** cover edits and
  recordings.
- **Save MIDI** writes the sequence as a MIDI file on the MPC's own pad notes (A01 is 37), at 96
  ticks a beat, so the MPC2000XL loads it into a sequence. **Open MIDI** brings a MIDI file in as
  a new sequence.
- **In Logic**, the sequence plays along when Logic plays, at Logic's tempo and position (turn off
  **Play with Logic** to stop that). Sequences are saved with the Logic project.

The memory readout shows how much of the MPC's sample memory a kit takes; pick your MPC's RAM next
to it. **Save as mono** halves a pad's size.

In Logic, add MPC Bank Builder as an instrument (AU Instruments > Colin Blackshear) on a software
instrument track. The track's MIDI plays the pads, and your kits and their sounds are saved with
the Logic project.

### Crate Rig (for playing live)

One window that plays the tools together, on one clock, for a live set. It's a Mac app, and a
Logic plugin (see **The Rig in Logic** below).

- **One transport:** **Play** (or the space bar) starts the MPC's sequencer and everything else
  together, **Stop** stops and goes back to the start. Set the **Tempo** with the buttons or tap
  it with **Tap**; **Click** turns the metronome on. The MPC follows the Rig's tempo the way it
  follows Logic's.
- **The Stage:** the first tab shows every tool on one screen while you play. Along the top is
  the song: the part playing, what's next and when, a button for each part, **< Back**,
  **Next >** and **Song mode**. Below, each tool has a tile showing what it's doing right now,
  with its mute and meter: the **MPC**'s sequence and where it is in it, a button per sequence
  and **Play song** for the song you built in the MPC; the **Bass** line with its step lit as it
  plays, its 8 patterns and the sequencer; the **Synth**'s arp stepping along; the **Looper**'s
  eight layers going round (click one to record, stop or overdub it); and the **Vocoder**,
  **Input** and **Sampler**. Everything you start there comes in on the next bar. Click
  anywhere else on a tile to go into that tool's tab.
- **Two screens:** **Pop out** (next to the tabs) opens the tab you're on in its own window. Drag
  it to a second screen (with one plugged in, it goes there by itself): the Stage on one screen
  and the MPC on the other, say. Its tab says where it went; close the window (or **Bring it back
  here**) to put it back. Which tools are out, and where, is saved with the show, so they open on
  the same screens next time.
- **The MPC in time:** a sequence picked while the Rig plays (on the Stage, in the MPC, or by a
  song part) comes in at the next bar, from its top. **Play song** plays the MPC song on the
  Rig's clock: playing, it comes in at the next bar; stopped, it starts everything from the top.
- **The tools in tabs:** the **MPC**, **Vocoder**, **Bass**, **Synth** and **Sampler** tabs are the full tools, the
  same as their own apps. The vocoder's code comes from the
  [vocoder repo](https://github.com/colinblackshear-cell/vocoder) at the commit pinned in
  `CMakeLists.txt`; bump it to pick up vocoder changes.
- **Your keyboard (e.g. a Launchkey 61):** every MIDI keyboard is switched on as soon as it's
  plugged in. The drum pads (MIDI channel 10, the Launchkey's drum mode) play the MPC's pads:
  the 16 pads play pads 1-16 of the bank picked in **Pads play MPC bank**. The keys play what's
  picked in **Keys play**: the Vocoder, the Sampler (the sound in the Sampler tab, across the
  keys), the Bass, the Synth, the MPC's pads note for note, or nothing. **Split, below** splits
  the keys: notes below the split note play the second pick (the Bass, say) and the rest play
  **Keys play**, so your left hand plays bass while your right plays chords.
- **Bass:** a deep mono bass synth with a 16-step sequencer (8 patterns). Click a step to put a
  note on it; the **Accent** and **Slide** rows make the acid squelch and glide. It runs on the
  Rig's clock and a new pattern starts at the next bar. While it runs, the keys move the pattern
  up and down; while it's stopped they play the bass directly. **Random** writes a pattern for you.
- **Synth:** an 8-voice synth (saw, square, triangle, supersaw) with filter, envelopes, an LFO
  synced to the tempo, and an **arpeggiator** (up, down, up-down, random, as played; rate,
  octaves, gate, latch) with a 16-step rhythm row: light a step to play it, the lower row
  accents it.
- **Vocoder:** your audio input is its voice. **On** in its mixer strip turns it on and off.
- **Mixer:** a level, mute and meter for the MPC, the Vocoder, the Bass, the Synth, the Input, the Sampler and the master. Double-click a
  level to put it back to 0 dB.
- **Learn:** press **Learn** next to a level, Play or Stop, then move a fader or knob or press
  a button on your keyboard. From then on it controls that. Press Learn again while it's waiting
  to forget the control.
- **Effects on every strip:** press **FX** on a strip to open its effects under the tabs. Each
  strip, the master too, has a **Filter** (one knob: turn left for a low-pass that closes down,
  right for a high-pass that thins it out; the middle is off), **Resonance**, **Drive**, **Crush**,
  a **Delay** in time with the tempo (ping-pong; the Delay knob is how much goes in, so pull it
  down and the echoes ring out), **Reverb**, and **Repeat**, a beat repeat that loops the next
  1/4, 1/8, 1/16 or 1/32 until you click it again. **FX** lights up while any effect is on;
  **Reset** turns them all off. Put any of them on a Launchkey knob with **Knobs**.
- **Input:** plays an instrument or mic from your audio interface (a guitar, a second mic, a
  drum machine) through its own strip and effects. It's off until you press **On**, so a mic
  doesn't feed back; pick **1+2**, **In 1** or **In 2** for which input it listens to (say the
  vocoder's mic on 1 and a guitar on 2).
- **Looper:** the **Looper** tab has eight loop layers that stay in time with the Rig. Pick what a
  layer records (**Mic / input**, the Vocoder, Bass, Synth, MPC, Sampler, or the **Rig mix**) and
  press **Rec** (or keys **1**-**8**): it starts at the next bar (if the Rig is stopped, it starts
  with it) and stops at the end of the bar where you press again, so loops are always whole
  bars. Pick **1**-**16 bars** first and it stops by itself. **Rec** on a playing loop overdubs
  (**Dub**); **Undo** takes back the last overdub, **Clear** empties the layer, and each layer has a
  mute and a level. Loops follow the tempo when you change it. The **...** menu imports a loop
  (stretched to the Rig's tempo) or a whole song (setting the Rig's tempo to it), saves the
  loop as a WAV, and puts the layer's record and mute on buttons of your keyboard. All the
  layers play through the **Loops** strip, with its own effects. Loops are saved with the show,
  as WAV files in a folder next to it (`My show loops`).
- **Song:** the **Song** tab builds a song from parts (scenes). Get something going (an MPC
  sequence, a bass pattern, the arp, loops, some tracks muted), then press **+ Add scene**: it
  keeps the MPC's sequence, the bass pattern and sequencer, the synth's arp, which strips and
  loop layers are muted, and whether the vocoder and the input are on. Press a part's **Play**
  and it takes over at the next bar (straight away if the Rig is stopped). **Update** keeps
  what's playing now in that part instead. With **Song mode** on, a part with a length (1-32
  bars) plays that long and then the next part starts by itself; **Stay** waits for you.
  **< Back** and **Next >** step through the parts; **Keyboard...** puts them on buttons of your
  keyboard (the Launchkey's scene arrows, say). Parts are saved with the show.
- **Knobs:** to put a keyboard knob on any control in any tool (the bass cutoff, the synth's LFO
  depth...), press **Knobs** > **Learn a knob**, touch the control on screen, then turn the knob.
  The same menu lists your knobs so you can forget one, or all of them.
- **Sample any tool into another:** the **Sample** row (bottom left) picks what to record (the
  **Mic / input**, the MPC, Vocoder, Bass, Synth, Input strip, Sampler, Loops, or the whole **Rig
  mix**, each as you hear it), where it goes (**into Sampler**, or **onto MPC pad**: the next empty
  pad of the bank your keyboard's pads play), and how long (**1**-**16 bars**, or **Free**). Press
  **Sample** at the top (or on the Stage): it starts at the next bar (straight away if the Rig is
  stopped) and stops by itself after the bars, so the bass line lands on a pad as a perfect loop,
  in time (set to its length in beats for the MPC's keep in time). With Free, press again to stop.
- **Record set (for Logic):** press **Record set** and from the next bar (or when you press Play)
  the Rig records every tool as its own WAV (as you hear it, after its effects and level), the
  mix, and what you play on the keys and pads as a MIDI file with a track for each tool. Press
  again to stop, or stop the Rig. Each take goes in its own folder in `~/Music/Crate/Rig/Recordings`;
  set Logic to the tempo it says in `Open in Logic.txt` and drag everything in at bar 1: it all
  lines up. Silent strips are left out, and the metronome is never recorded.
- **Sounds (your sound bank):** the **Sounds** tab finds every sample in your folders: your
  Splice folder (`~/Splice`, where Splice saves what you download), `~/Music/Crate/Sounds` and
  the usual sample folders are looked through by themselves, and **Folders...** adds any other
  folder. It sorts each sound into a kind (Kicks, Snares, Claps, Hats, Cymbals, Toms, Percussion,
  Bass, Synths, Keys, Guitar, Vocals, FX) from its name and folders, and reads the tempo and key
  from names like `NS_Keys_Loop_92_Fmin`. Type to search, pick a kind, show only loops or one-shots,
  or **Near tempo** for loops close to the Rig's tempo. Click a sound to hear it (**Hear on click**),
  then send it **Onto MPC pad** (the next empty pad; double-click or Return does the same),
  **Into Sampler**, or **Onto a loop layer** (stretched to the Rig's tempo). The files stay where
  they are; the Rig looks for new downloads each time it opens, or straight away with **Folders... > Look through the folders again**.
- **Shows:** **Save show** saves everything (every tool's sounds, patterns and settings, the loops, the mixer, tempo,
  routing, learned controls and knobs) to `~/Music/Crate/Rig`; **Open show** brings one back. The Rig
  also saves where you left off when you quit and opens it again next time.
- **Audio + MIDI:** pick your audio interface and buffer size. A buffer of 128 samples or less
  keeps the delay from key to sound under about 3 ms.

#### The Rig in Logic

The whole Rig also runs inside Logic, as an instrument called **Crate Rig**:

1. Make a **Software Instrument** track and pick **AU Instruments > Colin Blackshear > Crate Rig**
   (choose **Multi Output** if you want each tool on its own track).
2. **Your mic:** make an audio track for the mic (or just know its input), then at the top of
   the Crate Rig window set **Side Chain** to it. The vocoder, the Input strip, sampling and
   the looper hear it from there.
3. **Your keys:** select the Crate Rig track and play; the Launchkey comes in through Logic and
   the Rig splits it between the tools as usual (keys, pads, split, learned knobs).
4. **The clock is Logic's:** the Rig uses Logic's tempo and time signature, plays when Logic plays,
   from where Logic is, and stops when Logic stops (the MPC's sequences and songs, the bass,
   the arp and the loops all line up with Logic's bars). With Logic stopped, the Rig's own
   **Play** still works, to jam.
5. **A track for each tool:** with the Multi Output version, click **+** at the bottom of the
   instrument's channel strip in Logic's mixer to add tracks for the MPC, Vocoder, Bass, Synth,
   Input, Sampler and Loops. A tool with its own track plays there (after its strip in the Rig)
   and leaves the Rig's main output, so you can record and mix each one in Logic.
6. Logic saves the show with the song. The loops are kept as WAVs in
   `~/Music/Crate/Rig/In Logic`.

To record: arm the Crate Rig track (and its tool tracks) and record in Logic as usual; you get
the MIDI you played, and Bounce in Place or the tool tracks give you audio. **Record set** in the
Rig works too.

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
with the app, the AU and the VST3, and **Crate-Rig-mac** with the Rig app and its Logic plugin (`Crate-Rig-AU.zip`). Sample-Editor-mac also has the instrument version
(**Sample-Editor-Instrument-AU** and **-VST3**).

1. Unzip them. Put the `.app` in Applications, the `.component` in
   `~/Library/Audio/Plug-Ins/Components`, and the `.vst3` in `~/Library/Audio/Plug-Ins/VST3`.
2. Apple hasn't signed the builds, so macOS blocks them the first time. Either open
   **System Settings > Privacy & Security** and click **Open Anyway**, or run:

   ```
   sudo xattr -cr "/Applications/Sample Editor.app" ~/Library/Audio/Plug-Ins/Components/"Sample Editor.component"
   sudo xattr -cr ~/Library/Audio/Plug-Ins/Components/"Sample Editor Instrument.component"
   sudo xattr -cr "/Applications/MPC Bank Builder.app" ~/Library/Audio/Plug-Ins/Components/"MPC Bank Builder.component"
   sudo xattr -cr "/Applications/Crate Rig.app" ~/Library/Audio/Plug-Ins/Components/"Crate Rig.component"
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
tools/editor/  the Sample Editor (app + AU + VST3, plus an AU/VST3 instrument version)
tools/mpc/     the MPC Bank Builder (app + AU instrument + VST3); mpc/ reads and writes .SND and .PGM
tools/rig/     Crate Rig: plays the tools together live, on one clock (app only)
tools/phone/   Crate on your phone: a web app (plain JavaScript, no build step), published to GitHub Pages
tests/         headless tests for the core and the tools, the MPC file format tests, and
               tests/phone (run with `node --test tests/phone/*.test.mjs`)
```

A new tool goes in `tools/<name>/` with its own `CMakeLists.txt`. It links `crate_core` and uses
the library and theme from `core/`.
