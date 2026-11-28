# Kaleidobox: a deep analysis of the tools, for planning what comes next

*Written 2026-11-28 from the code on `main` of the `crate` repository (commit 22e4545, through PR #136), plus the owner's stated goals. Meant to be pasted into another AI assistant (no code access needed) to think about future features, interface flow, user experience and capability.*

---

## 0. How to use this document (note to the assistant reading it)

You are being asked to help plan the future of a music app called **Kaleidobox**. You cannot see the code, so this document describes everything that exists today: what each tool is, what it can do, how a person moves through it and between tools, and where it is thin. The owner (Colin, a musician and producer, not a programmer) will bring your suggestions back to the developer.

Good suggestions here:

- **Respect the rules in section 1.** Ideas that simplify by removing features, add an "Easy mode", or put buttons on screen that do nothing will be rejected.
- **Build on what exists.** The audio engine is strong. Most gains are in how tools connect, how the instruments move and breathe, and how quickly an idea becomes a layer.
- **Say where it lives.** For each idea, say whether it is for the Mac app, the iPhone app, or both, which screen it goes on, and what the person taps or clicks.
- **Rank by impact on making music.** Rank ideas by what they do for making music and performing it live, not by technical elegance.

Section 9 lists open questions the owner would like you to think about.

---

## 1. What Kaleidobox is, and the rules it follows

### 1.1 Two apps, one family

| | **Kaleidobox (Mac)** | **Kaleidobox Pocket (iPhone)** |
|---|---|---|
| Role | A pro tool. (1) Play and record its instruments into Logic Pro as plugins. (2) A live performance rig with the **looper at the centre**. | A simple, fun **companion app** for catching ideas on the fly. Usable by non-musicians, with the Mac's depth underneath. |
| Feel | "Simply advanced": deep, but every control is plain. Vintage hardware look (cream panels, walnut, lamps, LCDs). | Night-sky look taken from the app icon. Thumb-first, big chips, bottom sheets. |
| Core sellers | All tools on one clock, the looper, the song, Logic round trip, live show moves. | Sampler (with effects and looping), the looper as the timeline, effects, beat pads, adding drums, bass and synth, voice layers ("Voice Memos on steroids"). **How easy it is to add and remove layers is the key.** |

Both are written in C++ with the JUCE framework and share engine code (the drum machine, synth, vocoder, effects rack, drone, quantize maths, tempo detection, etc.).

### 1.2 The owner's rules (non-negotiable)

1. **Keep every existing feature** and the way the tools talk to each other. No feature is removed to simplify.
2. **"Simply advanced"**: the depth stays, the surface is clear. **No Easy mode.** Kids and teenagers should be able to explore *everything*, not a cut-down version.
3. **Every visible button must do something.** No dead or placeholder controls.
4. **Phone buttons must be thumb-intuitive**, and button placement is crucial.
5. **Every feature added to the phone must also come to the Mac.** The Mac is the more advanced version of the same flow, and the two should feel like one product.
6. On the phone, **every instrument must be reachable from the bottom bar** (or a picker), so you can jump between them freely, jam, and send to the looper.

### 1.3 The musical vision

Deep, experimental electronic dance music and a **"life-changing" live show**: tribal, ritual, tropical, minimal, still user friendly. Deep original patterns, deep drums, experimental sounds. Seamless layering is the top priority: hear the song, go to any instrument, play over it until it's right, and see where it lands in the song.

### 1.4 The owner's gear

A Mac with **Logic Pro** (needs Audio Unit plugins), a **Novation Launchkey 61** (keys, 16 pads, 8 knobs), an **Akai MPC2000XL** (hardware sampler; Kaleidobox exports kits to its card format), an iPhone (with wired USB-C headphones), **Arturia V Collection** soft synths, Cymatics sample packs, a large sample library and a Splice folder. No working audio interface right now (the old one won't connect), so the Mac's built-in mic and output are what's used day to day.

---

## 2. The big picture: how the Mac app is laid out

### 2.1 The frame (same on every screen)

- **Left rail: the tools**, in the same order as the phone's tool row: **Loops, Drums, Pads · MPC, Sampler, Bass, Synth, Chords, Vocoder, Line in, Drone, Effects.** Each instrument is a *card* showing what it's playing, with its own Play, Mute and Solo. Picking a card makes the keyboard, knobs and Rec follow that instrument.
- **Four views (F1-F4):**
  - **Play**: the picked instrument's steps, patterns and 8 knobs. This is the jam surface.
  - **Edit**: that instrument's full panel.
  - **Song**: sections and layers over a bar ruler.
  - **Mix**: the mixer and effects.
- **Drawers:** **Sounds** (F5, the sample library), **Settings** (F6), **Chords** (F7), **Guide** (walkthrough and how-tos). There is also a Visuals window (F8).
- **Header:** the song name, a dot when there are unsaved changes, and an LCD showing "BAR › LAYER n" (where the next recording lands).
- **Bottom dock**, modelled on the phone's thumb bar:
  - The tempo box: click to tap, drag or scroll to change, right-click to **Ramp to** a new tempo.
  - **Click**, **Count-in** and **Length** for the next take.
  - **One big Rec**: a red pill that says where the take goes, with an arrow for the record options.
  - **Play**, **Undo** and **Redo**: one history of reversible steps, also Cmd+Z and Cmd+Shift+Z.
  - **To looper**: prints the picked instrument's part onto the next empty loop layer, then turns that instrument off so it doesn't double.
  - **Rec moves** and **Automation ›**, then **Quantize**.
  - On the right, the song's **section buttons** (keys 1-8 play them, **+** keeps what's playing as a new section).
- **Front page:** **My ideas**. It has:
  - **Catch an idea**: the mic goes to layer 1 of a new song, and the song takes the tempo you sang at.
  - "Continue" with a preview of the last song's layers.
  - **Tonight's spark**: a daily prompt with a tempo, a key and three tool steps, the same on Mac and phone.
  - Start from any tool, plus search and stars.
  - An animated kaleidoscope title at launch.

### 2.2 Inside Logic Pro

The same engine runs as plugins:

- **Kaleidobox**: the whole box as an instrument. It follows Logic's clock and has stem outputs.
- **Kaleidobox Input**: the same box as an effect, so it gets mic or guitar audio from Logic.
- **One plugin per tool** (MPC, Bass, Synth, Drums) with their own Rec and **Drag MIDI / Drag audio** chips.
- **Kaleidobox Vocoder**.

There is also **Record set** (stems + MIDI in real time), **Bounce** (song, section or loops; as a mix, each loop, or everything; WAV/AIFF/MP3), **To Logic** drags, and **Import a song from Logic** to perform it live.

### 2.3 The iPhone frame (Pocket)

- **Top:** the song name and an LCD (bar, and the layer the next take lands on).
- **Thumb bar** along the bottom: BPM/TAP, CLICK, a big **REC**, PLAY, **UNDO** (with REDO on the toast), and a slot that shows **ON/OFF** for the current instrument (or VISUALS).
- **Tool row** under it: **LOOPER, DRUMS, PADS, SAMPLER, BASS, SYNTH, CHORDS, VOCODER, DRONE, EFFECTS, SONG**, plus **+ ADD A LAYER** and **MORE**. Slide a finger along the row to pick a tool.
- **Every instrument has TO LOOPER.** It records that instrument's part onto the next empty layer for its pattern length, then switches the instrument OFF (part kept) so it doesn't play twice. UNDO reverses both.
- **Navigation:** a song drawer (your songs, sounds, record, Mac link, remote, settings, tour), press-and-hold menus on layers, pads and chord pads, and drag-down sheets.
- **Saving:** the phone saves on every change. Songs are folders, visible in the Files app.

---

## 3. The instruments, one by one

For each one: what it is, what it can do, how you play and record it, what moves over time (dynamics), and where it is thin. **The owner wants to keep making the instruments more dynamic**, so the "what moves" and "thin spots" lines are the most important part of this section.

### 3.1 Drum machine ("Drums")

**What it is.** A synthesised early-80s rhythm box (no samples by default), on Mac and phone, from the same engine.

**Sounds.**

- 12 voices: kick, snare, clap, rim, three toms, clave, cowbell, closed hat, open hat (choked by the closed hat), and cymbal.
- Each voice has alternate sounds (the toms become congas, the clap becomes maracas, etc.).
- **Ritual hand drums**, made by modal synthesis: frame drum, djembe bass and slap, bendir, tabla, udu, talking drum, log drum, temple bell, caxixi, ankle bells, gong.
- Factory kits include Classic, Deep sub, Punchy electro, Latin, Dirty, Tight and dry, Big room, three Ritual kits, and six newer kits shared by both apps.
- **Your own kits:** put any sample from your library on any drum voice. On the phone you can also map a pad kit onto the voices in one go.

**Per voice:** Level, Tune, Decay, Tone, Snappy, Pan, Mute.

**Patterns.**

- 16 patterns, each with A/B variations (A, B, or A then B).
- **Up to 64 steps** (four bars of 16ths), an accent row, and rolls of 2, 3 or 4.
- An intro pattern, fills (every 2, 4, 8 or 16 bars or on demand), and a chain of up to 32 patterns.
- Swing, Drive, Humanize.
- Step rates of 1/8, 1/16, 1/16T, 1/32 and 1/8T.
- A Feel menu: 12/8, 6/8, triplets, shuffle.

**Step tools** (shared by Mac and phone): Hit, Roll, Builds, Flam, Accent, Soft/Loud, Erase. Row and bar tools: shift, reverse, spread, copy a bar, double. **Note Repeat** for playing live.

**Pattern life (Mac only):**

- Per-hit **chance %**.
- **Conditions** (1:2, 3:4, Fill, First…).
- **Nudge** (±50% of a step).
- **Per-row length**, so rows can run different lengths (polymeter).
- **Euclid**, **Turn** (rotate), and **Mutate** (with undo).
- **Step locks**: per-hit pitch, decay and velocity.

**Playing and recording.** Tap steps, or play live from MIDI notes (C = kick, D = snare… in any octave) or the Launchkey pads. Then use Rec, or TO LOOPER (prints it to audio).

**What moves today:** chance, conditions, fills, mutate, swing and humanize, plus automation of knobs (Rec moves).

**Thin spots:**

- **The phone has none of the pattern life** (no chance, conditions, Euclid, mutate or locks).
- The Mac's pattern life is spread over hidden modes on the Play view.
- There is no per-step probability *of variation*, such as "this bar becomes B 20% of the time".
- Nothing evolves a pattern over many bars (slow builds, density curves).
- No ratchet curves beyond the 2/3/4 roll, and no micro-timing feel templates beyond the Feel menu.

### 3.2 Pads · MPC (drum sampler + sequencer)

**What it is.** A virtual Akai MPC2000XL, on Mac and phone.

**Sounds and pads.**

- 64 pads (4 banks of 16).
- Per pad: volume, pan, tune, play mode (One shot / Mono / Note off).
- A live sound per pad: Filter, Reso, Attack, Decay, and sends to the kit's Delay and Reverb.
- **16 Levels**: one pad's sound spread over 16 pads by Tune, Filter, Tune+filter, Velocity, Decay, **Attack**, or **In key** (the song's scale).
- **Make 16 pads** bakes those into real pads, with undo.
- Note repeat (1/8, 1/16, 1/32 and triplets).
- Chop a sample across pads, and "keep in time" to stretch loops to the kit's BPM.
- Export to the real MPC2000XL's card format (`.PGM`/`.SND`). This is untested on the real machine.

**Sequencer (Mac).**

- Sequences with their own tempo and length, up to 64 tracks, 96 ticks per beat.
- Rec with count-in, overdub, punch-in, timing correct with swing, a steps grid, undo/redo.
- MIDI in and out, and MPC Songs.
- Euclid and Turn on the grid.

**Phone.** REC PADS records a pad pattern (up to 512 hits). There is a pad sheet with sound, 16 LEVELS, copy to another pad, and the sample page, plus a sound bank sheet with factory kits.

**What moves:** note repeat while playing, and 16 Levels as a performance surface.

**Thin spots:**

- No chance, conditions or step locks on the MPC pattern, so it is the least "alive" drum source.
- MPC Songs are a separate song model from the app's Song.
- On the Mac, having both "Drums" and "Pads · MPC" means a beginner has to choose a drum engine at step one.

### 3.3 Bass

**Mac.**

- A deep mono **acid bass** engine:
  - Saw, square or triangle wave, Cutoff, Resonance, Env mod, Decay, Accent, Glide, Drive, Sub, Pulse width, Fat.
  - An LFO with depth, rate, shape and destination.
  - Filter types (Low 24, Low 12, Band, High), key track, attack, release.
- An **8 pattern × 16-step** sequencer (note, accent and slide per step; length 1-16; step rate; gate).
- The keys transpose the running pattern, or play the bass directly when it is stopped. There is also **Random**.
- Pattern life: chance, conditions, Euclid, Turn.
- Named sounds ("BX-1" style), and a **plugin slot**: any Audio Unit or VST3 instrument (e.g. Arturia) can replace the sound, with its own screen embedded.

**Phone.**

- A separate BASS tool using the *synth* engine with eight bass kinds: SUB, 808, FUNK, ACID, GROWL, ANALOG, PICKED, DIGITAL.
- Four knobs (TONE, SHAPE, DECAY, ECHO).
- Modes KEYS / PATTERN / ARP.
- A **polyphonic note pattern of 1, 2, 4 or 8 bars** (edited as 16 steps a bar), with snap, tie, quantize, and a scale mode (one note, chords, or piano).

**Thin spots:**

- **The two apps' bass works differently**: the Mac has an acid step sequencer with accent and slide, the phone a free note pattern.
- The Mac Bass tops out at 16 steps; longer lines need a recorded note take.
- There is no pattern life on the phone.
- There is no "bass follows the chord progression" or "bass follows the kick" behaviour.

### 3.4 Synth

**Mac.**

- An 8-voice analog-style poly synth ("PS-8"):
  - Osc 1 (saw, square, triangle, supersaw) with detune and octave. Osc 2 (wave, tune, level). Sub and noise.
  - Filter (24/12 dB low-pass, band, high) with its own envelope, key track and velocity.
  - Amp envelope, Drive, Width. Poly, mono or legato with glide.
  - An LFO to filter, pitch, volume or pulse width.
- A strong **arpeggiator**:
  - Modes: up, down, up-down, random, as played, chord, converge, pinky.
  - 1-4 octaves. Speeds from 8 bars to 1/32T or free in Hz.
  - Swing, repeats, chance, a 16-step on/accent rhythm, and latch.
- A plugin slot like the Bass.

**Phone.**

- SYNTH with eight kinds: LEAD, KEYS, PAD, PLUCK, ORGAN, STRINGS, BELLS, BRASS. Each has presets drawn from the Mac's sounds.
- Four knobs, KEYS / PATTERN / ARP modes, and the same 1-8 bar polyphonic **note pattern** as the phone bass.

**Thin spots:**

- **The Mac Synth has no note sequencer.** In the Song a Synth box can only choose "Keys" or "Arp". A written melody on the Mac has to come from a recorded note take, the chord progression, or live playing.
- **The phone already has a note pattern the Mac lacks**, which breaks the parity rule in reverse.
- The phone exposes only 4 macro knobs over the engine.
- There is no modulation matrix, no second LFO, no per-step parameter locks, and no morphing between sounds.

### 3.5 Vocoder

**What it is.** The owner's own vocoder, a separate project compiled into both apps. Its engine has:

- A carrier synth (saw, square, supersaw, pulse…).
- An arpeggiator (patterns like Eighths, Offbeats, Trance, Gallop, Stutter).
- Chords (major, minor and modes; seventh, ninth, sus, add9, sixth, power).
- **Auto-tune** (scales, melody mode), harmony, filter, effects, vintage character.
- The **Mic locker** (12 classic mic models, first in the chain).

**Mac.** A pedalboard of 13 pedals: Tune, Harmony, Robot, Character, Synth, Chords, Arp, Filter, Vintage, Dirt, Chorus, Echo, Space. Each has a lamp, one main knob and a footswitch, and clicking a pedal opens its full rack. There is a raw-voice section, Your voice / Robot faders, and a full-window view of every setting.

**Phone.** 14 pedals (adds Voice). It asks you to put on headphones before the mic opens (feedback warning, wired recommended over AirPods because of latency). There are MY VOICE / VOCODER / BOTH sources for recording, and the chord pads drive the carrier.

**Voice recording.**

- **"To a click"**: a normal timed take.
- **"Sing free"**: no click. The take is then fitted to the song's beat (stretched, pitch kept). On both apps.
- **Catch an idea** reads the tempo you sang at and builds the song around it.

**Thin spots:**

- It is the heaviest tool on the CPU, so it is off when unused.
- Its internal Filter, Dirt, Chorus, Echo and Space pedals share names with the general effects pedals.
- No formant or gender sweep as a performance gesture, no vocal "freeze/hold" pad, no vocal chop-to-pads in one step.

### 3.6 Sampler (sample editor)

**What it is.** A full sample editor, also its own app and plugin. You can:

- Record from any input, or open WAV, AIFF, MP3 or FLAC.
- Cut, copy, paste, trim, silence, trim quiet ends, snap to zero.
- Fades with 4 curves, reverse, normalize, gain.
- Apply destructive effects: pitch/speed, filter, compressor, drive, lo-fi, chorus, phaser, delay, reverb.
- **Play it across the keys** (While held / Loop / To the end).
- **Smart chop** (Find hits / Split evenly), then send the slices **To MPC pads**.
- Use 200 undo steps, and save to the library with tempo and key metadata.

On the Mac it is merged with the MPC on one screen. **Sample a tool** records any instrument into the sampler.

**Phone.** SAMPLER tool and a "RECORD A SOUND" recorder (Voice Memos style) with effects, FIND THE HITS, CHOP, SEND TO A PAD.

**Thin spots:**

- Effects are destructive only. There is no live, non-destructive effect chain inside the sampler itself; that happens on the strip.
- No slice sequencer.
- No granular or "play the sample as a texture" mode on the sampler (Grain exists as an effect pedal on the Mac only).

### 3.7 Chords (chord builder)

**What it is.** 16 chord pads. You can build them on the keys, catch them from what you play, or fill them from a key, scale and chord type.

**Voicing and playing.**

- Voicing: Lower, Higher, Oct, Spread, Tight. Hold and Strum.
- Played by click, the Launchkey pads, the lowest 16 keys, one-finger chords, or **Map keys** (every key becomes a chord in the chosen key and scale).
- Map keys layouts: scale notes, white keys I-vii, your pads, or the full keyboard. Options: smooth voicing, +Bass, octave.
- **Live key**: change key on the fly (12 big keys, octave ±2, change now / next beat / next bar).
- **Chords on/off**: flip between chords and plain single notes, live.
- **All off.**
- Sent to any instrument: vocoder, bass, synth, sampler, MPC, drum machine.

**Progression.** 8 steps, each a beat to 2 bars long, on the clock. It can go To loop, Rec MIDI, or To Logic.

**Phone.** A CHORDS tool with pads, progression, LIVE KEY, chords on/off, and an EDIT CHORD sheet (root, type, inversion, octave, mini keyboard).

**Thin spots:**

- The progression is only 8 steps with no variation.
- No voice-leading "next chord suggestions", no borrowed chords, no tension/release helper.
- Chords don't drive the bass or the drone automatically.
- No rhythm patterns for the chord stabs beyond strum and the progression's step lengths.

### 3.8 Drone box

**What it is.** The owner's own electronic tanpura, inspired by the Raagini.

**Sound.**

- An additive tanpura: four strings (Pa/Ma/Ni/Sa + Sa Sa + low Sa) with **jawari** bloom (the buzzing brightness sweeping down the harmonics after each pluck).
- **Reeds** (a shruti box).
- **Resonate**: sympathetic strings fed by any track or the mic, so you can sing into the strings.
- Instruments: Tanpura, Bowed, Swarmandal, Electric, Bell. Tone controls: Bright, Body, Detune, Space.
- 15 presets.

**Pattern.** Order (Classic, Reverse, Wander, Pairs, Roll), an 8 or 16-step grid, swing and human feel.

**Miracle tones.** 12 tone tiles (174 to 963 Hz, 432, 136.1 "Om", or your own frequency). The tone can be fitted over the drone, or the drone tuned to it. Sound options Pure / Warm / Bowl, binaural beats (2, 6 or 10 Hz), level, swell, and tone only.

**Thin spots (Mac):**

- No Song timeline lane, so it can't be placed in a section.
- Not a capture or Keys destination.
- Not on the clock in the same way as the others (it sounds whenever it is on).

On the phone, loop layers are mono, so recorded binaural tones become a soft pulse.

### 3.9 Line in

A guitar or second mic through the pedalboard: plug in, pick the input, gain, tuner, and Rec to the looper, sampler or a pad. Mac only. In Logic it needs the Input plugin.

### 3.10 Hosted plugins

Any AU or VST3 instrument can sit in the Bass or Synth slot (the steps, arp and keys then play it). Any effect plugin can go at the end of any strip or loop rack. The plugin scan runs in a helper so a bad plugin can't freeze the app, and plugin sounds are kept when shows are saved and reopened.

---

## 4. The looper, the song, and getting ideas down

### 4.1 Looper

**Mac:** 16 stereo layers, bar-locked, following the tempo.

- **Recording.** A take starts at the next bar and stops at the bar where you press again, or after 1-64 bars. Up to 60 minutes per layer. Overdub with undo.
- **Sources:** mic, vocoder, bass, synth, MPC, sampler, mix, line in, drums, drone.
- **Per layer:** level, mute, solo, and its own full effects rack.
- **Bends** (live): reverse, half speed, double speed, stutter 1/4, 1/8 or 1/16.
- **Editing:**
  - The loop editor: trim snapped to bar, beat or 1/16.
  - The Shape row: slip, nudge, gain, fades, reverse.
  - **Quantize** a loop's hits onto the grid, Flex-style.
  - **Loop Lab · Warp**: hits and warp markers, so a loop is fitted onto the beat.
- **Loop banks:** up to 64 named sets of all layers, switched at the next bar.
- **Import** a loop (stretched) or a whole song (sets the tempo).
- All loops share one bar timeline.

**Phone:** **8 mono layers.**

- LOOPER screen with a layer panel: DUB, DUB OVER IT, MUTE, LEVEL, PAN.
- The loop edit sheet:
  - IN/OUT with snap, TRIM, **FIT BARS** (stretch), **WARP**, BEAT MATCH, **QUANTIZE**.
  - HALF / DOUBLE / REVERSE, FADE IN / OUT, SEND TO A PAD.
  - EFFECTS per layer, with **REC MOVES** (touch automation of 5 knobs that replays every pass).
- **Recording keeps looping until you press Stop**, and each pass is a take.

**Thin spots:**

- The phone's 8 mono layers vs the Mac's 16 stereo, and the phone has no loop banks, no live bends and no solo.
- Loop Lab has only Warp built. Slice, Tune and Play were designed but not built.
- On the Mac, learnable MIDI controls for loop Rec and Mute cover layers 1-8 only.

### 4.2 Song

**Mac.**

- The **Song** view: sections to scale over a bar ruler, with only the tracks you use showing.
- Each box names what plays (a pattern, a loop, an MPC sequence). Click a box for its side panel; double-click to open that instrument on that pattern.
- "**Add a layer with**" buttons for each instrument.
- A **Bars view** for fine edits: split, slip, Alt-drag copy, level, and effect-move rows under the lanes.
- **Rec into** a section with Cycle (each pass is a take, and you cycle through takes). **Note takes** for Bass, Synth, Sampler, Drums and MPC stay editable notes in a piano roll.
- **Rec live**: launch loops in and out while playing, Mark new sections, punch in with a 2-bar run-up, keep or cancel. Each run is a new take, never overwriting.
- Delete anything in one click, with Undo.
- **Friend's track**: bring in a friend's mixdown, detect its tempo, bar 1 and key, practise over a bar range, and send your parts back lined up from 0:00.

**Phone.**

- A SONG tool with lanes of blocks per layer (fades, level, slip, split), takes, REC LIVE tiles, MARK, PUNCH, REC FROM END.
- TO SONG PART, a friend's TRACK row, EXPORT, SEND BACK.

**Thin spots:**

- One show is one song (no setlist).
- Sections switch at the next bar only (no beat or 2/4-bar launch options).
- No section morph (gliding settings from one section to the next).
- The time signature is effectively 4/4: it isn't saved and there is no control for it.
- Some older song models still exist in the code (legacy scenes, MPC songs), though they are mostly hidden.

### 4.3 Recording paths (how sound gets captured)

There are many: the dock's Rec (target chosen in its arrow menu: next layer, a song take, a sampler pad, the whole set as stems), TO LOOPER, Rec into a section, Rec live, Sample a tool, Record set, the MPC sequencer's own Rec, the chord builder's Rec MIDI, Rec moves (automation), the line in's Rec buttons, and Catch an idea.

The Mac has been converging on **one Rec with a visible target**. That work is not finished, and some screens still carry their own Rec buttons.

### 4.4 Undo and Redo

- **Phone:** one history of the whole song. It takes snapshots of the song whenever a change settles (a pad hit, a knob, a layer, ON/OFF, the tempo). UNDO and REDO step through everything in order.
- **Mac (new, PR #135):** a 64-step journal. Undo/Redo covers loop takes, overdub, clear, clear all, To looper, Sing free fit, song edits, Play view clear/mutate, quantize, automation, sampler pad operations, and chord edits.
  - **Knob turns and per-step pattern edits are not yet in the Mac history** (that is stage 2).
  - The piano roll and the MPC sequencer and drum-machine panels still have their own separate undo.

### 4.5 Saving

- **Phone:** saves on every change.
- **Mac:** Save / Save As, a dot beside the name when there are unsaved changes, and one Save / Don't Save / Cancel prompt when you replace a show. Saving is transaction-safe.
- **There is still no periodic autosave or crash recovery on the Mac.**

---

## 5. Effects, mixing and performance moves

### 5.1 Effects

**The strip rack** sits on every instrument and every loop layer, in this order:

> Gain in, Mic (mic models), Gate, Comp, Wah, Filter (DJ one-knob), Drive + Crush, Amp (Clean, Crunch, Lead, Fuzz, Bass amp, with cabinet), EQ, Chorus, Phaser, Tremolo, Delay (tempo-synced), Repeat (beat repeat), Reverb, **Grain** (cloud / freeze), then a plugin slot.

- Every setting can be learned to a knob, automated, or put on a macro.
- The rack is shown as a **pedalboard** (Effects tool) with presets (Clean guitar, Funk wah, Dub delay…) and your own presets, copy/paste.
- **Every sound is visible at once**: 8 instrument chips plus layers 1-8, ALL, and MIX.
- The phone has the same pedalboard (13 pedals, **no Grain**).

Other effect places:

- Mixer sends (shared Reverb and tempo Echo).
- The vocoder's own pedals.
- MPC per-pad effects.
- The sampler's destructive effects.

### 5.2 Mixer (Mac)

One strip per tool: on/off, FX, two sends, pan, mute, solo, fader with meter, Learn, and Own out (a separate output in Logic). There are Reverb/Echo returns and a master. A **Loop layers** page shows the 16 loop strips. Safety limiters (−0.3 dBFS) sit on the master, the output and each stem.

### 5.3 Performance moves

- **FX pad** (Mac) / **MOVES** (phone): an XY pad over the whole mix with Filter, Repeat, Tape stop, Gate, Riser, Drop (returns on the next bar), Freeze, Grain, Reverse. On the phone it has a HOLD switch.
- **8 Macros** (Mac), each driving up to 12 settings. You assign by touching the settings.
- **Automation** (Mac):
  - Rec moves records any smooth setting from the next bar. Up to 64 lanes.
  - Lanes loop or play once along the song, with draw, halve/double, clear and undo.
  - **Move** makes an LFO-shaped lane (sine, triangle, ramps, square, random) on the last touched setting.
  - Switches (patterns, on/off) are never recorded.
- **Tempo ramp** and section jumps (keys 1-8), mutes and solos, loop banks, bends.
- Pop-out windows for a second screen.
- **Visuals**: an audio-reactive picture with 5 looks (Kaleido, Tunnel, Liquid, Bloom, Trails) plus Drift (a new look each 8-bar phrase), colour palettes, and notes in colour. It can run in a window, full screen, or on a second screen. Untested on real hardware.

**Thin spots:**

- Performance moves act on the whole mix, not per instrument or per layer.
- The phone has no macros and no live loop bends.
- Automation lanes are not tied to song sections.
- There is no recorded lane of live actions (section jumps, mutes) to replay a performance.
- No scene/snapshot morph.

---

## 6. Moving between tools: how the flows actually feel

### 6.1 The core loop, Mac

1. Pick a tool on the rail. The keys, the 8 knobs and Rec now follow it.
2. Jam on **Play**: light steps, pick a pattern, play keys or pads.
3. Capture: **Rec** (lands on the next loop layer by default), or **To looper** to print the part and switch the instrument off.
4. Build: add more layers from other tools. In **Song**, use "Add a layer with", Rec into sections, or Rec live.
5. Shape: Edit for the full instrument, **Mix** for levels and effects, Quantize, Warp, the loop editor.
6. Perform: section keys, FX pad, macros, bends, vocoder.
7. Out: Bounce, Record set, or play inside Logic as a plugin.

### 6.2 The core loop, phone

1. **New song** (or Catch an idea, or Tonight's spark).
2. Pick an instrument from the tool row and play it.
3. Press **TO LOOPER** (or REC) and it becomes a layer, while the instrument turns OFF.
4. Repeat with another tool, or sing (to a click, or free).
5. EDIT a layer (fit, warp, quantize, effects + moves), or build the SONG.
6. MOVES for live drama, then EXPORT (WAV/AIFF/MP3, mix or stems) or SEND TO MAC.

### 6.3 Between the two apps

- The **Mac link** is Bonjour plus a 4-digit pairing code. It sends **audio only**: a sound to the library, a pad, the sampler, a loop, or a kit.
- There is a phone **Remote** for the Mac: play, stop, bpm, section, next/back, mute, rec.
- **A phone song does not open on the Mac as a song**: there are no patterns, notes or sections, only loose audio.
- Pocket shows where to get the Mac version.

### 6.4 Where flow breaks or slows (observed in code and from the owner's history)

1. **Many ways to record**, some with hidden behaviour.
2. **Effects live in several places** with overlapping names.
3. **Choosing between Drums and Pads · MPC** at the first step.
4. **Song vs Jam control** can make an instrument seem unresponsive when the song is driving it.
5. **Keys 1-8 change meaning by screen** (sections vs loop rows).
6. **Two editors for the same drum grid** (Play vs Edit).
7. **Phone and Mac instruments don't match exactly** (bass engine, synth note pattern, pattern life, looper size).
8. **The owner's tests so far:** the phone redesign and TO LOOPER work; "access everything from everywhere" is his latest wish (no hidden or side-scrolled choices).

A non-musician test is planned: the owner's wife will try Pocket as-is ("sing an idea and loop it"), and the spots where she gets stuck get fixed. A Start screen is built only if she is lost from the beginning.

---

## 7. What is strong (keep and build on)

1. **One sample-accurate clock** for every tool, with no allocation on the audio thread. It is solid and expensive to rebuild.
2. **The looper**: bar-locked, tempo-following, long layers, overdub/undo, bends, banks, quantize, warp.
3. **Takes and note takes**: jamming turns into composing without losing editability.
4. **TO LOOPER + ON/OFF**: the fastest "make it a layer" gesture, now on both apps.
5. **Unusual, on-brand instruments**: ritual hand drums with pattern life, the Drone box with Resonate and Miracle tones, the vocoder with the Mic locker.
6. **Chords with Map keys and Live key.** The owner called chords on keys "a big deal".
7. **Logic round trip** both ways, and **MPC2000XL file compatibility**.
8. **Catch an idea** (the song follows the tempo you sang at) and **Sing free**.
9. **A consistent visual identity** on each platform, and a large automated test suite.

---

## 8. Where the instruments could become more dynamic (openings, not decisions)

These are areas where the code shows the instruments are currently static or uneven. They are prompts for your thinking, not a plan.

**A. Parity of "life" across instruments**

- Pattern life (chance, conditions, nudge, polymeter, Euclid, mutate, locks) is deep on the Mac drum machine, partial on the Mac Bass and MPC, and **absent on the phone**.
- How should "life" be presented so it is one consistent idea on every step grid and on a thumb-sized screen?

**B. A real melody sequencer for the Mac Synth**

- The phone's 1-8 bar polyphonic note pattern could be the shared model for both Synth and Bass, keeping the Mac Bass's accent/slide acid grid as a mode.
- How should song boxes pick melodies?

**C. Movement inside a sound**

- Per-step parameter locks for Bass and Synth (as the drum machine has).
- A second LFO or simple modulation slots.
- Sound morphing (A↔B preset crossfade on one knob).
- Envelope-follower links ("kick ducks the pad", "voice opens the filter").

**D. Instruments that listen to each other**

- Bass that follows the chord progression or the kick.
- The drone that follows Live key.
- Drums whose fills respond to section changes.
- The vocoder carrier following the bass.
- Resonate already does this for the drone: what is the general version?

**E. Evolving over time**

- Slow builds over 8-64 bars (density, filter, chance curves).
- Automation lanes tied to sections.
- Section morph.
- "Mutate a little every N bars".
- How can this stay controllable live rather than random?

**F. Per-instrument performance moves**

- MOVES / FX pad currently affects the whole mix.
- Per-instrument or per-layer XY moves, repeat/stutter on one instrument, and freeze on one instrument.
- The phone could get bends and macros.

**G. Voice as an instrument**

- Sing free and Catch an idea exist.
- Next: vocal chops to pads in one step, sung melody to notes (for Synth/Bass), harmony layers, vocal freeze, and Loop Lab Tune (auto-tune following Live key on a recorded take).

**H. The phone's layer model**

- 8 mono layers limit stereo effects, binaural tones and the Mac round trip.
- What does "more layers" look like on a phone without clutter?

**I. Controller depth**

- Launchkey pads and knobs follow the picked tool.
- Learn covers loops 1-8 only.
- What would a "performance layout" for the Launchkey 61 be: drum pads for sections, knobs for macros, keys for chords?

---

## 9. Questions for you (the assistant)

1. **Flow.** Which three changes would most shorten "idea → layer → song" on the phone, without hiding depth? And the same on the Mac.
2. **Instruments.** For each instrument, what is the single most musical way to make it more dynamic and alive, in keeping with tribal, ritual, minimal dance music? Which ideas from section 8 matter most, and what's missing?
3. **Consistency.** How should "life", quantize, TO LOOPER and undo look and behave identically across all instruments and both apps?
4. **Live show.** What would make a set on this rig feel "life-changing" to an audience, using the looper, drone, vocoder, ritual drums and visuals? What performance controls are missing?
5. **Learning curve.** How can a non-musician find their way (the wife test) while a pro keeps full depth, without an Easy mode?
6. **Interface.** Where should new controls go so that every visible button does something and nothing important is buried or side-scrolled?
7. **Mac ↔ phone.** What should travel from a phone song to the Mac (layers, notes, sections, sounds), and how should it feel to the user?
8. **Keyboard shortcuts** on the Mac (being worked on separately): beyond Cmd+Z / Cmd+Shift+Z, which shortcuts matter most for fast desktop work?

---

## 10. In progress, not on main yet (upcoming)

- **Quit protection for saving (PR #137):** a Save / Don't Save prompt on quit, reopening the show you were on at relaunch, and the unsaved dot. Discarded edits are never restored.
- **Phone:** a permanent REDO button on the thumb bar.
- **Phone:** a pre-record option (catch what you played just before pressing REC).
- **Phone:** note editing of recorded parts.
- **Mac Undo/Redo stage 2:** knob turns and per-step pattern edits in the one history.
- **Drum triggers (PR #95, open):** hit-to-learn trigger mapping and firmware for a DIY drum trigger box.
- **Designed, not built:** Loop Lab Slice / Tune / Play.

---

## 11. Status honesty

Much of this was built in the last two weeks. **Confirmed by the owner on real hardware:**

- The whole-box plugin in Logic, the vocoder, the sampler (phone).
- The phone redesign and TO LOOPER.
- The phone mic with wired headphones.

**Built and tested automatically, but not yet tried hands-on:**

- Most of the Mac deep-show features (pattern life, ritual drums, Drone, Miracle tones, Grain, bends, macros).
- Visuals, Friend's track, Catch an idea tempo, Mac Undo/Redo, the Mac's TO LOOPER / Sing free / 16 Levels additions.
- MPC card export on the real 2000XL.

Suggestions that depend on these features working as described should note that.
