// Kaleidobox Trigger: up to 8 piezo drum triggers -> USB MIDI notes with velocity.
//
// Board: an Arduino Pro Micro (ATmega32U4, 5 V / 16 MHz) or an Arduino Leonardo. In the
// Arduino IDE pick "Arduino Leonardo" as the board (most Pro Micro clones are sold that way),
// install the "MIDIUSB" library (Tools > Manage Libraries, search MIDIUSB), and upload.
// Plug it into the Mac: no driver. It shows up as a MIDI controller; in Kaleidobox open
// Settings > Audio & MIDI > MIDI controllers, set it to "Drum triggers" (it guesses that from
// the name), press "Hit to learn" and hit each drum in turn.
//
// Each input: a piezo (a stick-on drum trigger, a pad, or a bare disc) on a 1/4" jack, with a
// 1 MOhm resistor and a 5.1 V zener across it and a 1 kOhm resistor into the pin (see the guide).
//
// What it does, per input, about 4000 times a second:
//   1. Waits for the piezo to go over its threshold (a hit starts).
//   2. Keeps the highest reading for a few milliseconds (the hit's peak = how hard).
//   3. Sends a note on with a velocity from that peak, then a note off a moment later.
//   4. Ignores the drum's own ringing after a hit (retrigger mask), but still lets a fast roll
//      through when a new hit jumps clearly above the ringing.
//   5. Drops a hit that is just another drum's vibration (crosstalk), when a louder drum hit at
//      the same moment.
//
// Tuning: set TUNING to 1, upload, open Tools > Serial Monitor (115200). Every hit prints its
// input, peak and velocity, so you can set each threshold just above what the other drums make it
// read, and its maxPeak to what your hardest hit reads.

#include <MIDIUSB.h>

#define TUNING 0

// ---------------------------------------------------------------- settings you might change
const uint8_t kInputs = 8;

// Pins, in order: input 1 to 8. On a Pro Micro: A0 A1 A2 A3 then the pins labelled 4 6 8 9.
const uint8_t kPins[kInputs] = { A0, A1, A2, A3, A6, A7, A8, A9 };

// The note each input sends. 36, 37, 38... play Kaleidobox's pads 1, 2, 3... before you learn
// anything. (For a General MIDI drum kit elsewhere, try 36 kick, 38 snare, 42 hat, 46 open hat,
// 45 low tom, 48 high tom, 49 crash, 51 ride.)
const uint8_t kNotes[kInputs] = { 36, 37, 38, 39, 40, 41, 42, 43 };

const uint8_t kChannel = 10;   // MIDI channel 1-16 (10 is drums)

// Readings are 0-1023. A hit starts above `threshold`; `maxPeak` and above is velocity 127.
// Raise a threshold if that input fires by itself or from other drums; lower it if soft hits
// are missed.
uint16_t threshold[kInputs] = { 40, 40, 40, 40, 40, 40, 40, 40 };
uint16_t maxPeak[kInputs]   = { 700, 700, 700, 700, 700, 700, 700, 700 };

// Velocity curve: 1.0 is straight; below 1 (0.6-0.8) brings soft hits up, which feels right
// for most piezos. Kaleidobox's Feel menu bends it further without uploading again.
const float kCurve = 0.7f;

const uint16_t kScanUs = 2500;        // how long a hit's peak is looked for (2-4 ms)
const uint16_t kMinGapMs = 10;        // nothing at all from this input for this long after a hit
const uint16_t kMaskMs = 40;          // after that, a new hit has to jump above the ringing until it dies down
const uint16_t kRollJump = 150;       // ...to this % of the ringing (lower it if fast rolls lose notes)
const uint8_t kCrosstalkPercent = 50; // a hit under this % of a louder drum's hit at the same time is dropped
const uint16_t kCrosstalkMs = 15;     // "the same time"
const uint16_t kNoteLengthMs = 30;    // note off this long after the note on

// ---------------------------------------------------------------- how it works
enum State : uint8_t { idle, scanning, masked };

struct Input
{
    State state = idle;
    uint16_t peak = 0;        // while scanning
    uint16_t lastPeak = 0;    // the last hit sent
    uint16_t ring = 0;        // the ringing after it (see masked)
    uint32_t ringMs = 0;
    uint32_t scanStart = 0;   // micros
    uint32_t lastHit = 0;     // millis of the last hit sent
    uint32_t noteOffAt = 0;   // millis; 0 = no note sounding
};
Input inputs[kInputs];

uint16_t readPin (uint8_t pin)
{
    // The ADC has one sampler shared by every pin: after switching pins, the first reading still
    // carries some of the last pin's charge (piezos are weak), so it's read twice.
    analogRead (pin);
    return analogRead (pin);
}

uint8_t velocityFor (uint8_t i, uint16_t peak)
{
    const float span = (float) maxPeak[i] - (float) threshold[i];
    float x = ((float) peak - (float) threshold[i]) / (span > 1.0f ? span : 1.0f);
    x = constrain (x, 0.0f, 1.0f);
    const int v = 1 + (int) (126.0f * pow (x, kCurve) + 0.5f);
    return (uint8_t) constrain (v, 1, 127);
}

void sendNote (bool on, uint8_t note, uint8_t velocity)
{
    const uint8_t ch = (kChannel - 1) & 0x0F;
    midiEventPacket_t p = on ? midiEventPacket_t { 0x09, (uint8_t) (0x90 | ch), note, velocity }
                             : midiEventPacket_t { 0x08, (uint8_t) (0x80 | ch), note, 0 };
    MidiUSB.sendMIDI (p);
    MidiUSB.flush();
}

// True if hit `i` (with `peak`) looks like the vibration of a louder drum hit just now.
bool isCrosstalk (uint8_t i, uint16_t peak, uint32_t now)
{
    for (uint8_t j = 0; j < kInputs; ++j)
    {
        if (j == i)
            continue;
        const Input& o = inputs[j];
        uint16_t loud = 0;
        if (o.state == scanning)
            loud = o.peak;                                   // hitting at this very moment
        else if (o.lastHit != 0 && now - o.lastHit < kCrosstalkMs)
            loud = o.lastPeak;                               // hit a moment ago
        else if (o.state == masked)
            loud = o.ring;                                   // still ringing
        if (loud > 0 && (uint32_t) peak * 100 < (uint32_t) loud * kCrosstalkPercent)
            return true;
    }
    return false;
}

void setup()
{
    // Faster ADC (prescaler 16: about 16 microseconds a reading instead of 110). Still plenty
    // accurate for drums.
    ADCSRA = (ADCSRA & 0xF8) | 0x04;
#if TUNING
    Serial.begin (115200);
#endif
}

void loop()
{
    const uint32_t nowMs = millis();
    for (uint8_t i = 0; i < kInputs; ++i)
    {
        Input& in = inputs[i];
        const uint16_t v = readPin (kPins[i]);
        const uint32_t nowUs = micros();

        if (in.noteOffAt != 0 && (int32_t) (nowMs - in.noteOffAt) >= 0)
        {
            sendNote (false, kNotes[i], 0);
            in.noteOffAt = 0;
        }

        switch (in.state)
        {
            case masked:
            {
                // Follow the drum's ringing: it holds its loudest recent reading and fades by
                // 1/16 every millisecond. A new hit is one that jumps well above it (a fast roll),
                // never the ringing itself, however slowly a big drum dies away.
                const uint32_t since = nowMs - in.lastHit;
                for (uint32_t ms = nowMs - in.ringMs; ms > 0 && in.ring > 0; --ms)
                {
                    in.ring -= in.ring / 16 + 1;
                    if (ms > 64) { in.ring = 0; break; }
                }
                in.ringMs = nowMs;
                if (since >= kMinGapMs && v > threshold[i] && (uint32_t) v > (uint32_t) in.ring * kRollJump / 100)
                {
                    in.state = scanning;
                    in.peak = v;
                    in.scanStart = nowUs;
                }
                else
                {
                    if (v > in.ring)
                        in.ring = v;
                    if (since >= kMaskMs && in.ring <= threshold[i] / 2)
                        in.state = idle;   // the ringing has died down: back to listening
                }
                break;
            }
            case idle:
                if (v > threshold[i])
                {
                    in.state = scanning;
                    in.peak = v;
                    in.scanStart = nowUs;
                }
                break;
            case scanning:
                if (v > in.peak)
                    in.peak = v;
                if (nowUs - in.scanStart >= kScanUs)
                {
                    const bool ghost = isCrosstalk (i, in.peak, nowMs);
                    const uint8_t vel = velocityFor (i, in.peak);
#if TUNING
                    Serial.print ("input ");
                    Serial.print (i + 1);
                    Serial.print ("  peak ");
                    Serial.print (in.peak);
                    Serial.print ("  velocity ");
                    Serial.print (vel);
                    Serial.println (ghost ? "  (dropped: crosstalk)" : "");
#endif
                    if (! ghost)
                    {
                        if (in.noteOffAt != 0)
                            sendNote (false, kNotes[i], 0);   // a fast roll: end the last one first
                        sendNote (true, kNotes[i], vel);
                        in.noteOffAt = nowMs + kNoteLengthMs;
                        if (in.noteOffAt == 0)
                            in.noteOffAt = 1;
                    }
                    in.lastPeak = in.peak;
                    in.ring = in.peak;
                    in.ringMs = nowMs;
                    in.lastHit = nowMs == 0 ? 1 : nowMs;
                    in.state = masked;
                }
                break;
        }
    }

    // Nothing comes in from the Mac, but reading keeps the USB MIDI port happy.
    MidiUSB.read();
}
