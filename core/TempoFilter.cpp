#include "TempoFilter.h"

namespace crate::Tempo
{
namespace
{
constexpr double lowest = 20.0, highest = 400.0, nearBy = 0.08;

juce::String number (double v)
{
    return juce::String (v, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
}

bool isNumber (const juce::String& s)
{
    return s.isNotEmpty() && s.containsOnly ("0123456789.") && s.indexOfChar ('.') == s.lastIndexOfChar ('.')
        && s != ".";
}

// "92", "92.5", "90-100", "90–100": false when it isn't one.
bool readNumbers (juce::String s, double& a, double& b)
{
    s = s.replace (juce::CharPointer_UTF8 ("\xe2\x80\x93"), "-").trim();   // an en dash
    if (s.contains ("-"))
    {
        const auto first = s.upToFirstOccurrenceOf ("-", false, false).trim();
        const auto second = s.fromFirstOccurrenceOf ("-", false, false).trim();
        if (! isNumber (first) || ! isNumber (second))
            return false;
        a = first.getDoubleValue();
        b = second.getDoubleValue();
        return true;
    }
    if (! isNumber (s))
        return false;
    a = b = s.getDoubleValue();
    return true;
}

bool inRange (double v) { return v >= lowest && v <= highest; }
}

//==============================================================================
Filter Filter::exactly (double bpm, bool hd)
{
    Filter f;
    if (! inRange (bpm))
        return f;
    f.lo = bpm - 0.5;
    f.hi = bpm + 0.5;
    f.target = bpm;
    f.halfDouble = hd;
    return f;
}

Filter Filter::between (double a, double b, bool hd)
{
    if (a > b)
        std::swap (a, b);
    Filter f;
    if (! inRange (a) || ! inRange (b))
        return f;
    if (a == b)
        return exactly (a, hd);
    f.lo = a;
    f.hi = b;
    f.target = (a + b) * 0.5;
    f.halfDouble = hd;
    return f;
}

Filter Filter::near (double bpm, bool hd)
{
    Filter f;
    if (! inRange (bpm))
        return f;
    f.lo = bpm * (1.0 - nearBy);
    f.hi = bpm * (1.0 + nearBy);
    f.target = bpm;
    f.halfDouble = hd;
    return f;
}

bool Filter::matches (double bpm) const
{
    if (! isActive())
        return true;
    if (bpm <= 0)
        return false;
    for (double k : { 1.0, 0.5, 2.0 })
    {
        if (k != 1.0 && ! halfDouble)
            break;
        if (bpm * k >= lo && bpm * k <= hi)
            return true;
    }
    return false;
}

double Filter::distance (double bpm) const
{
    if (! isActive() || bpm <= 0)
        return 1.0e9;
    double best = std::abs (bpm - target);
    if (halfDouble)
        best = juce::jmin (best, std::abs (bpm * 0.5 - target), std::abs (bpm * 2.0 - target));
    return best;
}

bool Filter::isNear() const
{
    return isActive() && ! isExact() && std::abs (lo - target * (1.0 - nearBy)) < 1.0e-6 && std::abs (hi - target * (1.0 + nearBy)) < 1.0e-6;
}

juce::String Filter::shortText() const
{
    if (! isActive())
        return {};
    if (isExact())
        return number (target);
    if (isNear())
        return "~" + number (target);
    return number (lo) + "-" + number (hi);
}

juce::String Filter::describe() const
{
    if (! isActive())
        return {};
    juce::String s;
    if (isExact())
        s = number (target) + " BPM";
    else if (isNear())
        s = "near " + number (target) + " BPM";
    else
        s = number (lo) + "-" + number (hi) + " BPM";
    return halfDouble ? s + " (or half/double)" : s;
}

//==============================================================================
Filter parse (const juce::String& typed, bool halfDouble)
{
    auto s = typed.trim().toLowerCase();
    if (s.endsWith ("bpm"))
        s = s.dropLastCharacters (3).trim();

    bool near = false;
    for (auto* mark : { "~", "near", "around", "about", "\xe2\x89\x88" })   // last: the "approximately" sign
    {
        const auto m = juce::String (juce::CharPointer_UTF8 (mark));
        if (s.startsWith (m))
        {
            s = s.substring (m.length()).trim();
            near = true;
            break;
        }
    }
    if (s.contains (" to "))
        s = s.replace (" to ", "-");

    double a = 0, b = 0;
    if (! readNumbers (s, a, b))
        return {};
    if (near && a == b)
        return Filter::near (a, halfDouble);
    return Filter::between (a, b, halfDouble);
}

Filter takeFromSearch (juce::String& text, bool halfDouble)
{
    juce::StringArray tokens;
    tokens.addTokens (text, " \t", "\"");
    tokens.removeEmptyStrings();

    for (int i = 0; i < tokens.size(); ++i)
    {
        const auto t = tokens[i].toLowerCase();
        int used = 0;
        juce::String candidate;
        if (t.endsWith ("bpm") && t.length() > 3)
        {
            candidate = t;
            used = 1;
        }
        else if (i + 1 < tokens.size() && tokens[i + 1].equalsIgnoreCase ("bpm"))
        {
            candidate = t;
            used = 2;
            // "90 - 100 bpm" / "90 to 100 bpm"
            if (i >= 2 && (tokens[i - 1] == "-" || tokens[i - 1].equalsIgnoreCase ("to")))
            {
                candidate = tokens[i - 2] + "-" + t;
                i -= 2;
                used = 4;
            }
        }
        if (used == 0)
            continue;

        const auto f = parse (candidate, halfDouble);
        if (! f.isActive())
            continue;
        tokens.removeRange (i, used);
        text = tokens.joinIntoString (" ");
        return f;
    }
    return {};
}

double guessFromLength (double seconds)
{
    if (seconds < 1.4 || seconds > 100.0)
        return 0;
    for (int bars : { 1, 2, 4, 8, 16, 32 })
    {
        const double bpm = 240.0 * bars / seconds;
        if (bpm >= 80.0 && bpm < 160.0)
        {
            // Loops are made at whole-number tempos: a length that doesn't land on one is a
            // loop with a tail, or one not cut to bars.
            if (std::abs (bpm - std::round (bpm)) > 0.06)
                return 0;
            return std::round (bpm);
        }
    }
    return 0;
}
}
