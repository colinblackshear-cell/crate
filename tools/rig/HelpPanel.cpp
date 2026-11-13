#include "HelpPanel.h"

#include "core/ui/Theme.h"

namespace Theme = crate::Theme;

const std::vector<HelpPanel::Guide>& HelpPanel::guides()
{
    static const std::vector<Guide> list {
        { "Getting started",
          { "Pick your audio interface and keyboard: More > Audio and MIDI set-up.",
            "Starting from nothing? New song (top bar) saves the song you're on and clears everything. New pattern then starts "
            "the first thing: a beat, a bass line, keys, the vocoder, a loop or a sample, on an empty pattern.",
            "Back and Forward (left of the screen buttons, or Cmd+[ and Cmd+]) step through the screens you've been on.",
            "Set the tempo at the top (or tap it), and switch the click on if you want one.",
            "Make a beat and a bass line on the Groove screen (F1), or record loops on the Looper (F3).",
            "Press Play (or the space bar). Everything plays in time with Kaleidobox's clock.",
            "Build the song: More > Song builder (parts you switch between, in order) or Looper > Timeline (lay it out bar by bar, with fades).",
            "Save show (Cmd+S) keeps everything. Export for Logic gives you the WAV files." },
          "Use headphones when the mic is on, so the speakers don't feed back into it." },

        { "Groove (F1): patterns",
          { "Pick a track on the left: Drums, Bass, Synth, Vocoder, Sampler or Loops.",
            "Click the 16 steps to program its pattern. The bar button shows the other bars of the beat.",
            "Your keyboard's keys and its 8 knobs follow the picked track.",
            "Mute or solo tracks on the left; a muted track keeps going silently, so it comes back in time.",
            "The song parts along the bottom (A-H): an empty one saves what's playing; a full one plays it from the next bar." },
          "Patterns play by themselves. To play over them, use the keys, or record a loop on top in the Looper." },

        { "Vocoder (F2): sing live",
          { "Switch the mic on (top bar) and wear headphones.",
            "Start from your raw voice, then switch pedals on one at a time: Auto-Tune, Robot, Rhythm, Effects.",
            "The Robot needs a chord: play keys, or pick a chord on the Vocoder screen.",
            "Mix (right of the pedals): the Voice fader is how much of your own voice you hear, Robot is the effect. Turn them on the fly.",
            "Full window shows every setting on one page, each part with its switch, plus the keys.",
            "To keep a vocal part, record it on a loop: Looper > Record from > Vocoder." },
          "" },

        { "Looper (F3): record loops",
          { "Pick what to record under Record from: Mic, Line in (a guitar or keytar), Vocoder, or Master (everything).",
            "Pick a length (1, 2, 4 or 8 bars), or Free to record until you press again.",
            "Press Rec next layer. It starts at the next bar (with the clock stopped, the clock starts with it).",
            "Press Overdub to play over the layer you picked last; Undo takes the last overdub back.",
            "Double-click a loop to trim where it starts and ends. Its effect slot adds a filter, delay, reverb...",
            "Keys 1-8 record, stop or overdub layers 1-8.",
            "Loop bank: Save bank names this batch of eight loops. + New batch keeps them and gives you eight empty layers. "
            "Back and Next (or click the bank's name) bring a batch back, at the next bar while playing." },
          "Loops can be up to 10 minutes long. A short loop and a long one play together in time." },

        { "Sampler and MPC (F4): play the pads like an MPC",
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

        { "Build a song",
          { "Open the Song builder: More > Song builder, or the Song button on the Looper. (Or skip it: New pattern > Keep "
            "what's playing as the next song part does the same as + New part, from any screen.)",
            "Get something playing: a beat on the Groove screen, the Bass or Synth, loops on the Looper. Mute what shouldn't play yet.",
            "Press + New part. It keeps what's playing now as the next column: which tracks and loops play, the MPC sequence, "
            "the bass pattern and the arp.",
            "Each column is a part, each row a track. Click a cell: ON plays in that part, OFF is muted. Right-click it for "
            "AS IS (the part leaves that track alone).",
            "Click a part's length (the amber LCD) to set how many bars it plays. Hold stays until you move on.",
            "Double-click a part's name to rename it. Its menu moves it earlier or later, copies it, saves what's playing into it, or deletes it.",
            "Press Play song: it plays from part A, each part for its length, then the next one. The strip at the top shows the "
            "whole song to scale and where you are.",
            "A loop that comes in with a part starts from its top on the part's first beat.",
            "Over each part's loops is its loop bank (from the Looper): click it to pick which batch of loops that part plays." },
          "Jump to any part live: click its letter or its place in the strip, press keys 1-8, or the A-H buttons along the bottom. "
          "Keys puts Next and Back on your keyboard's buttons." },

        { "Compose on the timeline",
          { "Record your loops and make your patterns first. Then open Looper > Timeline.",
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
          "Already built parts in the Song builder? From parts... lays them out on the timeline to shape further. Cmd+Z undoes, Delete removes a block, Cmd+D copies it." },

        { "Save and export",
          { "Save show (top bar, or Cmd+S): the show and its loops, together. The first time it asks for a name.",
            "More > Save show as... saves a copy under a new name; More > Open show... opens one.",
            "Export for Logic (Song builder, or More): every loop as a WAV (with its effects and dry), each loop laid out over "
            "your song, and a MIDI file with the tempo and a marker per part. Every file starts on bar 1.",
            "Rec (top bar) records the whole set as you play it: a WAV per tool, the mix, and the notes as MIDI, in Music > Crate > Rig > Recordings.",
            "In Logic: set the same tempo and drag the files in at bar 1." },
          "" },

        { "Play live and sing over it",
          { "Build the song (see Build a song), then switch the mic on and pick your vocoder sound.",
            "Press Play song, or launch parts yourself from the song part buttons.",
            "Sing: your voice goes through the vocoder live. Record a part of it onto a loop if you want it to repeat.",
            "Press Rec (top bar) first to keep the whole performance as stems for Logic.",
            "The Mixer (F5) has every level, pan, mute and solo; its FX pad button swaps the strips for the FX pad on the whole mix (filter sweeps, repeat, tape stop)." },
          "" },

        { "Guitar, bass, keytar and other instruments",
          { "Plug it into an input of your audio interface (the instrument or Hi-Z input, for a guitar or bass).",
            "Open Effects (F6) and pick Line in. Press Plug in, and pick input 1, 2 or both.",
            "Too quiet? Turn up Gain in. The tuner beside it shows the note you play.",
            "Stomp the pedals you want (Amp, Wah, Chorus, Delay...), or pick a board from Presets: Clean guitar, Crunch, Bass DI...",
            "Rec to looper puts it on the next loop layer from the next bar; Rec to sampler or Rec to MPC pad samples it. "
            "What you record has the pedals on it.",
            "The Mixer (F5) has the Input strip too: its level, mute and sends.",
            "Every other sound has its own board too: pick the Bass, a loop or the Master on the left of the Effects screen.",
            "A keytar with MIDI can also play Kaleidobox's instruments, like any keyboard." },
          "" },

        { "Effects (F6): pedals on any sound",
          { "Pick a sound on the left: the Line in, the Vocoder, Bass, Synth, MPC, Drum machine, Sampler, any loop, all the loops, or the Master.",
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
