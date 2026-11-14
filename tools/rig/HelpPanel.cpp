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
            "Loop bank: Save bank names this batch of eight loops. + New batch keeps them and gives you eight empty layers. "
            "Back and Next (or click the bank's name) bring a batch back, at the next bar while playing." },
          "Loops can be up to 10 minutes long. A short loop and a long one play together in time." },

        { "Sampler and MPC (pick one, then Edit): play the pads like an MPC",
          { "Your keyboard's drum pads (Launchkey in Drum mode, or a Launchpad in Drum mode) play the 16 pads of the bank on screen. "
            "A Launchpad plays all four banks at once.",
            "Sounds (under the pads) opens your sound bank: click a sound to hear it, double-click to put it on the picked pad.",
            "FX (by the knobs) turns the picked pad's filter, decay, delay and reverb, live while it plays.",
            "16 Levels spreads the picked pad over all 16 pads: Tune plays it as a scale, Filter opens up, Velocity soft to loud, Decay short to long.",
            "Repeat: hold a pad and it plays again every 1/16 (or the rate you pick), in time. Record with the click on to keep the repeats.",
            "The Sequencer + Kits side (beside the same pads) is the rest of the MPC: Play, Rec and steps, songs, your kits, and the picked pad's settings. Tapping a pad only plays it; Edit opens its sound editor.",
            "Beat on/off: the MPC's sequence plays whenever the Rig plays, or stays quiet (the pads still play). Its Play, Stop and Rec are the Rig's.",
            "Loop pads records the MPC into the next empty looper layer from the next bar (Free, or 1 to 16 bars). Switch Beat off to loop just what you play." },
          "Hits you record with 16 Levels keep their level when the sequence plays them back." },

        { "Build a song (Song, F3)",
          { "Make your sounds first: a beat and a bass line on Play, loops on Loops, pads on the Sampler.",
            "Open Song (F3). Your song is a row of sections (Intro, Verse, Chorus...), each as wide as its bars. "
            "Down the side are the tracks you're using.",
            "+ Section adds one at the end: empty, or one that plays what's on now. (From any screen: New pattern > Keep what's playing.)",
            "Each box is what one track plays in that section, by name: Beat 1, Pattern 2, Guitar. An empty box is quiet.",
            "Click a box. The panel on the right picks what it plays (which sequence or pattern), how it comes in and goes out "
            "(straight, or a fade), which bar it comes in on, and its level. Double-click an empty box: it plays what that track has on now.",
            "Right-click a box: Same as another section, copy it to the next or to every section, or Edit (opens that track's screen).",
            "Click a section's name for its length, name and loop bank, or to copy, move or delete it. Drag its right edge for more or fewer bars.",
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
            "Rec (top bar) records the whole set as you play it: a WAV per tool, the mix, and the notes as MIDI, in Music > Crate > Rig > Recordings.",
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
    if (page == "Groove") return 1;
    if (page == "Vocoder") return 2;
    if (page == "Looper") return 3;
    if (page == "Sampler" || page == "MPC") return 4;
    if (page == "Song") return 5;
    if (page == "Timeline") return 6;
    if (page == "Mixer") return 8;
    if (page == "Setup") return 9;
    if (page == "Effects") return 10;
    return 0;
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
    g.drawText ("HOW TO", getLocalBounds().reduced (16, 12).removeFromTop (28), juce::Justification::centredLeft);
}

void HelpPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 12);
    auto top = area.removeFromTop (28);
    close.setBounds (top.removeFromRight (80));
    area.removeFromTop (10);
    auto left = area.removeFromLeft (juce::jmin (260, area.getWidth() / 3));
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
    return false;
}
