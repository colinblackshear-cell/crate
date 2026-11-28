# Kaleidobox master development roadmap (revision 2, amended)

*Accepted by the owner on 2026-11-28 as the authoritative product and development specification for Kaleidobox. It lives in the repository at `docs/ROADMAP.md`. Comprehensive planning is complete: the roadmap is not expanded and not rewritten. It is updated in place as work is completed and as musical testing informs decisions, and work proceeds one small approved PR at a time. Amendments are marked **[AMENDED 11-28]**. Revised 2026-11-28 after the owner's review. Checked against the code on `main` of the `crate` repository (commit 22e4545, through PR #136). Planning only: no code was changed. Companion to `docs/KALEIDOBOX_TOOLS_ANALYSIS.md`, which describes every tool.*

**The aim.** Make Kaleidobox the most dynamic, expressive and intuitive instrument for creating and performing music that it can be. Joining up the code is a means to that, never the goal.

**Product principles (protected).** Every new advanced capability must preserve the shortest existing path from inspiration to recorded music. Simply Advanced means: immediate capture, effortless layering, expressive playing, musical exploration, reversible experimentation, continuous performance. No new mandatory configuration step is ever added to an existing simple workflow. All existing features and musical workflows remain protected. From here on, small tested improvements and real musical experiences come before more planning documents.

**How status is recorded.** Four separate states, never merged into one "done":

| State | Meaning |
|---|---|
| **Implemented** | The code is merged to `main`. |
| **Automatically tested** | Automated tests cover it and pass in CI. |
| **Hardware validated** | It has been run on the real Mac, iPhone or controller and behaved correctly. |
| **Musically accepted** | The owner has used it to make music and accepted it. |

A feature is complete only when it is musically accepted. Passing automated tests alone never marks it complete. The status log at the end of this document records where each roadmap item stands.

**Standing priorities.** Musical usability, reliability and uninterrupted playback come first. All existing features, workflows and instrument identities are preserved.

**Labels on facts.** **[CHECKED]** = I looked at the code today. **[CARRIED]** = from earlier verified documents, not re-checked line by line. **[PROPOSAL]** = a design that is not built.

**Labels on work.** Every roadmap item carries one of these:

| Label | Meaning |
|---|---|
| **REUSE** | The engine part exists. The work is wiring, screens and tests. |
| **PREP** | Needs a shared structure put in place first (a move into shared code, an identity, a schema). Little or no new sound code. |
| **NEW** | Substantial new engine work, with real-time, CPU or memory risk to manage. |

---

## Summary

1. **Decisions recorded.** Everything the owner approved is listed below and built into the plan.
2. **What changed from revision 1:**
   - "Live layers" are now called **editable layers**. True live layers and several copies of one instrument are a later investigation, and the project format is designed not to block them.
   - **LIFE** gains evolution amount, duration and six behaviours, six actions, and a layered model of what controls a setting (amended: layers that can combine, not permanent ownership).
   - **Chance** gets three behaviours: Repeatable (default for new projects), Evolving and Free. Existing projects keep Free.
   - The **phrase clock** is added beside the existing timing. It does not replace any of it.
   - **Portable projects** move up to Phase 2, with stable identities defined before LIFE and CONNECT add data.
   - **CONNECT** is designed around seven musical roles and six signal types.
   - Three areas are restored as full phases or tracks: **creative sound transformation**, **expressive sound architecture** and the **sound library**.
   - The **Stage interaction model** is designed now and built in pieces.
   - The roadmap has **eight phases (0 to 7)**.
3. **How much is reuse.** Of 76 roadmap items, 42 are REUSE, 20 are PREP and 11 are NEW (the rest are design steps and hardware sessions). The NEW work is LIFE's engine stage and its melodic rules, 16 stereo layers on the phone, sung melody to notes, the granular instrument, section transitions and the gapless setlist.
4. **Next step.** Finish and accept quit protection (PR #137). Then autosave with crash recovery (section 17), approved as the next PR. No later phase starts until authorized.

### Owner decisions recorded (2026-11-28)

| Decision | Status |
|---|---|
| Reliability, quit protection, autosave, crash recovery and unified undo come first | Approved |
| Preserve all instruments, features and workflows | Approved |
| Shared note-pattern architecture, keeping the acid bass sequencer | Approved |
| Both bass modes eventually on both platforms | Approved |
| 16 stereo Pocket layers, subject to real-device testing; layout not fixed to two pages of eight | Approved |
| Versioned `.kbox` project package | Approved |
| Real-hardware validation continues | Approved |
| Unrelated-feature freeze until quit protection is accepted | Approved |
| Recipe-based editable audio layers now; multi-instance instruments investigated later | Approved |
| LIFE amounts Density, Variation, Humanity, Movement, Tension | Approved for prototyping |
| Deterministic chance as the default | Approved |
| Curated CONNECT relationships first | Approved |
| Stage: design the interaction model now, build the workspace later | Approved |
| Revision 2 as the master roadmap; no further full revisions | Approved 11-28 |
| Autosave and crash recovery as the next PR after #137 is accepted | Approved 11-28 |
| Existing projects keep Free chance; new projects default to Repeatable | Decided 11-28 |
| Parameter control is layered and able to combine later; exclusive ownership only as a first implementation where necessary | Decided 11-28 |
| "Editable layer" and "Keep" as the names | Approved 11-28 |
| Real-phone mockups before choosing the 16-layer Pocket layout | Approved 11-28 |
| MPC2000XL export of round-robin pads uses the first sample, with a clear warning | Approved 11-28 |
| The drum machine is the first LIFE prototype; musical approval required before any other instrument | Approved 11-28 |

---
## 1. Verified inventory: what already supports each initiative

### 1.1 CAPTURE

| Exists today | Where | Status |
|---|---|---|
| One Rec in the Mac bottom bar with a visible target (next layer, song, sampler or pad, whole set, MPC pattern) | `RigView::RecLands`, `Dock` | [CHECKED] |
| TO LOOPER on both apps, with the instrument switching off afterwards | `RigView::pressToLooper`, Pocket tool screens | [CHECKED] |
| Catch an idea (the song takes the sung tempo) and Sing free | `core/IdeaTempo`, `RigLoopLab::fitToBeat` | [CHECKED] |
| Takes into a song section with Cycle; note takes that stay notes | `RigTakes`, `RigNotes` (Mac only) | [CHECKED] |
| Count-in and length for the next take | `Dock`, `RigLooper::Layer::countIn`; Pocket `CountInPanel` | [CHECKED] |
| Recording keeps looping until Stop, each pass a take | Pocket looper | [CHECKED] |
| Phone saves on every change | `PocketApp::saveSong` | [CHECKED] |
| **Missing:** capture of what was just played before Rec was pressed | neither looper keeps a rolling buffer | [CHECKED] (phone version in progress in another thread) |
| **Missing:** Mac autosave and crash recovery | no code in `Main.cpp` or `RigDirty` | [CHECKED] |

### 1.2 PLAY

| Exists today | Status |
|---|---|
| Every instrument on the Mac rail and the phone tool row, same order | [CHECKED] |
| Keys, pads and 8 knobs follow the picked instrument (Mac) | [CARRIED] |
| Navigation never stops the clock on either app | [CARRIED], and it is covered by the phone stress test |
| Hosted plugins in the Bass and Synth slots (Mac) | [CHECKED] |

### 1.3 EVOLVE (the basis for LIFE)

| Exists today | Where | Status |
|---|---|---|
| Shared life vocabulary: chance, 14 conditions, nudge, Euclid, step locks | `core/synth/PatternLife.h` | [CHECKED] |
| Drum machine pattern with per-hit chance, condition, nudge, per-row length, pitch, decay and velocity locks, up to 64 steps | `crate::drums::Pattern` | [CHECKED] |
| Bass step with chance and condition | `crate::bass::Step` | [CHECKED] |
| Shared step tools used by three screens (Mac panel, Mac Play view, phone) | `tools/drums/StepTools.h` | [CHECKED] |
| Mutate with undo, Feel menu, fills every N bars, A/B variations, chain | drum machine | [CHECKED] |
| Automation lanes and Move (LFO-shaped lanes), 8 macros × 12 targets | `RigAutomation` (64 lanes), `RigMacros` | [CHECKED] |
| **Missing:** any life features in Pocket's screens | zero matches for chance or Euclid in `tools/pocket` | [CHECKED] |
| **Missing:** repeatable chance | `chanceRandom.nextInt` in the drum and bass engines | [CHECKED] |
| **Missing:** a phrase concept | only the Visuals "8-bar phrase" drift and Rec live's phrase start | [CHECKED] |

### 1.4 CONNECT

| Exists today | Where | Status |
|---|---|---|
| Drone Resonate strings fed by a chosen instrument (8 feeds) | `DroneProcessor::Feed` | [CHECKED] |
| Drone Follow keys and "Use the chords' key" | `DroneView` | [CHECKED] |
| Chords sent to any instrument; Live key; chords drive the vocoder carrier | `RigChords` | [CHECKED] |
| An envelope follower already exists inside the Wah pedal | `StripFx` | [CHECKED] |
| The song's key shared by Synth, Chords and Vocoder on the phone | `PocketKey` | [CHECKED] |
| **Missing:** any general "this listens to that" link; no ducking, no bass following chords | | [CHECKED] |

### 1.5 COMPOSE

| Exists today | Where | Status |
|---|---|---|
| The Mac song: 23 lanes of blocks plus section markers | `RigTimeline`, `rigsong` | [CHECKED] |
| Rec live, punch, takes, Bars view editing | `RigLiveSong`, `TimelineView` | [CHECKED] |
| The phone song: blocks per layer with fades, level, slip; takes | `PocketSong` | [CHECKED] |
| Notes that stay notes in the song, with a piano roll | `rignotes::Clip`, `NoteEditor` (Mac) | [CHECKED] |
| **Phone layers already keep their recipe**: the notes or pad pattern, the sound, the knobs and the effects board they were printed from | `pocket::NotePart` on `LoopTake::part`, plus `LoopTake::tool` | [CHECKED] |
| Mac layers keep only which source they were recorded from | `RigLooper::Layer::source` | [CHECKED] |

### 1.6 PERFORM

| Exists today | Status |
|---|---|
| FX pad / MOVES with 9 modes over the whole mix, both apps | [CHECKED] |
| 8 macros, tempo ramp, section keys, loop banks (64), live loop bends (Mac) | [CHECKED] |
| Learnable controls: levels, mutes, loops 1-8 record and mute, Live key, Note Repeat, Chords on/off, next and previous section | `RigRouting::Control` [CHECKED] |
| Pop-out windows for a second screen; Visuals | [CARRIED] |
| **Missing:** launch timing other than "next bar", per-layer moves, snapshots, morph, setlist, a recording of live actions, controller banks | [CHECKED] |

### 1.7 Sound transformation, expressive sound and the library

| Exists today | Where | Status |
|---|---|---|
| Find the hits in a sound; time-stretch with pitch kept; pitch shift | `core/SoundFx` | [CHECKED] |
| Warp a loop onto the beat with markers | `core/Warp`, Loop Lab on both apps | [CHECKED] |
| Play a sample across the keys (16 voices; hold, loop, to the end, slices) | `core/KeyboardSampler`, used on the Mac only | [CHECKED] |
| Chop to pads | Sampler on both apps | [CHECKED] |
| Grain cloud with freeze | `core/GrainCloud`, an effect pedal and an FX pad mode on the Mac | [CHECKED] |
| Auto-tune and harmony, live | The vocoder engine | [CARRIED] |
| Tempo and bar-1 detection; key and section detection for a whole track | `core/IdeaTempo`, `core/TrackAnalysis` | [CHECKED] |
| LFO-shaped automation (Move); 8 macros; an envelope follower inside the Wah pedal | `RigAutomation`, `RigMacros`, `StripFx` | [CHECKED] |
| Library browser: kinds, tempo and key from names, search, BPM search, hear on click, send to pad, sampler or loop | `core/SoundBank`; phone `PocketLibrary` with tags | [CHECKED] |
| **Missing:** a pitch-to-notes tracker in the shared code (the vocoder has its own detector for auto-tune) | no match in `core/` | [CHECKED] |
| **Missing:** round-robin or velocity layers on pads; a slice sequencer; A/B sound states; favourites or collections for sounds | | [CHECKED] |
| **Missing:** a CPU or dropout meter | no usage code found | [CHECKED] |

---

## 2. Conflicts, duplicated models and dependencies

### 2.1 Duplicated models [CHECKED]

| Thing | How many versions | Detail |
|---|---|---|
| **Note lists** | 3 | `rignotes::Note` (Mac song takes, beats). `pocket::SynthNote` (phone patterns, beats). `seq::Note` (MPC, 96 ticks per beat, pad numbers). |
| **Pattern models** | 7 | Drum machine pattern. Bass 16 steps. Synth arp rhythm. MPC sequence. Phone note pattern. Phone pad pattern. Drone steps. Plus the chord progression. |
| **Bass** | 2 | Mac: acid engine with a 16-step accent/slide grid. Phone: the synth engine with eight bass kinds and a free note pattern. |
| **Loopers** | 2 | Mac: 16 stereo layers, `RigLooper`. Phone: 8 mono layers, `pocket::LoopsEngine`. Separate code. |
| **Song models** | 6 | Mac timeline (the real one). Legacy scenes, **still checked on every audio block** and still saved. MPC songs. Drum machine chain. Chord progression. Phone arrangement. |
| **Undo** | 2 designs, about 14 stacks | Phone: whole-song snapshots. Mac: a journal of reversible steps. Plus separate stacks listed in section 11. |
| **Project formats** | 2 | Mac: one XML show file plus a sibling "loops" folder. Phone: a folder with `idea.json`, `song.json`, `loops.json`, kits. The `.kbox` package agreed on 2026-11-22 is **not built** (no code mentions it). |

### 2.2 Architectural conflicts with the brief

1. **One instance per instrument.** There is one drum machine, one bass, one synth. Several live copies playing at once would be a large engine change. This shapes the layer design in section 4.
2. **Chance is not repeatable** (running random numbers). "Deterministic variation" and "return to the original" need each decision to be a pure function of a seed and the position in the song.
3. **Recorded notes sit at fixed bars.** Section edits move blocks but not note clips or take sets [CHECKED: `RigSong.cpp` never touches them]. Any "improvisation becomes arrangement" feature builds on this, so it must be fixed first.
4. **The unsaved-changes tracker must not be fooled.** The Mac decides whether a show has unsaved changes by comparing what the user authored against the saved state, ignoring what playback writes. LIFE and CONNECT change sound while playing, so their running state has to go through the same "playback, not authored" path or the unsaved dot will light by itself.
5. **Order of sound processing.** An instrument that listens to another (kick ducks the pad) needs the listened-to sound first. Where the order cannot give that, the link is one audio block late (a few milliseconds). Loops (A listens to B listens to A) must be refused when the link is made.
6. **One-tool plugins in Logic have no other instruments.** Relationships have to switch off cleanly there, and say so.
7. **Hosted plugins exist only on the Mac.** A shared project needs a rendered sound for the phone.
8. **Hand-kept parallel lists.** Strips, lanes, stems, sources and learnable controls are separate lists in separate files. That is why the Drone has no song lane [CHECKED] and why controller learn stops at loop 8 [CHECKED]. Every new shared system adds another list unless one table describes each tool.
9. **Large central files.** `RigView.cpp` is 6,122 lines and `PocketApp.cpp` 6,731. I do not recommend a broad refactor. New systems should live in new small files with their own tests, as `RigHistory` and `RigDirty` did.

### 2.3 Dependencies

- LIFE needs the chance behaviours, the phrase position and stable identities.
- LIFE on Synth and Bass needs the shared note pattern on the Mac.
- Live layers on the Mac need the layer recipe, which needs the shared note pattern.
- The shared project needs the layer recipe, the section-edit fix and the time signature.
- The shared project touches saving, so it must wait until the quit-protection work (PR #137) is merged and accepted.
- Stage features need launch timing in the engine; performance recording needs the action list that Undo stage 2 also needs.

---

## 3. Patterns, chance, phrase timing and LIFE [PROPOSAL]

### 3.1 Three tiers; the authored pattern is never touched

| Tier | What it is | Change needed |
|---|---|---|
| **1. The authored pattern** | What the musician wrote. Each instrument keeps its own shape (the drum grid stays a drum grid, the acid bass grid stays, the MPC stays at 96 ticks for card export). | One shared note pattern (3.2). |
| **2. Event detail** | Per-event chance, condition, nudge and locks. Exists in `PatternLife.h`. | Add a **protected** flag. Bring the fields to every pattern kind and to the phone. |
| **3. LIFE** | Amounts and behaviours that vary what is played **while it plays**. | New. |

LIFE, CONNECT and Transform read authored material. They never write to it. The only way a variation becomes authored is the musician pressing **Keep**.

### 3.2 One shared note pattern

Move the phone's note pattern (polyphonic notes over 1 to 8 bars, edited as 16 steps a bar) into the shared code, with the event-detail fields on each note.

- The **Mac Synth** gets note patterns that Song boxes can choose.
- The **Mac Bass** gets it as a second mode. **The acid grid with accent and slide stays.** The phone later gets the acid grid as its second mode.
- A Mac song note take and a pattern convert both ways.
- The MPC keeps its own sequence format, with a converter.

Shared editing words (Hit, Roll, Flam, Accent, Soft/Loud, Erase, shift, reverse, copy a bar, double) mean the same thing on every grid. **Each instrument keeps its own panel, look and playing surface.** A shared vocabulary does not mean identical screens.

### 3.3 Chance: three behaviours

Today a 50% hit uses a running random number [CHECKED], so no two plays match. That stays available.

**[AMENDED 11-28] Existing projects keep Free**, so they sound as they were made. **New projects default to Repeatable.** The behaviour is stored in the project.

| Behaviour | What you hear | How it is decided |
|---|---|---|
| **Repeatable** (default for new projects) | The same bar always plays the same way. | From the seed and the event's position. |
| **Evolving** | Each time round is different, but the whole journey can be replayed exactly. | From the seed, the position, and a count of passes or phrases since Play. |
| **Free** (kept by existing projects) | Never the same twice. For spontaneous playing. | A running random number, as today. |

**Capturing what actually happened.** Every engine reports the events it really played (after chance, LIFE and CONNECT) to a log. The Mac's set recorder already has a channel like this for played notes. That log is what **Keep** writes from, and later what a recorded performance stores. So even a Free run can be kept after the fact.

**Two levels of "the same".**

| Level | Promise |
|---|---|
| **Same musical events** | Which notes and hits, when, and how hard. Guaranteed in Repeatable and Evolving, and testable automatically. |
| **Same audio, sample for sample** | **Not promised.** Hosted plugins (Arturia and others) have their own free-running parts. Reverbs, grain clouds, analog-style drift and humanize also differ run to run. |

**[AMENDED 11-28] Two kinds of export.**

| Export | What it renders |
|---|---|
| **The composition** | The authored song. Its patterns are generated again under their chance behaviour. In Repeatable and Evolving this gives the same events every time. In Free it gives a fresh run. |
| **A captured performance** | The events that were actually played, read back from the log. Nothing is generated again, so a Free or Evolving performance comes out with exactly the events that were heard. |

The export sheet names which one it is doing. Either way, the events match but the audio is not bit-identical where plugins or random-phase effects are involved.

### 3.4 Phrase timing: added beside what exists

Three positions are available to every feature, side by side:

| Position | Counts | Used today by |
|---|---|---|
| **Pattern-relative** | Times this pattern has come round since Play | Conditions (1:2, 3:4, First), polymeter rows, the drum fill's "every N" |
| **Section-relative** | Bars since the section's top; passes of this section | Song playback, Rec into |
| **Phrase** (new) | Which musical phrase, how far through it, bars to the next section. Length 4, 8, 16, 32 or 64 bars; restarts at a section top. | New features only |

**Nothing existing changes its meaning.** Conditions stay pattern-relative. Drum fills keep counting their own pattern. Polymeter rows keep their own lengths. The Visuals drift stays on its 8 bars. Before the phrase position is added, tests pin down today's behaviour of all four so a change would fail the build.

The phrase position is calculated from the song position, so it is sample-accurate and has nothing to drift. LIFE, CONNECT roles, transitions and builds use it. Later, a fill or the Visuals may be given an optional "follow the phrase" switch, off by default.

### 3.5 LIFE

**The five amounts** (approved for prototyping):

| Amount | Musical meaning |
|---|---|
| **Density** | Thins out or fills in |
| **Variation** | How far each phrase strays from what was written |
| **Humanity** | Looseness of timing and touch |
| **Movement** | The sound itself breathes |
| **Tension** | Pressure toward the end of the phrase, released on the downbeat |

**Evolution** shapes how those amounts travel over time:

| Control | Choices |
|---|---|
| **Evolution amount** | How far the journey goes from the written pattern, 0 to full |
| **Evolution duration** | 4, 8, 16, 32 or 64 bars |
| **Evolution behaviour** | See below |

| Behaviour | Shape over the duration |
|---|---|
| **Steady** | A constant level of life. No direction. |
| **Evolve** | Drifts gradually further from the original, then starts a new journey from where it is. |
| **Build** | Grows from the original to full, then drops back on the downbeat. |
| **Dissolve** | Starts full and thins away to almost nothing. |
| **Breathe** | Out and back: rises to the middle, returns by the end. |
| **Wander** | A new direction each phrase, never beyond the evolution amount. |

**Six actions:**

| Action | What it does |
|---|---|
| **Protect** | Marks events LIFE may never touch (the kick on 1, the hook). |
| **Keep** | Writes the variation playing now into the next free pattern slot as an ordinary pattern. |
| **Hold** | Freezes the variation playing now. It repeats as it is until Hold is released. |
| **Return** | Glides back to the written pattern over the next phrase. The settings are remembered. |
| **Reset** | All amounts to zero at once. |
| **Reseed** | A different journey with the same settings. |

Every action is one Undo step.

**Three kinds of development, not random notes.**

| Kind | What changes | Examples |
|---|---|---|
| **Rhythmic** | Which events play, and when | Ghost notes, dropped hits, shifted accents, rolls into the downbeat, nudge |
| **Melodic** | Which notes | Neighbour tones and passing notes in the song's key, octave moves, chord inversions, ornaments. Never out of key unless the musician allows it. |
| **Timbral** | How it sounds | Decay, tone, filter, accent, jawari; later the A/B morph position (section 7) |

Each instrument has its own rules for what "more density" or "more tension" means, so the bass does not behave like the hi-hat.

**[AMENDED 11-28] What controls a setting: five layers.**

A continuous setting's value at any moment is built from five separate things. Keeping them separate is what lets them work together instead of fighting.

| Layer | What it is | Stored where |
|---|---|---|
| **Authored base** | Where the musician left the knob | The project |
| **Automation** | A recorded or drawn path for the base over time (Rec moves, lanes, and macros as they work today) | The project |
| **Modulation** | Movement added around the base: LFOs, envelope followers, modulation slots, CONNECT links aimed at the setting, LIFE Movement | Only the amounts and routes are stored. The moving value never is. |
| **Event lock** | A value written on one step or note, used for that event only | The pattern |
| **Manual override** | The musician's hand on the control right now | Becomes the new base when released, unless Rec moves is recording |

How they combine, as the target design:

> value = limit( smooth( base or automation, replaced by an event lock for that event ) + the sum of the modulation sources, each within its own depth )

and while a hand is on the control, the hand wins.

Rules:

- **Nothing is permanently shut out.** Putting automation or a macro on a setting never stops LIFE, a link or an LFO from also moving it. Automation moves the centre; modulation moves around the centre.
- **Bounded.** Each modulation source has its own maximum depth on a setting, and the total is limited to the setting's range, so combined movement cannot run away.
- **The first implementation may be exclusive where necessary.** Where a setting cannot yet combine sources safely, the first build may let only one modulation source act on it at a time, and shows which. This is a starting restriction inside the layered model, not the design. The storage and the engine path are built for combination from the start.
- **Modulation is never recorded as automation,** never lights the unsaved dot, and leaves no trace when switched off.
- **Switches and choices** (wave, filter type, on/off) are not modulated. They take automation and event locks only.

**Defined per setting before any of this is built.** A table for every continuous setting, written and reviewed first:

| Column | Example: filter cutoff | Example: level |
|---|---|---|
| How sources combine | In octaves (so movement sounds even across the range) | In decibels |
| Limits | The filter's range | Silence to the strip's maximum; never above the safety limiter's ceiling |
| Largest depth per source | LIFE ±1 octave, a link ±2, an LFO ±3 | LIFE ±3 dB, a duck down to silence |
| Smoothing | A few milliseconds, to avoid zipper noise | A few milliseconds, to avoid clicks |
| When a hand touches it | Modulation keeps going around the new position | Modulation pauses while held |
| With an event lock | The lock replaces the base for that event; modulation still applies unless the lock says "exact" | Same |

**LIFE and note-shaping links.** These act on events, not on settings, and are covered in 5.3: the order is worked out per link from who listens to whom, not fixed globally.

**On screen.** One LIFE band in the same position on every instrument, drawn in that instrument's own style: five amounts, the evolution row, and the six actions. On the phone it is a row of chips above the grid that opens one slider at a time.

**Engine shape.** One shared stage sits between "what the pattern says" and "what gets played" in each engine. It asks, per event: play it, move it, change its note, change its level? The answer comes from a small calculation with no memory allocation and no waiting, so it is safe on the audio thread. This is the main **NEW** engine work in LIFE; the per-instrument rules are the rest.

---

## 4. Layers and the portable project [PROPOSAL]

### 4.1 Editable layers (the immediate step)

A layer is audio, as today. Where it came from an instrument, it also carries a **recipe**: which instrument, the pattern or notes, the sound settings, the effects board, and later the LIFE settings and seed.

These are **editable layers**, not live layers. The audio is fixed until you change something. A **live layer**, one that keeps generating music in real time, is a separate future idea (4.3).

The phone already stores most of a recipe on each layer [CHECKED]. The Mac stores only the source. The phone's structure moves into shared code and the Mac adopts it. **REUSE / PREP.**

**Back to instrument** (the reverse of TO LOOPER) must do three things:

| Requirement | How |
|---|---|
| **Preserve existing work on that instrument** | Before loading the recipe, the instrument's current part is put in its next free pattern slot. If there is none, it asks. Nothing is ever overwritten. |
| **No doubled playback** | The layer mutes and the instrument switches on at the same bar line, using the same hand-off the Mac's To looper uses today in the other direction. |
| **Atomic Undo** | One Undo step restores all three together: the layer unmuted, the instrument off, and the instrument's previous part. |

**Re-rendering must not interrupt anything.** The new audio is made on a background thread into fresh memory. The old audio keeps playing. The swap happens at the loop's start, by handing the audio thread a finished buffer, which both loopers already know how to do. The clock, the other layers and the instruments are never paused. If the render is still running when you change something again, the stale one is dropped.

### 4.2 Stable identities (before LIFE and CONNECT add data)

Today things are named by position: layer 3, pattern 5, bar 17. Positions change. Before any new data refers to them, each gets a permanent identity:

| Thing | Identity | Why |
|---|---|---|
| **Instrument** | A type and an instance: `synth.1`, `bass.1` | Leaves room for `synth.2` later |
| **Layer** | A permanent id, not its row number | Layers can be reordered, and 8 can become 16 |
| **Pattern** | A permanent id, not its slot number | Song boxes, recipes and Keep refer to it |
| **Sample** | An id from its content, plus its name | The same sample is stored once, and found again after a rename |
| **Section** | A permanent id, not its bar | Sections move |
| **Sound** | A permanent id for a saved patch or kit | Recipes and collections refer to it |
| **Relationship** | Refers to instruments by identity | Survives reordering and transfer |

Old shows and songs get identities the first time they are saved. Nothing visible changes.

### 4.3 Not blocking true live layers and several copies of an instrument

**What blocks it today** [CHECKED]: the Mac engine owns exactly one of each instrument, and the mixer strips, song lanes, controller targets and stems are fixed lists with one entry per instrument.

**What helps** [CHECKED]: the sound engines are separate from the instruments' shells. The phone already runs three copies of the synth engine at once (keys, pattern, and the chords' voice). The sampler's keyboard player and the drone engine are self-contained.

**[AMENDED 11-28] What a future instance must be able to have, each independently:** its own mixer strip (level, pan, mute, solo, sends), its own effects rack, its own automation, its own LIFE settings, and its own CONNECT links. Several instances may optionally feed a shared group bus (for example all synths), but a shared strip is never required.

**A likely route, for later investigation:** extra copies of a sound engine, each with a strip created for it, driven by its own pattern or by note clips in the song. That suits Synth, Bass, Sampler and Drone. Hosted plugins are the hard case, since each copy is a whole plugin.

**What is done now so this stays possible:**

- **Identities:** instruments are a list with type and instance (`synth.1`, `synth.2`), never a fixed set of names.
- **Routing:** mixer strips, effects racks, song lanes, automation lanes, controller targets and stems refer to an instrument by identity in the project, not by a fixed position. (The app's internal fixed lists stay for now; the saved format does not depend on them.) An optional `bus` field on a strip is reserved for group buses.
- **Layer recipes** name their instrument by identity.
- **Layers** have a `kind`: `audio`, `editable`, and a reserved `live`.
- **LIFE settings and CONNECT links** are stored per instrument identity, so a second instance can carry its own.

No engine work on this now.

### 4.4 The `.kbox` package

A folder that looks like one file:

```
My song.kbox/
  project.json     format version, tempo, time signature, key, sections (with ids)
  instruments/     each instrument: type, instance, patterns, settings, sound
  layers/          each layer: audio (always present), kind, recipe
  samples/         every sample the song uses, copied in, stored once
  song.json        blocks, takes, note clips, automation, relationships, LIFE settings
  mac/  pocket/    whatever only one platform understands, carried untouched
```

It preserves: audio, editable notes, patterns, sounds, samples, effects, automation, sections, key, time signature and relationships.

| Rule | Meaning |
|---|---|
| **Audio is always there** | Anything a platform cannot play live has a rendered sound to fall back on: a hosted plugin's part on the phone, a Mac-only effect, layers the phone cannot show yet. |
| **Unknown settings are carried, never dropped** | Open on the phone, save, open on the Mac: the Mac's own data is intact. |
| **Versioned** | Every older show and song still opens and is converted on save. |
| **Honest about fallbacks** | A layer playing from its fallback says so, and says what would be needed to edit it. |

### 4.5 Order of building

1. **The schema and identities, written down and reviewed.** No code.
2. **Identities added to Mac shows and phone songs.** Invisible to the musician.
3. **A phone song opens on the Mac as an editable song.** One direction. Layers with their recipes, sections, key, tempo, synth and bass parts as notes.
4. **The Mac saves `.kbox`.** Only after quit protection is accepted, because it touches saving.
5. **The phone opens a Mac `.kbox`**, with fallbacks.

---

## 5. CONNECT: musical relationships [PROPOSAL]

### 5.1 Six kinds of signal

A relationship carries one kind of signal from a source to a target. Keeping the kinds apart is what makes links predictable.

| Signal | What it is | Where it comes from | Timing |
|---|---|---|---|
| **Audio** | The sound itself | Any strip or layer | Same block where the processing order allows, else one block late (a few ms) |
| **Envelope** | How loud it is, moment to moment | A follower on any strip (the Wah pedal's follower, made shared) | As audio |
| **Rhythm** | When its events fall | The source's events at one of three stages (see 5.1a). For audio layers, the hits found when the layer was recorded. | Exact. No listening delay. |
| **Note** | Which notes it plays | The source's events at one of three stages (5.1a), or live keys | Exact |
| **Harmony** | The key and the chord of the moment | Chords, Live key, the song key | Exact |
| **Transport** | Phrase and section events | The phrase position (3.4) | Exact |

Because rhythm, note and harmony signals come from events rather than from listening to audio, most musical links are exact and cost almost nothing.

### 5.1a Three stages of an event [AMENDED 11-28]

| Stage | What it is | Known when |
|---|---|---|
| **Authored** | What is written in the pattern | Always |
| **Generated** | What the instrument is about to play after chance and LIFE have decided | A short moment ahead (the engine works out each step or block before it sounds) |
| **Performed** | What actually sounded, including notes played by hand | As it happens; kept in the event log (3.3) |

A link says which stage it reads. **The default for rhythm and note links is Generated**, so a bass that avoids the kick avoids the kicks that really play tonight, ghost notes and dropped hits included, not only the ones on the page. A link may read Authored when a steady reference is wanted, or Performed when it should react to live playing.

### 5.2 Seven roles

| Role | Musical meaning | Signals it uses | First curated examples |
|---|---|---|---|
| **Follow** | Move with the source | Harmony, note | Bass follows chords. Drone follows the key. Vocoder carrier follows another instrument's notes. |
| **Answer** | Play in the space after the source's phrase | Rhythm, transport | Call-and-response percussion: congas answer the kick pattern every second bar. |
| **Avoid** | Stay out of the source's way | Rhythm | Bass rests where the kick hits. Hats step aside for the snare. |
| **Support** | Reinforce the source's accents | Rhythm | Bass accents with the kick. A shaker doubles the hat's off-beats. |
| **Contrast** | Do the opposite | Envelope, rhythm | When the drums get busy the pad thins. When the voice enters, the synth's filter closes. |
| **Trigger** | The source's event sets something off | Rhythm, envelope, transport | Kick ducks the pad. A section change fires a fill. |
| **Resonate** | The source's sound excites the target | Audio | The drone's strings ring to the voice (exists). |

Answer and Avoid together give complementary rhythms: two parts that interlock instead of piling up.

### 5.3 Rules

- **Optional.** Every link has an on/off and an amount. Off means exactly as before.
- **Predictable.** Follow and Avoid never add wrong notes: they move or remove what the target was already going to play.
- **Curated first.** The first releases offer a short list of named links. The roles and signals above are the long-term design they all fit into.
- **Compatible signals only.** A role only offers sources that carry the signal it needs.
- **Order is worked out from the links, not fixed.** [AMENDED 11-28] Each instrument, per block, does: its authored events, then its incoming links, then its own LIFE, giving its generated events. Instruments are processed in the order their links require: if the bass listens to the kick, the drum machine's generated events (after the drums' own LIFE) are ready before the bass decides. There is no universal "CONNECT before LIFE": a target responds to its source's finished, generated events, and then its own LIFE varies the result within what the link allows.
- **This is settled before any link is built.** The first CONNECT PR is the dependency ordering itself, with tests, and no musical link.
- **Loops.** When a link is made, the chain is checked. Two instruments that would each need the other's current events cannot both go first. The choice offered is plain: refuse the second link, or let it read the other's **previous** step or phrase (which is how two players answer each other anyway).
- **Real-time safe.** Links are settled on the message thread and handed to the audio thread as a finished table. Nothing is looked up or allocated while playing.
- **Links aimed at a setting** (duck, contrast) are modulation and follow the layered rules in 3.5.
- **In a one-tool Logic plugin** there are no other instruments. Links switch off and the screen says why.
- **Saved, undoable, portable.** Links refer to instruments by identity (4.2) and live in the project.

### 5.4 On screen

A link reads as a sentence on the target instrument: **Bass · Follows · Chords**, **Bass · Avoids · Kick**, with one amount. No patch cables.

---

## 6. Creative sound transformation [PROPOSAL]

**The promise.** One recorded sound can become many kinds of musical material, and the original recording is never lost.

### 6.1 The original is kept

Every recording, sample and loop becomes an **asset** with an identity (4.2). A transformation makes a new thing that points back to its asset and remembers the settings used. "Show original" is always available. This is the non-destructive model: the sampler's effects stop overwriting the sound and become a list that is applied again when changed. **PREP.**

### 6.2 The universal Transform menu

One menu, the same on any recording, sample, loop or layer, on both apps: **Turn into…**

| Turn into | Result | Built from | Label |
|---|---|---|---|
| **A loop on the beat** | Fitted or warped layer | Fit and Warp (exist) | REUSE |
| **Pads** | Slices across the pads | Find hits and chop (exist) | REUSE |
| **Vocal chops** | One step: find syllables, trim, spread across pads, in key order | Find hits + chop + pad tune | REUSE |
| **Keys** | Play it across the keyboard | `KeyboardSampler` (Mac has it; phone needs the screen) | REUSE |
| **A rhythm** | Its hits become a drum or pad pattern (low hits to kick, mid to snare, bright to hat) | Find hits + quantize; small new sorting step | REUSE + small NEW |
| **Slices in sequence** | The slices on a step grid: reorder, repeat, reverse, pitch per slice | Slices mode + the shared note pattern (a slice is a note) | PREP |
| **Notes** | A sung or played single-line melody becomes editable notes for Synth or Bass | Needs a pitch tracker and note splitting. The vocoder's detector is the starting point. | NEW (offline, no real-time risk) |
| **In tune** | Loop Lab Tune: auto-tune a recorded take to the song's key, with retune speed | The vocoder's auto-tune run offline | REUSE + PREP |
| **Harmony layers** | One or more new layers a third, fifth or octave away, in key | The vocoder's harmony run offline | REUSE + PREP |
| **A frozen pad** | A held, sustained texture from one moment of the sound | Grain freeze (exists on the Mac) | REUSE; phone needs Grain |
| **A texture instrument** | Granular: play grains of the sound across the keys, with position, size, spray | New playable voice built on `GrainCloud` | NEW (real-time; CPU budget) |

### 6.3 Loop Lab: Slice, Tune, Play

The three pages designed earlier and not yet built sit inside this phase: **Slice** (the slice grid above), **Tune** (in tune, harmony, melody to notes), **Play** (slice pads, scrub, repeat, freeze, tape stop, gate, played live on a loop).

### 6.4 Deeper pads

**Round-robin and velocity layers.** A pad holds a small list of samples and picks by turn or by how hard it is hit. The real MPC2000XL's file format cannot carry this, so card export uses the first sample and says so. **PREP** (the pad's model) then **REUSE**.

### 6.5 Rules

- Every transformation is one Undo step and never stops playback.
- Analysis (hits, pitch, stretch) runs on a background thread.
- Results land where the musician expects: a new layer, the pads, or the instrument, via the same routes as TO LOOPER and the dock's Rec.

---

## 7. Expressive sound architecture [PROPOSAL]

### 7.1 A/B sound states and morphing

Each instrument can hold two complete sound states, **A** and **B**, and a **morph** position between them.

- Smooth settings glide. Switches (wave, filter type) flip at the halfway point, or at a bar line if chosen.
- **Morph over musical time:** "A to B over 8 bars, landing on the downbeat", using the phrase position.
- The morph position is one setting. So it can be automated, put on a macro, moved by LIFE Movement, driven by a CONNECT link, or learned to a Launchkey knob.
- **Hosted plugins:** morph covers only what the plugin exposes to the host. Where a plugin exposes little, the instrument says morph is limited for this sound.

**REUSE** for the parameter system and automation; **PREP** for state storage; small **NEW** for timed morph.

### 7.2 Shared modulation sources

Today movement comes from each instrument's own single LFO, from Move (LFO-shaped automation), macros, and step locks. The shared set:

| Source | Exists as | Work |
|---|---|---|
| LFO | Each engine's own; Move shapes | REUSE |
| Second LFO per instrument | | Small NEW |
| Envelope follower | Inside the Wah pedal | PREP: lift into shared code (also feeds CONNECT) |
| Step locks | Drum machine | PREP: extend to Bass and Synth notes |
| Seeded random / sample-and-hold | Move's random shape | REUSE, made repeatable with the chance seed |
| Phrase ramp | | REUSE of the phrase position |
| Velocity, key position | In the synth and bass engines | REUSE |

**Modulation slots:** a few per instrument, each "source → setting, amount". They are modulation in the layered model (3.5): bounded, combinable, and never permanent owners of a setting.

### 7.3 Each instrument stays itself

The acid bass keeps its accent and slide. The drum machine keeps its analog panel. The drone keeps jawari and its strings. The vocoder keeps its pedalboard. Shared concepts (LIFE band, A/B, modulation slots, links) are added as a band in each instrument's own style, in the same position, with the same words.

The phone's four knobs per sound remain the surface. A deeper view of the full engine is added beneath them, so the phone reaches the Mac's sound depth without crowding the first screen.

---

## 8. Sound library [PROPOSAL, for investigation]

### 8.1 A musical sound browser

| Capability | Exists | Work |
|---|---|---|
| Kinds, tempo and key read from names; search; BPM search; hear on click | Yes | |
| Send directly to a pad, the sampler, a loop layer | Yes (Mac); pads and sampler (phone) | Extend to every instrument and the drum machine's voices |
| **Audition in the song's tempo and key** while the song plays | No | REUSE (stretch and pitch exist) |
| **Waveform preview** in the list | Not confirmed | REUSE (peak tables exist for loops) |
| **Favourites and collections** | No (songs have stars; sounds do not) | PREP (sample identity, 4.2) |
| **Musical character tags** (dark, bright, wooden, metallic, dusty…) | No | Tags by hand first; automatic tagging later |

### 8.2 Sound worlds (complete creative collections)

A package holding things that belong together: compatible kits, synth and bass patches, a drone setting, effect boards, a chord palette, macro assignments, and suggested LIFE settings. Opening one sets up a whole palette in one step.

- **Optional starting points, never a mode.** Everything a world loads is ordinary, editable material. Nothing is locked.
- "Tonight's spark" on the front page is the seed of this idea.
- A world uses the same identities and package rules as `.kbox`, so it depends on Phase 2.

---

## 9. Pocket interface [PROPOSAL]

### 9.1 Sixteen stereo layers

Approved subject to real-device testing. **The layout is not decided.** Before any code, mock up and try on a real phone at least these three:

| Option | Idea |
|---|---|
| **A. Continuous list that compresses** | One vertical list. Up to 8 layers have full rows. Beyond that, rows shrink so all stay visible; tapping one expands it. |
| **B. Overview tiles + detail** | A 4 × 4 grid of small tiles (colour, level, mute) always visible, with the selected layer's full controls beneath. |
| **C. Grouped by instrument** | Layers gathered under their instrument's colour, each group collapsible. |

Whichever is chosen must let the musician **see** every layer at once, and **select, mute, edit and remove** any of them in one or two touches, with no sideways scrolling and no hidden page.

### 9.2 Every instrument directly reachable

The tool row keeps every instrument one touch away with no horizontal scrolling through hidden choices. Any new band (LIFE, A/B, links) goes inside an instrument's screen, never into the tool row.

### 9.3 Performance validation before commitment

On the oldest iPhone to be supported: 16 stereo layers playing, each with its effects, the vocoder on, the song playing, MOVES held. Measure CPU headroom, memory, battery heat over 20 minutes, and whether Undo's history still fits. If it does not hold, the fallbacks in order are: fewer simultaneous per-layer effect boards, then 12 layers.

---

## 10. The Stage interaction model [PROPOSAL: designed now, built in pieces]

### 10.1 Five kinds of action

Every performance control is one of these. The kind decides how it looks and what a press, hold and release do.

| Kind | Behaviour | Looks like | Exists today as |
|---|---|---|---|
| **Momentary** | On while held | Lit only under the finger | FX pad, Note Repeat hold |
| **Latched** | Press on, press off | Stays lit | Mute, solo, HOLD on MOVES, loop on/off |
| **Queued** | Waits for a musical boundary, then happens | Blinks until it lands; press again to cancel | Section launch, loop bank switch (next bar only) |
| **Timed** | Runs for a set musical length, then ends itself | A shrinking ring | Drop (returns on the next bar), tempo ramp |
| **Transitional** | Moves from one state to another over a length | A progress bar between two names | Tempo ramp only |

Each action has a **target**, a **launch timing** (now, next beat, next bar, 2 or 4 bars, end of phrase) and, where it applies, a **duration**.

### 10.2 Transitions between sections

A transition is a first-class thing, set per boundary ("Verse → Drop: Build, 4 bars") and overridable live.

| Transition | What happens |
|---|---|
| **Cut** | Switches at the launch boundary. Today's behaviour, with timing now selectable. |
| **Morph** | Mixer, effects and sound states glide from one section's snapshot to the next over the duration, landing on the downbeat. |
| **Build** | Tension rises into the change: LIFE tension and density up, filter opening, a riser, a fill in the last bar. |
| **Dissolve** | The outgoing section thins and fades while the incoming one fades in underneath. |

### 10.3 Safe return

One control, **Home**, always visible and learnable to a pad:

- Releases every momentary and timed effect.
- Cancels everything queued.
- Returns mixer, effects and sound states to the current section's stored snapshot, at the next bar, with no silence and no jump in position.

Stop already silences all notes. Home is the musical version: it puts the song back on its feet without stopping it.

### 10.4 Building blocks, in order

Launch timing → the five action kinds in the engine → per-layer and per-instrument moves → snapshots → transitions → Home → recording of performed actions (using the event log from 3.3) → Launchkey banks → setlist with no gap between songs → the Stage workspace, which is a layout of all of these.

---

## 11. Recording and undo consistency audit

### 11.1 Recording

**Mac.** The bottom bar's Rec now carries its target in words [CHECKED]. Beside it, these start recording by other routes:

| Route | Records | Undo |
|---|---|---|
| Dock Rec → next layer | Audio loop | One history |
| Dock Rec → song (Rec into, Cycle) | Audio or note takes | Takes chooser |
| Dock Rec → sampler or pad | Audio | None |
| Dock Rec → whole set | Stems and MIDI to disk | None |
| Dock Rec → MPC pattern | Pad hits | MPC's own undo |
| To looper | Audio loop, instrument off | One history |
| Overdub | Onto a loop | One history |
| Rec live (Song) | Blocks and sections | Song undo, bridged to the history |
| Rec moves | Automation lanes | Bridged |
| Chords: To loop, Rec MIDI | Audio loop / a MIDI file | History / none |
| Screen buttons: Looper rows, Mix layer strips, Effects "Rec to…", Vocoder "To looper", Line in "Loop it", Sampler "Rec sample" | As above | Varies [CARRIED] |

**Phone.** REC on every tool (next layer), TO LOOPER, DUB and DUB OVER IT, REC PADS, RECORD A SOUND, SONG REC LIVE / PUNCH / REC FROM END, REC MOVES, Catch an idea. All are covered by the one history except the sample recorder's own edits.

**Inconsistencies to fix:**

1. Three Mac targets have no undo at all (sampler or pad, whole set, Rec MIDI). At minimum they should say where the file went and offer "delete it".
2. Count-in exists for loop takes; whether every route honours it was not re-checked [CARRIED: on 11-22 only the MPC had one].
3. The same press means different things on different screens (keys 1-8).
4. Neither app can capture the moment before Rec.
5. On the Mac, a loop take of a note instrument keeps no notes. On the phone it does.

### 11.2 Undo

| | Phone | Mac |
|---|---|---|
| Design | A snapshot of the whole song each time a change settles | A journal of up to 64 reversible steps |
| Covers | Everything in the song: hits, knobs, layers, ON/OFF, tempo | Loop takes, overdub, clear, To looper, Sing free fit, song edits, Play view clear and mutate, quantize, automation, Loop Lab, sampler pad operations, chord edits |
| **Not covered** | The sample editor's edits (own undo) | **Knob turns, per-step pattern edits**, the piano roll, the MPC sequencer, the drum machine panel, the sample editor (each has its own separate undo) |

Separate undo stacks still in the code [CHECKED]: sample editor (200 steps), MPC sequencer and bank, pad sound editor, drum machine panel, Play view, piano roll, Song view, Bars view, automation, Loop Lab, quantize, looper overdub; on the phone the looper screen, sampler, recorder and beats screens.

**Target:** one rule on both apps. *Anything you did, Undo takes back, in the order you did it.* On the Mac that means finishing stage 2 (knobs and steps) and bridging the remaining stacks into the journal, as the Song view and automation already are. LIFE's actions, CONNECT links, Transform and "Back to instrument" must each be a step from the day they ship.

---

## 12. Mac / Pocket parity matrix [CHECKED unless marked]

| Capability | Mac | Pocket | Gap |
|---|---|---|---|
| Looper layers | 16 stereo | 8 mono | Phone smaller (hardware allows more) |
| Loop banks, layer solo, live bends | Yes | No | Phone missing |
| TO LOOPER, instrument ON/OFF | Yes | Yes | Level |
| Layer remembers its notes and sound | No | Yes | **Mac missing** |
| Synth note pattern (1-8 bars) | No | Yes | **Mac missing** |
| Bass | Acid 16-step grid | Synth engine, note pattern | Different instruments |
| Full synth and bass controls | Yes | 4 knobs per sound | Phone shallower |
| Drum machine sounds, kits, 64 steps, rolls, flams | Yes | Yes | Level |
| Per-hit chance, conditions, nudge, polymeter, Euclid, mutate, locks | Yes | No | Phone missing |
| MPC sequencer (tracks, punch, songs) | Yes | Pad pattern only | Phone simpler by design |
| 16 Levels, Make 16 pads | Yes | Yes | Level |
| Sampler edit, chop, to pads | Yes | Yes | Level |
| Play a sample across the keys | Yes | No | Phone missing |
| Chords, Live key, chords on/off, own chords | Yes | Yes | Level |
| Map keys (every key a chord) | Yes | Not applicable | No hardware keys |
| Vocoder | 13 pedals + full view | 14 pedals | Level |
| Drone, Miracle tones | Yes | Yes (binaural lost on mono layers) | Mono limit |
| Effects pedalboard | 15 incl. Mic and Grain | 13 | Phone lacks Grain and Mic models |
| FX pad / MOVES | Yes | Yes | Level |
| Macros, tempo ramp | Yes | No | Phone missing |
| Automation | Any setting, 64 lanes, Move shapes | 5 knobs per layer | Phone shallower |
| Quantize, Warp, Sing free, Catch an idea | Yes | Yes | Level |
| Song with sections, Rec live, takes | Yes | Yes | Level in shape |
| Piano-roll note editing | Yes | In progress | |
| Friend's track, Export WAV/AIFF/MP3 | Yes | Yes | Level |
| Undo/Redo covers everything | Partly | Yes | **Mac behind** |
| Saves by itself | No | Yes | **Mac behind** |
| Hosted plugins, Line in, Logic plugins, MIDI controllers | Yes | No | Platform |
| A phone song opens on the other app | No | No | Both |

---

## 13. Real-hardware validation plan

A large share of what is on `main` has passed automated tests but has never been heard by a person. New systems should not be stacked on unheard ones. The owner prefers one short step at a time in chat, so these are **sessions**, each about 15 minutes, with one question each.

| # | Session | The one question | Needs |
|---|---|---|---|
| 1 | Mac Undo/Redo and To looper | Does Undo take back what you expect, in order? | Mac |
| 2 | Catch an idea and Sing free | Does the song land on the tempo you sang? | Mac + phone |
| 3 | Drum machine life and ritual kits | Do chance, conditions and the hand drums sound musical? | Mac |
| 4 | Drone, Resonate, Miracle tones | Does singing ring the strings? Is the level right? | Mac + mic |
| 5 | Chords on the Launchkey: Map keys, Live key, on/off, All off | Does it feel playable live? | Launchkey |
| 6 | Sound bending: Grain, loop bends, FX pad Drop/Freeze/Reverse, macros | Any clicks or level jumps? | Mac |
| 7 | Song: Rec live, punch, takes, note editing | Can you build a 3-section song without getting lost? | Mac |
| 8 | Visuals on real screens | Smooth on the Mac's GPU and the iPhone? | Both |
| 9 | Logic: tool plugins' Rec and Drag chips, Input plugin | Does a part land in Logic in time? | Logic |
| 10 | Hosted Arturia in Bass and Synth; save, quit, reopen | Does the sound come back? | Arturia |
| 11 | Friend's track round trip | Do your parts line up from 0:00 in their file? | Any song file |
| 12 | MPC card export | Does the real 2000XL load the kit? | MPC2000XL + card |
| 13 | Phone link: sound, kit and loop to the Mac; Remote | Does it arrive and play in time? | Both, same Wi-Fi |
| 14 | The non-musician test (already agreed) | Where does she get stuck singing an idea and looping it? | Phone |

Latency tests with an audio interface wait until there is a working interface.

Every bug found becomes its own small PR before the next phase starts.

---

## 14. Engineering rules and budgets

### 14.1 Real-time safety

These hold today and every new system must keep them:

- Nothing on the audio thread allocates memory, waits for a lock, or does work that grows with the length of a loop.
- Settings reach the audio thread as finished tables handed over without waiting.
- Analysis and rendering (hits, pitch, stretch, re-render, bounce) run on background threads. Stale results are dropped.
- LIFE and CONNECT decisions are a small fixed calculation per event.
- Navigation, editing, saving and transforming never stop the clock.

### 14.2 CPU and memory

There is **no CPU or dropout meter in the code today** [CHECKED]. Budgets cannot be honest without measurement, so Phase 0 adds a meter first. Proposed targets, to be confirmed once measured on the owner's Mac and iPhone:

| Budget | Target |
|---|---|
| Mac, whole rig with the vocoder on, small buffer | Under half of the available time per audio block, leaving room for hosted plugins |
| Any one new system (LIFE, CONNECT, modulation) | A few percent at most; measured in its PR |
| Granular instrument, extra engine copies | Each has a stated voice limit and its own measurement |
| iPhone, 16 stereo layers with effects and vocoder | Stable for 20 minutes on the oldest supported phone (9.3) |
| Mac loop memory | Already bounded: about 1 GB per 45 minutes of stereo loop, 2.5 hours in total [CHECKED] |
| Phone memory | Stereo doubles layer memory. Recipes are small. Undo shares audio between steps rather than copying it [CHECKED]. Measure before committing. |

### 14.3 Hosted plugins

- A plugin's saved state is kept byte for byte and never rewritten (existing rule).
- Undo never reloads a plugin (the reason the Mac uses a journal, not snapshots).
- Plugins are not repeatable sample for sample (3.3), may expose few settings to morph or modulate (7.1), and exist only on the Mac. Every plugin part has a rendered fallback in the project.
- A plugin crash still takes the app down. Safer hosting stays on the list as a later investigation.

### 14.4 Project portability

Identities before new data (4.2). Versioned format. Unknown data carried untouched. Audio always present. Samples copied in and stored once.

### 14.5 Recording consistency

Every new way of capturing goes through an existing route: the dock's Rec with its visible target, TO LOOPER, Keep, or Transform. Each honours count-in and length, lands where the LCD says, and is one Undo step.

### 14.6 Authored material

LIFE, CONNECT and Transform never modify what the musician wrote or recorded. Originals are always retained. The unsaved-changes tracker ignores what these systems do while playing.

---

## 15. Roadmap

Each line is one small PR unless noted. Every PR keeps old shows and songs opening, adds automated tests, and has a musical acceptance test (section 16). Phases overlap where their dependencies allow; the notes say where.

### Phase 0: Reliability and recovery

| # | Change | Label |
|---|---|---|
| 0.1 | Quit protection (PR #137) merged after its own acceptance. In progress elsewhere. | REUSE |
| 0.2 | Autosave to a recovery copy, and the offer to restore after a crash | REUSE |
| 0.3 | Save the time signature | REUSE |
| 0.4 | Section edits carry recorded notes and takes; Song undo includes them | REUSE |
| 0.5 | Undo stage 2: knob turns and step edits in the one Mac history | PREP |
| 0.6 | The piano roll, MPC sequencer and drum panel joined to the one history (one PR each) | REUSE |
| 0.7 | A CPU and dropout meter on both apps | REUSE |
| 0.8 | Hardware sessions 1 to 14 (section 13), with fixes as they come | |

### Phase 1: Frictionless musical creation

*Can start alongside late Phase 0 items that do not touch saving.*

| # | Change | Label |
|---|---|---|
| 1.1 | The phone's note pattern moved into shared code (no change in behaviour) | PREP |
| 1.2 | Mac Synth note patterns; Song boxes can pick them | REUSE |
| 1.3 | Mac Bass note-pattern mode beside the acid grid | REUSE |
| 1.4 | Keep what was just played before Rec, both apps | NEW (small) |
| 1.5 | Mac layers carry a recipe; each layer shows what made it | PREP |
| 1.6 | Re-render a layer in the background without interrupting playback | REUSE |
| 1.7 | Back to instrument, both apps (preserves work, no doubling, one Undo) | REUSE |
| 1.8 | Controller learn for loops 9 to 16; the Drone gets a song lane | REUSE |
| 1.9 | Pocket layer overview: three mockups tried on a real phone, then a choice | Design |
| 1.10 | Pocket 16 stereo layers: device measurement, then build | NEW (moderate) |
| 1.11 | The acid bass grid on the phone | REUSE |
| 1.12 | Library: audition in the song's tempo and key; send to every instrument | REUSE |

### Phase 2: Portable musical projects

*The schema document (2.1) can be written during Phase 0. Code that touches saving waits for 0.1.*

| # | Change | Label |
|---|---|---|
| 2.1 | Schema and identities written down and reviewed. No code. | Design |
| 2.2 | Identities added to Mac shows and phone songs, invisibly | PREP |
| 2.3 | A phone song opens on the Mac as an editable song (one way) | REUSE |
| 2.4 | Note takes become blocks in the song; legacy scenes leave the audio thread (old files still load) | PREP |
| 2.5 | The Mac saves `.kbox` | PREP |
| 2.6 | The phone opens a Mac `.kbox`, with rendered fallbacks | REUSE |

### Phase 3: Living patterns

*Needs 1.1 and 2.2.*

| # | Change | Label |
|---|---|---|
| 3.1 | Tests that pin today's conditions, fills, polymeter and Visuals drift | REUSE |
| 3.2 | The phrase position, beside pattern and section positions | REUSE |
| 3.3 | Chance behaviours: Repeatable (new projects), Evolving, Free (kept by existing projects) | REUSE |
| 3.4 | The log of events actually played | PREP |
| 3.5 | Per-hit chance, conditions, nudge and locks on the phone's drum grid | REUSE |
| 3.6 | Protect on every grid | REUSE |
| 3.7 | LIFE on the drum machine: five amounts, Keep, Hold, Return, Reset, Reseed. Both apps. **The prototype the owner hears before going wider.** | NEW |
| 3.8 | Evolution amount, duration and the six behaviours | NEW |
| 3.9 | LIFE melodic rules on Bass and Synth | NEW |
| 3.10 | LIFE on MPC pads, Chords and Drone (one PR each) | REUSE of 3.7 |
| 3.11 | The per-setting combination table (3.5), reviewed before any modulation is built; marks on knobs showing what is moving them | PREP |

### Phase 4: Connected instruments

*Needs 2.2 and 3.2. The follower in 4.4 is shared with 6.1.*

| # | Change | Label |
|---|---|---|
| 4.1 | The link table and dependency ordering: signals, roles, event stages, processing order, loop handling, saved, undoable. No links yet. | PREP |
| 4.2 | Follow: Drone follows the key and the chord | REUSE |
| 4.3 | Follow: Bass follows chords | REUSE |
| 4.4 | Trigger and Contrast: "Listen to" with the shared envelope follower. First: kick ducks the pad. | PREP |
| 4.5 | Avoid and Support: Bass and the kick | REUSE |
| 4.6 | Follow: vocoder carrier follows another instrument's notes | REUSE |
| 4.7 | Answer: call-and-response percussion | NEW (small) |
| 4.8 | Trigger: fills at section changes | REUSE |

### Phase 5: Creative sound transformation

*Items marked REUSE can start any time after Phase 1; the asset model (5.1) should come first.*

| # | Change | Label |
|---|---|---|
| 5.1 | Assets: the original kept, transformations remember their source and settings | PREP |
| 5.2 | The Transform menu, holding what already exists (loop on the beat, pads, sampler) | REUSE |
| 5.3 | Play a sample across the keys on the phone | REUSE |
| 5.4 | Vocal chops to pads in one step | REUSE |
| 5.5 | Non-destructive sample effects | PREP |
| 5.6 | Hits to a drum or pad rhythm | REUSE |
| 5.7 | Loop Lab Slice and slice sequencing | PREP |
| 5.8 | Sung melody to editable notes | NEW |
| 5.9 | Loop Lab Tune; harmony layers | PREP |
| 5.10 | Grain on the phone; vocal freeze on both | REUSE |
| 5.11 | Loop Lab Play | REUSE |
| 5.12 | The granular texture instrument | NEW |
| 5.13 | Round-robin and velocity layers on pads (card export uses the first sample, with a clear warning) | PREP |
| 5.14 | Library: favourites, collections, character tags, waveform previews | PREP |

### Phase 6: Expressive sound architecture

| # | Change | Label |
|---|---|---|
| 6.1 | Shared modulation sources (follower, second LFO, repeatable random, phrase ramp) | PREP |
| 6.2 | Modulation slots per instrument | REUSE |
| 6.3 | Step locks on Bass and Synth notes | REUSE |
| 6.4 | A/B sound states with a morph position | PREP |
| 6.5 | Morph over musical time; morph as a target for LIFE, links, macros and automation | NEW (small) |
| 6.6 | The phone's deeper sound view under the four knobs | REUSE |
| 6.7 | Sound worlds (complete creative collections) | PREP |

### Phase 7: The performance instrument

*The interaction model (section 10) is designed now. 7.1 and 7.2 have no dependencies beyond Phase 0.*

| # | Change | Label |
|---|---|---|
| 7.1 | Launch timing for sections and banks | REUSE |
| 7.2 | The five action kinds in the engine, with queued actions cancellable | PREP |
| 7.3 | Per-layer and per-instrument moves | REUSE |
| 7.4 | Snapshots | PREP |
| 7.5 | Transitions: Cut, Morph, Build, Dissolve | NEW |
| 7.6 | Home (safe return) | REUSE |
| 7.7 | Recording of performed actions as an editable lane; export of a captured performance from the event log | REUSE of 3.4 |
| 7.8 | Launchkey banks | REUSE |
| 7.9 | Setlist with no gap between songs | NEW |
| 7.10 | The Stage workspace | REUSE |

### Later investigations (no build yet)

- Several copies of one instrument, and true live layers (4.3).
- Safer hosting of plugins.
- Automatic character tagging of sounds.

---

## 16. Musical acceptance

### 16.1 Recurring creative challenges

These are run again at the end of every phase, on real hardware, by the owner. They measure the whole instrument, not one feature. A phase that makes any of them harder has failed, whatever its tests say.

| # | Challenge | Becomes fully possible after |
|---|---|---|
| 1 | Create a four-layer musical idea in five minutes | Now; should get faster each phase |
| 2 | Create a composition from one recorded sound | Phase 5 |
| 3 | Develop an interesting five-minute evolving groove | Phase 3 |
| 4 | Capture and retain an unexpected musical variation | Phase 1 (pre-record) and Phase 3 (Keep) |
| 5 | Transfer an editable Pocket composition to the Mac | Phase 2 |
| 6 | Experiment radically and restore the original composition | Phase 0 (undo) and Phase 3 (Return, Reset) |
| 7 | Perform for ten minutes without touching the mouse | Phase 7 |
| 8 | Enable a non-musician to create an enjoyable layered piece | Now; the agreed test with a first-time user |
| 9 | Perform a continuous three-song set | Phase 7 |

Challenges 1 and 8 can be timed today to set a baseline.

### 16.2 Per-phase tests

| Phase | Test |
|---|---|
| 0 | Play for 20 minutes, force-quit, reopen: everything is offered back, loops included. |
| 0 | Record a bass line into section B, move B before A: the bass line moves with it. |
| 0 | Turn six knobs, light four steps, record a loop, press Undo eleven times: back to the start, in order. |
| 1 | On the Mac, write an 8-bar synth melody and place it in two sections. |
| 1 | Play something great without recording, then keep it with one press. |
| 1 | Send a synth part to the looper, hear a wrong note, bring it back, fix it, send it again. Under 20 seconds. The music never stops, nothing doubles, and whatever was on the synth before is still there. |
| 1 | On the phone, with 16 layers, find and mute layer 11 in one touch. |
| 2 | Make an idea on the phone, open it on the Mac: same layers, same sections, the synth part still editable notes. |
| 3 | A drum pattern with LIFE on Evolve over 32 bars stays interesting for five minutes. The kick on 1 never moves. Return glides home. Reset is exactly what was written. |
| 3 | Hear a variation you love, press Keep: it is a pattern you can pick. Works in Free mode too. |
| 3 | Bounce the same Repeatable groove twice: the same hits in the same places. |
| 3 | An old show with conditions, fills and polymeter sounds exactly as it did before. |
| 3 | An old show with chance keeps its free, never-the-same behaviour. A new project's chance repeats. |
| 3 | Play a wild Free groove, export it as a captured performance: the file has the hits you heard. |
| 4 | Change the chord: bass and drone follow with no wrong notes. Switch the link off: they stop following at once. |
| 4 | With LIFE adding ghost kicks, a bass set to avoid the kick avoids the ghost kicks too. |
| 4 | Congas answer the kick every second bar and never land on top of it. |
| 5 | Sing one phrase. Turn it into pads, a melody on the synth, a drum rhythm and a frozen pad. The original recording is still there. |
| 6 | Morph the synth from sound A to sound B over 8 bars, landing on the downbeat. |
| 6 | Automate a filter sweep, then turn LIFE Movement up: the sweep still happens and the sound breathes around it. |
| 7 | Queue the drop with a 4-bar Build, change your mind, cancel it. Then press Home after heavy effects: the song is back at the next bar. |

---

## 17. The next PR: autosave and crash recovery [APPROVED 11-28 as next, after PR #137 is accepted]

**Sequence.** Quit protection (PR #137) is finished and accepted first. Then the full implementation scope for this PR is presented for approval: recovery lifecycle, failure cases, automated tests and real-hardware steps. The PR stays narrow. Nothing in later phases starts until authorized.

The behaviours below are defined now, as required before implementation. They will be confirmed against the code as it stands after #137 when the scope is presented.

### 17.1 What it does

- While the standalone Mac app has unsaved changes, it keeps a **recovery copy** of the current project: the show and any loop or take audio that is new or changed.
- If the app did not close properly, the next launch offers: **Restore what you were working on?** with Restore and Discard.
- It **never writes to the project you saved yourself**, or to that project's audio folder. Only Save and Save As do that.

### 17.2 Loss window

| Change | Recovered up to |
|---|---|
| A completed recording (a take, an overdub, To looper, a Rec live run) | The recording itself: a recovery write starts as soon as it closes |
| Any other authored change (steps, knobs, song edits) | About 60 seconds before the crash |

Both are subject to measurement: the write must cost nothing audible. If a long loop takes longer than the window to write, the window stretches and the status line says so. Audio is written only when it has changed, and a recording in progress is not included until it closes.

### 17.3 Generations

Each recovery write is a new, complete **generation** in its own folder, finished by a final marker written last.

- A generation without its marker is incomplete and is ignored.
- **The last complete generation is kept until its replacement has been committed successfully.** Only then is the older one removed.
- So at every instant there is one whole, readable recovery copy, even if the power fails mid-write.

### 17.4 Defined behaviours

| Case | Behaviour |
|---|---|
| **Untitled, never-saved project** | Protected the same way. Restored as an untitled project with the unsaved dot lit. Saving it asks for a name, as today. |
| **Interrupted recovery write** (crash or power loss mid-write) | The incomplete generation is ignored. The previous complete one is offered. |
| **Interrupted recovery attempt** (crash while restoring) | The recovery copy is not removed by restoring. It is removed only after the restored project has been saved, or the musician chooses Discard. So it is offered again. After a second failed attempt the offer adds "Restore without hosted plugins" and "Show the recovery folder". |
| **Several projects opened in one session** | There is one recovery copy, for the current project. Opening or creating another project already goes through Save / Don't Save / Cancel. Save or Don't Save settles the old project and clears its recovery copy; Cancel changes nothing. Recovery then follows the new project. |
| **Low disk space, or a failed write** | Free space is checked first. If there is not enough, or the write fails, nothing is removed, the previous generation stays, the status line says the recovery copy could not be written and why, and it tries again later. Playback and recording are never interrupted, and no dialog appears mid-performance. |
| **Missing hosted plugin on restore** | Everything else is restored. The plugin's saved sound is kept untouched and returns when the plugin is available again. The screen names what is missing. |
| **Normal Save** | After the save succeeds, the recovery copy is cleared: the saved project is now the truth. If the save fails, the recovery copy stays. |
| **Save As** | As Save. Recovery then follows the new name. |
| **Intentional Don't Save** (on quit or when replacing a project) | The recovery copy is discarded. Nothing comes back at the next launch. This keeps the decision of 2026-11-22. |
| **Clean quit with nothing unsaved** | No recovery copy exists. Nothing is offered. |
| **Standalone app versus inside Logic** | Standalone only. Inside Logic, Logic's own project holds the plugin's state and Logic's own autosave applies. The plugins write no recovery copies and show no recovery prompts. |
| **Discard at the offer** | The recovery copy is removed and the app opens as it otherwise would. |

### 17.4a Concurrency: stale work must never win [AMENDED 11-28]

A recovery write runs in the background and takes time. Save, Don't Save, replacing the project, and newer edits can all happen while one is under way. The rules:

- **Every recovery write carries the identity of the project and the edit count it was started from.** Before it commits, and again before it removes anything, it checks both are still current. If not, it abandons itself and removes only its own unfinished folder.
- **Save, Don't Save and project replacement each advance a counter and cancel any write in flight** before they act. A write that started before a Don't Save can never commit after it, so **discarded work is never resurrected**.
- **Clearing the recovery copy after a Save clears only generations older than that Save.** A recovery write that began from edits made after the Save is untouched, so **newer changes are never cleared**.
- **One recovery operation at a time.** Writes, clears and restores go through a single queue, so two can never interleave.
- A restore reads only a generation that was complete when the app launched. Nothing written during this session can be offered in this session.

Tested specifically, each with the background write paused at every step: Save during a write; Don't Save during a write; opening another project during a write; a new edit during a write; a new recording finishing during a write; two of these at once; quit during a write.

### 17.4b Durability: confirmed on disk, not assumed [AMENDED 11-28]

A completion marker alone proves nothing if the data it vouches for is still in memory when the power goes. So:

- Each file in a generation is flushed to the physical disk (the full flush macOS provides, as the existing show saving does), then **read back and checked** against what was written (size and checksum).
- The generation's folder is flushed, the marker is written and flushed, and the parent folder is flushed, in that order. The marker records the checksum of every file.
- **At restore, every file is verified against the marker's checksums** before anything is offered. A generation that fails is treated as incomplete, and the previous one is used.
- The older generation is removed only after the newer one has passed its read-back check.

This does not claim safety against a failing disk. It does mean that a generation offered for restore has been confirmed on disk and confirmed whole.

### 17.5 What it reuses

The transaction-safe saving, the unsaved-changes tracker, and the copy-without-dropout loop snapshots that are already built and tested. No new audio-thread code. The audio thread is never blocked, never allocates, and never copies a whole loop for this.

### 17.6 Automated tests (outline; full list comes with the scope)

- Each row of 17.4, with failures injected at every step of a write and of a restore.
- Every concurrency case in 17.4a, with the background write paused at each step.
- Durability (17.4b): a generation whose file was truncated or altered after its marker was written is rejected at restore.
- The saved project and its audio folder are byte-for-byte unchanged by any number of recovery writes.
- A crash at any point leaves exactly one complete generation readable.
- Recovery writes during playback, recording and overdub pass the existing dropout detector.
- Don't Save and clean quit leave nothing to restore.
- Plugin builds never create a recovery folder.

### 17.7 Real-hardware acceptance

1. Play for 20 minutes and record three loops, one of them long. Force-quit. Reopen and choose Restore: loops, patterns, sounds and song are all there and play in time.
2. During playback and while recording, listen through several recovery writes: no dropout, click or stutter.
3. Quit normally and choose Don't Save. Reopen: nothing is offered.
4. With a hosted Arturia sound loaded, repeat step 1: the sound returns.
5. Start a new untitled idea, record one loop, force-quit, reopen: it is offered back.
6. Check the saved project's file date after all of this: unchanged until you pressed Save.

### 17.7a Findings from the code after #137 (2026-11-28)

- The unsaved-changes model, and quit protection with it, is still switched off by default. Recovery is built behind the same switch. Switching all three on for everyday use is a separate, later step.
- Recovery writes run in the background and copy only audio that changed, because today's saving runs on the screen's thread and copies every loop.
- A recovery copy holds only what differs from the saved project, and refers to audio already in it.

### 17.8 Not in this PR

The `.kbox` format. Any change to how Save works. The phone, which already saves on every change. Any later roadmap item.

---


### 17.9 Owner approval and added acceptance requirements (2026-11-28)

Approved: built behind the existing switch, off by default; recovery copies in the application's support folder; restored work returns as unsaved changes on the original project, and the saved version is not modified; Discard invalidates and removes the recovery copy at once.

Added requirements:

- Publication is atomic and durable, including directory metadata, not checksums alone.
- The last valid recovery generation is kept until its replacement is committed.
- A stale background write cannot bring back discarded work.
- Recovered work stays protected if the application crashes again before it is saved.
- Several running copies of the application cannot overwrite or discard each other's recovery data.
- Recovery failures are visible without interrupting playback or recording.
- All existing recording, saving and Logic workflows are preserved.

After autosave, crash recovery and quit protection have passed automated and real-hardware acceptance, a separate small PR switches all three on by default. Basic protection against losing musical work should not require configuration.

## 18. Decisions: all settled on 2026-11-28

| # | Decision | Outcome |
|---|---|---|
| 1 | Autosave with crash recovery as the next PR after quit protection | **Approved** |
| 2 | Old shows and chance | **Existing projects keep Free. New projects default to Repeatable.** |
| 3 | Parameter control | **Layered: base, automation, modulation, event locks, manual override. Exclusive ownership only as a first implementation where necessary; built to combine. Per-setting rules defined first.** (3.5) |
| 4 | Names "Editable layer" and "Keep" | **Approved** |
| 5 | Pocket 16-layer layout | **Chosen only after real-phone mockups** |
| 6 | MPC2000XL export of round-robin pads | **First sample only, with a clear warning** |
| 7 | First LIFE prototype | **The drum machine. Musical approval required before any other instrument.** |

No decision is open. The next approval point is the autosave scope, presented after PR #137 is accepted.
---

## 19. Status log

Updated in place as work lands. A blank cell means "not yet".

| Item | Implemented | Automatically tested | Hardware validated | Musically accepted |
|---|---|---|---|---|
| 0.1 Quit protection (PR #137) | Merged 2026-11-28 (main 5be76c1). **Switched off by default** with the unsaved-changes model. | Yes | Yes, in an isolated test app on the owner's Mac, including real recording and overdub | Not yet: needs the model switched on for everyday use |
| 0.2 Autosave and crash recovery | Scope approved 2026-11-28 (four decisions yes, seven added acceptance requirements, section 17.9). Build in progress, behind the existing switch. | | | |
| 0.2a Switch on by default: unsaved-changes model, quit protection, recovery | Not started. A separate small PR, presented only after 0.1 and 0.2 pass automated and real-hardware acceptance. | | | |

Features merged before this roadmap are listed with their hands-on status in section 13 and in `KALEIDOBOX_TOOLS_ANALYSIS.md` (section 11).

---

## What this plan deliberately does not do

- No feature, instrument or workflow is removed.
- No second pattern system: LIFE reads the patterns that exist.
- No second looper or song model: the phone's layer recipe and note pattern move into shared code and the Mac adopts them.
- No broad refactor. Shared-code moves happen only where the roadmap would otherwise build the same thing twice.
- No change to saving until quit protection is accepted.
- No implementation of the full roadmap. One small PR at a time, each approved.
