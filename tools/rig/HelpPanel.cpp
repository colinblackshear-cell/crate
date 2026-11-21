#include "HelpPanel.h"

#include "core/ui/Theme.h"

namespace Theme = crate::Theme;

const std::vector<HelpPanel::Guide>& HelpPanel::guides()
{
    static const std::vector<Guide> list {
        { "Getting started",
          { "Pick your audio interface and keyboard: Settings > Audio + MIDI.",
            "Starting from nothing? New song (top bar) saves the song you're on and clears everything. New pattern then starts "
            "the first thing: a beat, a bass line, keys, the vocoder, a loop or a sample, on an empty pattern.",
            "Back and Forward (left of the screen buttons, or Cmd+[ and Cmd+]) step through the screens you've been on.",
            "Set the tempo at the top (or tap it), and switch the click on if you want one.",
            "Make a beat and a bass line on Play (F1), or record loops: pick Loops on the left, then Edit (F2).",
            "Press Play (or the space bar). Everything plays in time with Kaleidobox's clock.",
            "Off the beat? Quantize (top bar, under Tap) pulls what's on screen onto a grid: 1/4 to 1/32, triplets, a strength "
            "and swing. It works on the MPC, the drum machine, Bass, Synth arp and loops. Cmd+U does it straight away; Undo puts it back.",
            "Build the song on Song (F3): sections in order, and what each track plays in each.",
            "Save show (Cmd+S) keeps everything. Export for Logic gives you the WAV files." },
          "Use headphones when the mic is on, so the speakers don't feed back into it." },

        { "Capture: Rec take and To Song",
          { "Pick a track on the left and dial it in: its sound and pattern on Play (F1), or the whole tool on Edit (F2). Double-click a track to open the whole tool.",
            "Happy with a pattern? Under the tracks, To Song locks it into the song: into a section (it says what it replaces), into every section, "
            "or as a new section at the end. It's the pattern as it is now (Drums Pattern 2, MPC Seq 1, Synth Arp...).",
            "Want to play it live instead (the vocoder over the beat, a bass line, a guitar)? Start the beat, then press Rec take under the tracks. "
            "It starts at the next bar and records what you play onto the next empty loop.",
            "Press it again (it says Stop take) and the take closes at the end of that bar and plays round with the beat.",
            "Then To Song > Your take puts that loop into a section, or a new one.",
            "Not right? Undo on the Song screen (Cmd+Z) takes back a To Song. Clear the loop on Loops (Edit) and take it again." },
          "Rec take works on every track: the MPC (pads you play), Drums, Bass, Synth, Vocoder, Sampler, Line in, and Loops (the next layer from what it records from)." },

        { "Play (F1): patterns",
          { "Pick a track on the left: Drums, Bass, Synth, Vocoder, Sampler or Loops.",
            "Click the 16 steps to program its pattern. The bar button shows the other bars of the beat.",
            "Your keyboard's keys and its 8 knobs follow the picked track.",
            "Mute or solo tracks on the left; a muted track keeps going silently, so it comes back in time.",
            "The song's sections along the bottom (A-H): the next empty one saves what's playing as a section; a full one plays it from the next bar." },
          "Patterns play by themselves. To play over them, use the keys, or record a loop on top (Loops, Edit)." },

        { "Vocoder (pick it, then Edit): sing live",
          { "Switch the mic on (top bar) and wear headphones.",
            "Start from your raw voice, then switch pedals on one at a time: Auto-Tune, Robot, Rhythm, Effects.",
            "The Robot needs a chord: play keys, or pick a chord on the Vocoder screen.",
            "Mix (right of the pedals): the Voice fader is how much of your own voice you hear, Robot is the effect. Turn them on the fly.",
            "Full window shows every setting on one page, each part with its switch, plus the keys.",
            "To keep a vocal part, record it on a loop: Loops > Edit > Record from > Vocoder." },
          "" },

        { "Loops (pick it, then Edit): record loops",
          { "Pick what to record under Record from: Mic, Line in (a guitar or keytar), Vocoder, or Master (everything).",
            "Pick a length (1, 2, 4 or 8 bars), or Free to record until you press again.",
            "Press Rec next layer. It starts at the next bar (with the clock stopped, the clock starts with it).",
            "Press Overdub to play over the layer you picked last; Undo takes the last overdub back.",
            "Double-click a loop to trim where it starts and ends. Its effect slot adds a filter, delay, reverb...",
            "Keys 1-8 record, stop or overdub layers 1-8.",
            "Clear empties the layer you picked (its effects stay). Clear all loops (by the loop bank) starts fresh: all the layers empty, their effects off. Undo brings them back.",
            "Loop bank: Save bank names this batch of loops. + New batch keeps them and gives you empty layers. "
            "Back and Next (or click the bank's name) bring a batch back, at the next bar while playing." },
          "Loops (and Rec takes) can be up to an hour long. A short loop and a long one play together in time." },

        { "Automation: record knob moves (Rec moves, under the tracks)",
          { "Press Rec moves. From the next bar (with the clock stopped, the clock starts), every setting you turn is recorded: "
            "the bass's Cutoff sweeping, the synth's filter, an effect's Mix or Feedback, on screen or with a learned knob.",
            "Press Rec moves again to stop at the end of the bar. Each setting you moved gets a lane that plays back on the beat, "
            "round and round (Loop), or once along the song from where you recorded it (Once: the default over the song).",
            "Automation (beside it, or Cmd+Shift+A) shows the lanes: drag on one to draw new moves, halve or double its length, "
            "switch it on or off, Clear one or Clear all (Undo brings them back). Read off stops them all moving.",
            "Move a knob that has a lane and you have it while you move it; let go and the lane carries on.",
            "Record again on the same setting to replace its lane. Switches (patterns, waves, on/off) aren't recorded.",
            "Lanes are saved with the show. What you record for Logic (Record set, To Logic) has the moves in it, "
            "and Export for Logic adds Automation.mid: each lane as a MIDI CC." },
          "Lanes follow the beat, so in Logic they follow Logic's bars." },

        { "Sampler and MPC (pick one, then Edit): play the pads like an MPC",
          { "Your keyboard's drum pads (Launchkey in Drum mode, or a Launchpad in Drum mode) play the 16 pads of the bank on screen. "
            "A Launchpad plays all four banks at once. Another controller (MPD, APC, MPK mini...)? "
            "Settings > Audio & MIDI > MIDI controllers says what it is; Learn pad 1 sets where its pads start.",
            "Sounds (under the pads) opens your sound bank: click a sound to hear it, double-click to put it on the picked pad.",
            "FX (by the knobs) turns the picked pad's filter, decay, delay and reverb, live while it plays.",
            "16 Levels spreads the picked pad over all 16 pads: Tune plays it as a scale, Filter opens up, Velocity soft to loud, Decay short to long.",
            "Repeat: hold a pad and it plays again every 1/16 (or the rate you pick), in time. Record with the click on to keep the repeats.",
            "The Sequencer + Kits side (beside the same pads) is the rest of the MPC: Play, Rec and steps, songs, your kits, and the picked pad's settings. Tapping a pad only plays it; Edit (or a triple-click on the pad) opens its sound editor.",
            "Remove A01 (over the pads), or right-click a pad, takes its sound off. Undo remove puts it back.",
            "Beat on/off: the MPC's sequence plays whenever the Rig plays, or stays quiet (the pads still play). Its Play, Stop and Rec are the Rig's.",
            "Loop pads records the MPC into the next empty looper layer from the next bar (Free, or 1 to 16 bars). Switch Beat off to loop just what you play." },
          "Hits you record with 16 Levels keep their level when the sequence plays them back." },

        { "Build a song (Song, F3)",
          { "Make your sounds first: a beat and a bass line on Play, loops on Loops, pads on the Sampler. "
            "To Song (under the tracks, on any screen) drops what a track plays, or a take you just recorded, straight into a section.",
            "Open Song (F3). Your song is a row of sections (Intro, Verse, Chorus...), each as wide as its bars. "
            "Down the side are the tracks you're using.",
            "+ Section adds one at the end: empty, or one that plays what's on now. (From any screen: New pattern > Keep what's playing.)",
            "Each box is what one track plays in that section, by name: Beat 1, Pattern 2, Guitar. An empty box is quiet.",
            "Click a box. The panel on the right picks what it plays (which sequence or pattern), how it comes in and goes out "
            "(straight, or a fade), which bar it comes in on, and its level. Double-click an empty box: it plays what that track has on now.",
            "Right-click a box: Same as another section, copy it to the next or to every section, or Edit (opens that track's screen).",
            "Click a section's name for its length, name and loop bank, or to copy, move or delete it. Drag its right edge for more or fewer bars.",
            "Delete (top of the screen, or the Delete key) deletes the picked section and its boxes; what comes after moves up. "
            "Undo (or Cmd+Z) brings it back, and takes back any other change to the song.",
            "Click a track on the left: its row lights up in the picked section. Double-click it: its whole tool.",
            "Jam or Song decides what the big Play (and space) does: Jam plays whatever you've switched on; Song plays this song "
            "from the green marker. Click the ruler to move the marker.",
            "From start plays the song from bar 1 over the click. Loop section plays the picked one over and over while you practise.",
            "Bars view is the same song bar by bar, for fine edits: a loop that comes in on bar 3, a fade across two sections." },
          "Jump to any section live with the A-H buttons along the bottom or keys 1-8: it goes there at the next bar. "
          "The ... menu puts Next and Back on your keyboard's buttons." },

        { "Compose on the timeline",
          { "Record your loops and make your patterns first. Then open Song (F3) > Bars view: the same song, bar by bar.",
            "Each loop (1-8) and each tool (MPC, Bass, Synth, Vocoder, Sampler, Input) has a lane. Click a lane to put a block on it: "
            "that's where it plays.",
            "Drag a block to move it. Drag its right edge to make it longer (a loop repeats; thin lines show where), its left edge to start it later.",
            "Click a block, then drag the small squares at its top corners to fade it in or out, or right-click it for fades, copy and delete. "
            "Fade one layer out while another fades in.",
            "Click the ruler to pick where Play starts. Drag along the ruler to cycle those bars. Turn the Click on for a click track.",
            "Name sections in the row above the bars (right-click: Verse, Chorus...). While the song plays, click a section to jump there at the next bar.",
            "A lane with no blocks says \"live\": the timeline leaves it alone, so you can sing through the vocoder or play your guitar over the song.",
            "Rec onto song: record a loop while the song plays and it lands on its lane where you played it.",
            "Record song plays it once and keeps every tool as its own WAV. Export for Logic gives the loops laid out over the song." },
          "The sections on the Song screen are the named sections here: change one and the other follows. Cmd+Z undoes, Delete removes a block, Cmd+D copies it." },

        { "Save and export",
          { "Save show (top bar, or Cmd+S): the show and its loops, together. The first time it asks for a name.",
            "The show menu (your show's name, top left) has Save show as... for a copy under a new name, and Open show...",
            "Export for Logic (Song screen, or More): every loop as a WAV (with its effects and dry), each loop laid out over "
            "your song, and a MIDI file with the tempo and a marker per part. Every file starts on bar 1.",
            "Rec set (top bar) records the whole set as you play it: a WAV per tool, the mix, and the notes as MIDI, in Music > Crate > Rig > Recordings.",
            "In Logic: set the same tempo and drag the files in at bar 1." },
          "" },

        { "Play live and sing over it",
          { "Build the song (see Build a song), then switch the mic on and pick your vocoder sound.",
            "Switch the Song screen to Song and press Play, or jump between sections yourself with the A-H buttons.",
            "Sing: your voice goes through the vocoder live. Record a part of it onto a loop if you want it to repeat.",
            "Press Rec (top bar) first to keep the whole performance as stems for Logic.",
            "Mix (F4) has every level, pan, mute and solo; its FX pad button swaps the strips for the FX pad on the whole mix (filter sweeps, repeat, tape stop)." },
          "" },

        { "Guitar, bass, keytar and other instruments",
          { "Plug it into an input of your audio interface (the instrument or Hi-Z input, for a guitar or bass).",
            "Pick Line in on the left, then Edit (F2). Press Plug in, and pick input 1, 2 or both.",
            "Too quiet? Turn up Gain in. The tuner beside it shows the note you play.",
            "Stomp the pedals you want (Amp, Wah, Chorus, Delay...), or pick a board from Presets: Clean guitar, Crunch, Bass DI...",
            "Rec to looper puts it on the next loop layer from the next bar; Rec to sampler or Rec to MPC pad samples it. "
            "What you record has the pedals on it.",
            "Mix (F4) has the Line in strip too: its level, mute and sends.",
            "Every other sound has its own board too: pick the Bass, a loop or the Master on the left of the Effects screen.",
            "A keytar with MIDI can also play Kaleidobox's instruments, like any keyboard." },
          "" },

        { "Effects: pedals on any sound",
          { "Open it from FX on any Mix strip. Pick a sound in its list: the Line in, the Vocoder, Bass, Synth, MPC, Drums, Sampler, any loop, all the loops, or the Master.",
            "Stomp a pedal's footswitch to switch it on; turn its knob. Click the pedal to open all its settings below.",
            "Presets has ready-made boards for guitar, bass, keys, drums and anything; Save keeps yours for any sound.",
            "Copy and Paste put one sound's board on another, to change from there.",
            "Bypass switches the whole board out for a moment; Reset all switches every pedal off.",
            "The Bass and Synth screens' Effects button, and All pedals on any FX panel, come straight here." },
          "" },
    };
    return list;
}

int HelpPanel::guideFor (const juce::String& page)
{
    const auto& all = guides();
    auto find = [&] (const char* start)
    {
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].title.startsWith (start))
                return i;
        return 0;
    };
    if (page == "Groove") return find ("Play (F1)");
    if (page == "Vocoder") return find ("Vocoder");
    if (page == "Looper") return find ("Loops");
    if (page == "Sampler" || page == "MPC") return find ("Sampler and MPC");
    if (page == "Song") return find ("Build a song");
    if (page == "Timeline") return find ("Compose on the timeline");
    if (page == "Mixer") return find ("Play live");
    if (page == "Setup") return find ("Guitar");
    if (page == "Effects") return find ("Effects");
    return 0;
}

const std::vector<HelpPanel::Stop>& HelpPanel::tour()
{
    static const std::vector<Stop> list {
        { "Welcome",
          "This walkthrough goes through all of Kaleidobox on its real screens, in the order you'd make a song: dial in a sound, play over "
          "the beat, record a take, lock it into the song, arrange it, mix it, save it.\n\n"
          "Next moves on and shows you each place. Try things at every stop: nothing here is a demo. Close it any time; the Guide button "
          "(top right, always there) brings you back to the stop you were on.",
          "Play:Drums" },
        { "The top bar",
          "Left to right:\n\n"
          "- Your show's name: its menu has New song, Open, Save (Cmd+S), Save as, Export for Logic. A dot means unsaved changes.\n"
          "- Play / Stop (or the space bar) run everything on Kaleidobox's clock. Rec set records the whole set as you play it, a WAV per track.\n"
          "- The tempo: + and - nudge it, Tap sets it. Quantize (under Tap) pulls what's on screen onto the grid; Cmd+U does it at once.\n"
          "- The window in the middle shows where the song is. When the song is in charge of what plays it turns green, with Jam to take over.\n"
          "- The meters, what's plugged in, and the Mic lamp (click it to switch your mic on).",
          "" },
        { "The view buttons",
          "Under the top bar:\n\n"
          "- Back and Forward (Cmd+[ and Cmd+]) step through the screens you've been on.\n"
          "- Play (F1): the picked track's quick strip: steps, patterns, 8 knobs, keys.\n"
          "- Edit (F2): the picked track's whole tool: the MPC, the 808, the Bass and Synth panels, the vocoder's pedalboard, the Looper.\n"
          "- Song (F3): the song, section by section. Mix (F4): faders, effects and the FX pad.\n"
          "- Chords (F7) and Sounds (F5) open as drawers over any view. Settings (F6): audio, MIDI, keys, look.\n"
          "- Guide: this.",
          "" },
        { "The tracks on the left",
          "Every instrument is a track down the left: MPC, Drums (the 808), Bass, Synth, Vocoder, Sampler, Loops, Line in. "
          "They're the real tools, not copies.\n\n"
          "- Click one to pick it. The view follows it: Play shows its steps, Edit its whole tool, Song lights its row, Mix opens its effects. "
          "Your keyboard and its knobs play the picked track.\n"
          "- Double-click one: its whole tool, from any view.\n"
          "- On each: its Play (or On), M to mute (it keeps going silently), S to solo. The lamp says Playing, Quiet, Rec or Muted.\n"
          "- Under them: Rec take and To Song, the two capture buttons (coming up). Above them: + New / start over.",
          "Play:Drums" },
        { "Make a beat (Play, F1)",
          "Drums is picked. Each row is a sound of the 808, the top row is the accent; click steps to put hits in. A and B are two variations "
          "of the pattern, the numbered buttons are its patterns.\n\n"
          "Press Play on the Drums track (or the space bar) to hear it. Turn the 8 knobs. Clear empties the pattern (Undo or Cmd+Z puts it back). "
          "Quantize tightens what you played in.",
          "Play:Drums" },
        { "The whole drum machine (Edit, F2)",
          "This is the full 808: every sound's level, tune, decay and tone, pattern lengths, rolls, A/B and fills. What you change here is what "
          "Play shows: it's one machine.\n\n"
          "For pads, sampled kits and sequences, the MPC track is a full MPC (pick it, then Edit).",
          "Edit:Drums" },
        { "Find sounds (Sounds, F5)",
          "The Sounds drawer has the library: kits, bass and synth sounds, your own samples and Splice folder. Click to hear; double-click to put "
          "a sound on the picked track or pad. Close it with Sounds again, or Esc.",
          "Sounds" },
        { "Lock the beat into the song (To Song)",
          "Happy with the beat? Press To Song (under the tracks) > As the first section. The song now has section A (Intro), and in it Drums "
          "plays the pattern you made.\n\n"
          "Make another pattern and To Song puts it into another section, or a new one at the end. The menu says what each choice would replace.",
          "Play:Drums" },
        { "A bass line",
          "Bass is picked. Pick its sound with < and > (or click the name for the whole list). Click steps for notes and drag one up or down "
          "to change its note; the keys on screen, the computer's keys (A W S E D...) and your Launchkey play it live. Press Play on the Bass track.\n\n"
          "Then To Song > Into A: section A now has the beat and the bass.",
          "Play:Bass" },
        { "Keys and chords",
          "Synth plays keys or an arpeggio, with its sound and 8 knobs here and its whole panel on Edit (Arturia synths too, from Settings > Your plugins).\n\n"
          "Chords (F7) builds your own chords on 16 pads with a progression; the vocoder can play them too.",
          "Play:Synth" },
        { "Set up the vocoder",
          "Switch the mic on (Mic, top right) and wear headphones. Start from your raw voice, then stomp pedals one at a time: Auto-Tune, Robot, "
          "Rhythm, Effects. The Robot needs a chord: play keys, or pick a chord here. The Mix faders set your voice and the robot.\n\n"
          "Now press Play at the top: the beat runs while you set it, so you test the vocoder over the drums.",
          "Edit:Vocoder" },
        { "Record a take over the beat (Rec take)",
          "With the beat playing and the Vocoder picked, press Rec take (under the tracks). It lights up and waits for the next bar, then "
          "records you; the button says Stop take. Press it at the end of your part: the take closes at the end of that bar and plays round "
          "with the beat, on the next empty loop.\n\n"
          "Rec take works on any track: a bass line you play on the keys, the MPC pads, a guitar on Line in.",
          "Play:Vocoder" },
        { "Put the take into the song",
          "To Song > Your take (loop 1) > Into A, or as a new section. Now the song has your beat, your bass and your vocal.\n\n"
          "Not happy with the take? Loops (on the left), then Edit: Clear or Redo the loop, and take it again.",
          "Play:Vocoder" },
        { "Loops: layer on the fly",
          "The Looper is sixteen layers, all in time. Record from picks the source (Mic, Line in, Vocoder, a tool, Master). Rec next layer starts "
          "at the next bar; pick 1, 2, 4 or 8 bars, or Free. Overdub plays over a layer, Undo takes the overdub back, each layer has its own "
          "effects. Save bank keeps the loops and gives you empty ones.",
          "Edit:Loops" },
        { "Arrange (Song, F3)",
          "Your song is a row of sections, each as wide as its bars; down the side, the tracks in it. Each box says what that track plays there.\n\n"
          "- Click a box: the panel on the right picks what it plays, how it fades in and out, the bar it comes in on, its level.\n"
          "- Double-click a box: its tool opens on that pattern, to change it.\n"
          "- + Section adds one (empty, or what's playing). Drag a section's right edge for more or fewer bars.\n"
          "- Click a track on the left: its row lights up.",
          "Song" },
        { "Copy, move, delete",
          "Click a section's name: copy it, move it left or right, rename it (or double-click the name), loop it.\n\n"
          "Delete (top of the Song screen, or the Delete key) deletes the picked section and its boxes; what comes after moves up. "
          "Undo (or Cmd+Z) brings it back, and takes back any change to the song, To Song included.\n\n"
          "Right-click a box: Same as another section, Copy to every section, Empty this box.",
          "Song" },
        { "Play the song",
          "Jam or Song decides what the big Play does. Jam: whatever you've switched on. Song: this song, section by section, from the green "
          "marker (click the ruler to move it). From start plays it from bar 1; Loop section repeats the picked one while you practise.\n\n"
          "The A-H keys along the bottom (or keys 1-8) jump to a section at the next bar. While the song decides, the window at the top is "
          "green: Jam hands it back to you, so you can edit what's playing.",
          "Song" },
        { "Mix (F4)",
          "Every track's fader, pan, mute and solo, the loops too. FX on a strip opens its effects; All pedals opens the full pedalboard. "
          "FX pad swaps the strips for a performance pad on the whole mix: filter sweeps, repeat, tape stop.",
          "Mix" },
        { "Save, and take it to Logic",
          "Save (Cmd+S) keeps the whole show: sounds, patterns, the song and the loops. The show menu (top left) has Save as and Open.\n\n"
          "For Logic: Rec song (Song screen) plays the song once and keeps every track as its own WAV, plus MIDI; Export for Logic gives "
          "the loops laid out over the song. Each tool is also a plugin in Logic, with its own Rec and drag chips.",
          "Song" },
        { "That's the whole loop",
          "Dial in a sound, play it over the beat, Rec take or To Song, arrange on Song, mix, save. Then do it again for the next part.\n\n"
          "All how-tos (below) has a step-by-step for every screen, and each control says what it does when you hover over it.",
          "" },
    };
    return list;
}

HelpPanel::HelpPanel (const juce::String& page)
{
    setWantsKeyboardFocus (true);
    const auto& all = guides();
    for (int i = 0; i < (int) all.size(); ++i)
    {
        auto* b = list.add (new juce::TextButton (all[(size_t) i].title));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { show (i); };
        addAndMakeVisible (b);
    }
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (close);
    tourButton.onClick = [this] { startTour (0); };
    tourButton.setColour (juce::TextButton::buttonColourId, Theme::accent.withAlpha (0.35f));
    addAndMakeVisible (tourButton);
    back.onClick = [this] { showStop (stop - 1); };
    next.onClick = [this]
    {
        if (stop + 1 >= (int) tour().size())
            setTouring (false);
        else
            showStop (stop + 1);
    };
    allHowTos.onClick = [this] { setTouring (false); };
    for (auto* b : { &back, &next, &allHowTos })
        addChildComponent (b);
    body.setMultiLine (true, true);
    body.setReadOnly (true);
    body.setCaretVisible (false);
    body.setScrollbarsShown (true);
    body.setFont (juce::FontOptions (16.0f));
    body.setColour (juce::TextEditor::backgroundColourId, Theme::background);
    body.setColour (juce::TextEditor::textColourId, Theme::text);
    body.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (body);
    show (guideFor (page));
}

void HelpPanel::startTour (int first)
{
    setTouring (true);
    showStop (first);
}

void HelpPanel::setTouring (bool on)
{
    touring = on;
    for (auto* b : list)
        b->setVisible (! on);
    tourButton.setVisible (! on);
    for (auto* b : { &back, &next, &allHowTos })
        b->setVisible (on);
    if (! on)
        show (shown);
    if (onModeChanged)
        onModeChanged();
    resized();
    repaint();
}

void HelpPanel::showStop (int index)
{
    const auto& all = tour();
    stop = juce::jlimit (0, (int) all.size() - 1, index);
    const auto& s = all[(size_t) stop];
    body.setText (s.title + "\n\n" + s.text, false);
    body.moveCaretToTop (false);
    back.setEnabled (stop > 0);
    next.setButtonText (stop + 1 >= (int) all.size() ? "Finish" : juce::String::fromUTF8 ("Next \xe2\x80\xba"));
    repaint();
    if (s.place.isNotEmpty() && onPlace)
        onPlace (s.place);
}

void HelpPanel::show (int guide)
{
    const auto& all = guides();
    shown = juce::jlimit (0, (int) all.size() - 1, guide);
    const auto& g = all[(size_t) shown];
    juce::String text;
    text << g.title << "\n\n";
    for (int i = 0; i < g.steps.size(); ++i)
        text << juce::String (i + 1) << ".  " << g.steps[i] << "\n\n";
    if (g.note.isNotEmpty())
        text << g.note << "\n";
    body.setText (text, false);
    body.moveCaretToTop (false);
    for (int i = 0; i < list.size(); ++i)
        list[i]->setToggleState (i == shown, juce::dontSendNotification);
}

void HelpPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::outline);
    g.drawRect (getLocalBounds(), 1);
    g.setColour (Theme::accent);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    auto top = getLocalBounds().reduced (16, 12).removeFromTop (28);
    if (touring)
    {
        g.drawText ("GUIDE", top, juce::Justification::centredLeft);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Walkthrough " + juce::String (stop + 1) + " of " + juce::String ((int) tour().size()),
                    top.withTrimmedLeft (80).withTrimmedRight (90), juce::Justification::centredLeft);
        // The stops, as dots.
        const int n = (int) tour().size();
        auto dots = getLocalBounds().reduced (16, 0).removeFromBottom (12).withTrimmedBottom (4);
        const float step = juce::jmin (14.0f, (float) dots.getWidth() / (float) n);
        for (int i = 0; i < n; ++i)
        {
            g.setColour (i == stop ? Theme::accent : i < stop ? Theme::accent.withAlpha (0.45f) : Theme::outline);
            g.fillEllipse ((float) dots.getX() + step * (float) i, (float) dots.getY(), 6.0f, 6.0f);
        }
        return;
    }
    g.drawText ("GUIDE", top, juce::Justification::centredLeft);
}

void HelpPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 12);
    auto top = area.removeFromTop (28);
    close.setBounds (top.removeFromRight (80));
    area.removeFromTop (10);
    if (touring)
    {
        area.removeFromBottom (14);   // the dots
        auto buttons = area.removeFromBottom (34);
        area.removeFromBottom (8);
        allHowTos.setBounds (buttons.removeFromLeft (120));
        next.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (8);
        back.setBounds (buttons.removeFromRight (90));
        body.setBounds (area);
        return;
    }
    auto left = area.removeFromLeft (juce::jmin (260, area.getWidth() / 3));
    tourButton.setBounds (left.removeFromTop (44).reduced (0, 2));
    left.removeFromTop (8);
    for (auto* b : list)
    {
        b->setBounds (left.removeFromTop (36).reduced (0, 2));
    }
    area.removeFromLeft (14);
    body.setBounds (area);
}

bool HelpPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    if (touring && key == juce::KeyPress::rightKey)
    {
        next.triggerClick();
        return true;
    }
    if (touring && key == juce::KeyPress::leftKey && stop > 0)
    {
        showStop (stop - 1);
        return true;
    }
    return false;
}
