#include "HelpPanel.h"

#include "core/ui/Theme.h"

namespace Theme = crate::Theme;

const std::vector<HelpPanel::Guide>& HelpPanel::guides()
{
    static const std::vector<Guide> list {
        { "Getting started",
          { "Pick your audio interface and keyboard: More > Audio and MIDI set-up.",
            "Set the tempo at the top (or tap it), and switch the click on if you want one.",
            "Make a beat and a bass line on the Groove screen (F1), or record loops on the Looper (F3).",
            "Press Play (or the space bar). Everything plays in time with the Rig's clock.",
            "Build the song: Looper > Timeline (lay it out bar by bar, with fades) or Looper > Song grid (parts you switch between).",
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
            "To keep a vocal part, record it on a loop: Looper > Record from > Vocoder." },
          "" },

        { "Looper (F3): record loops",
          { "Pick what to record under Record from: Mic, Line in (a guitar or keytar), Vocoder, or Master (everything).",
            "Pick a length (1, 2, 4 or 8 bars), or Free to record until you press again.",
            "Press Rec next layer. It starts at the next bar (with the clock stopped, the clock starts with it).",
            "Press Overdub to play over the layer you picked last; Undo takes the last overdub back.",
            "Double-click a loop to trim where it starts and ends. Its effect slot adds a filter, delay, reverb...",
            "Keys 1-8 record, stop or overdub layers 1-8." },
          "Loops can be up to 10 minutes long. A short loop and a long one play together in time." },

        { "Build a song",
          { "Record a few loops (and/or make patterns on the Groove screen).",
            "Open Looper > Song grid.",
            "+ Part keeps what's playing now as a part. Make a few: intro, verse, chorus...",
            "Click the cells: lit plays in that part, dark is muted. Loops and tools are both in the grid.",
            "Click a part's length to set how many bars it plays (or copy, update or delete it).",
            "Press Play song. In song mode each part plays its bars, then the next one starts by itself.",
            "A loop that comes in with a part starts from its top on the part's first beat." },
          "You can still jump to any part live: click its name, or use the song part buttons along the bottom." },

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
          "Already built parts in the Song grid? From parts... lays them out on the timeline to shape further. Cmd+Z undoes, Delete removes a block, Cmd+D copies it." },

        { "Save and export",
          { "Save show (top bar, or Cmd+S): the show and its loops, together. The first time it asks for a name.",
            "More > Save show as... saves a copy under a new name; More > Open show... opens one.",
            "Export for Logic (Song grid, or More): every loop as a WAV (with its effects and dry), each loop laid out over "
            "your song, and a MIDI file with the tempo and a marker per part. Every file starts on bar 1.",
            "Rec (top bar) records the whole set as you play it: a WAV per tool, the mix, and the notes as MIDI, in Music > Crate > Rig > Recordings.",
            "In Logic: set the same tempo and drag the files in at bar 1." },
          "" },

        { "Play live and sing over it",
          { "Build the song (see Build a song), then switch the mic on and pick your vocoder sound.",
            "Press Play song, or launch parts yourself from the song part buttons.",
            "Sing: your voice goes through the vocoder live. Record a part of it onto a loop if you want it to repeat.",
            "Press Rec (top bar) first to keep the whole performance as stems for Logic.",
            "The Stage screen (F5) shows everything at once, with the FX pad for the whole mix." },
          "" },

        { "Guitar, keytar and other instruments",
          { "Plug it into an input of your audio interface.",
            "In the mixer (More > Mixer), the Input strip plays it: switch it on, and pick input 1, 2 or both.",
            "Give it effects with the strip's FX button.",
            "To loop it: Looper > Record from > Line in. Picking Line in switches the Input strip on for you.",
            "A keytar with MIDI can also play the Rig's instruments, like any keyboard." },
          "" },
    };
    return list;
}

int HelpPanel::guideFor (const juce::String& page)
{
    if (page == "Groove") return 1;
    if (page == "Vocoder") return 2;
    if (page == "Looper") return 3;
    if (page == "Song") return 4;
    if (page == "Timeline") return 5;
    if (page == "Stage") return 7;
    if (page == "Mixer") return 8;
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
