// Reading and writing the MPC2000XL's .SND sounds and .PGM programs, the same layouts as
// tools/mpc/mpc/MpcFiles.cpp (the tests check both against files saved by a real MPC).
// Multi-byte values are little endian.

export const NAME_LENGTH = 16;
export const FILE_NAME_LENGTH = 8;
export const PAD_COUNT = 64;
export const FIRST_NOTE = 35;
export const NO_SOUND = 0xff;

export const VoiceOverlap = { poly: 0, mono: 1, noteOff: 2 };

const ALLOWED_SPECIALS = "_^$~!#%&-{}()@`";

// Uppercases and replaces anything the MPC can't show with '_'. Spaces and dots are dropped.
export function toMpcName (name, maxLength = FILE_NAME_LENGTH)
{
    let result = "";
    for (const c of String (name))
    {
        if (result.length >= maxLength)
            break;
        const u = c.toUpperCase();
        if (u === " " || u === ".")
            continue;
        result += (u.length === 1 && (/[A-Z0-9]/.test (u) || ALLOWED_SPECIALS.includes (u))) ? u : "_";
    }
    return result || "SOUND";
}

// Makes `name` unique against `taken` (case-insensitive), e.g. "KICK" -> "KICK2",
// "BREAKBEA" -> "BREAKB_2". Adds the result to `taken`.
export function makeUniqueName (name, taken, maxLength = FILE_NAME_LENGTH)
{
    const base = toMpcName (name, maxLength);
    const isTaken = (c) => taken.some ((t) => t.toUpperCase() === c.toUpperCase());
    let candidate = base;

    for (let n = 2; isTaken (candidate); ++n)
    {
        const suffix = String (n);
        const keep = Math.min (base.length, maxLength - suffix.length);
        candidate = keep < base.length && keep > 1 ? base.slice (0, keep - 1) + "_" + suffix
                                                   : base.slice (0, keep) + suffix;
    }

    taken.push (candidate);
    return candidate;
}

class Writer
{
    constructor (size = 1024) { this.buf = new Uint8Array (size); this.pos = 0; }

    ensure (n)
    {
        if (this.pos + n <= this.buf.length)
            return;
        const next = new Uint8Array (Math.max (this.buf.length * 2, this.pos + n));
        next.set (this.buf);
        this.buf = next;
    }

    u8 (v) { this.ensure (1); this.buf[this.pos++] = v & 0xff; }
    u16 (v) { this.u8 (v); this.u8 (v >> 8); }
    u32 (v) { this.u16 (v & 0xffff); this.u16 ((v >>> 16) & 0xffff); }
    bytes (a) { this.ensure (a.length); this.buf.set (a, this.pos); this.pos += a.length; }

    // A 16-character name padded with spaces, then a terminating zero.
    name (n)
    {
        for (let i = 0; i < NAME_LENGTH; ++i)
            this.u8 (i < n.length ? n.charCodeAt (i) : 32);
        this.u8 (0);
    }

    result() { return this.buf.slice (0, this.pos); }
}

class Reader
{
    constructor (bytes) { this.in = bytes; this.pos = 0; this.failed = false; }

    u8()
    {
        if (this.pos >= this.in.length) { this.failed = true; return 0; }
        return this.in[this.pos++];
    }

    s8() { const v = this.u8(); return v > 127 ? v - 256 : v; }
    u16() { const lo = this.u8(); return lo | (this.u8() << 8); }
    s16() { const v = this.u16(); return v > 32767 ? v - 65536 : v; }
    u32() { const lo = this.u16(); return (lo + this.u16() * 65536) >>> 0; }
    bytes (n) { const a = new Uint8Array (n); for (let i = 0; i < n; ++i) a[i] = this.u8(); return a; }

    name()
    {
        let n = "";
        for (let i = 0; i < NAME_LENGTH + 1; ++i)
            n += String.fromCharCode (this.u8());
        const zero = n.indexOf ("\0");
        return (zero >= 0 ? n.slice (0, zero) : n).replace (/ +$/, "");
    }
}

//==============================================================================
// .SND: 42-byte header, then 16-bit samples, left channel then right (not interleaved).

export function newSound (name, left, right = null)
{
    return { name, left, right, level: 100, tune: 0, start: 0, end: left.length, loopTo: 0,
             loopEnabled: false, beatCount: 4, sampleRate: 44100 };
}

export function writeSnd (s)
{
    const frames = s.left.length;
    const stereo = s.right != null && s.right.length > 0;
    const end = Math.min (s.end === 0 ? frames : s.end, frames);
    const loopTo = Math.min (s.loopTo, end);
    const w = new Writer (42 + frames * (stereo ? 4 : 2));

    w.u8 (0x01);
    w.u8 (0x04);
    w.name (s.name);
    w.u8 (s.level);
    w.u8 (s.tune);
    w.u8 (stereo ? 1 : 0);
    w.u32 (Math.min (s.start, end));
    w.u32 (end);
    w.u32 (frames);
    w.u32 (end - loopTo);   // stored as the loop length, counted back from the end
    w.u8 (s.loopEnabled ? 1 : 0);
    w.u8 (s.beatCount);
    w.u16 (s.sampleRate);

    for (let i = 0; i < frames; ++i)
        w.u16 (s.left[i]);
    if (stereo)
        for (let i = 0; i < frames; ++i)
            w.u16 (i < s.right.length ? s.right[i] : 0);

    return w.result();
}

export function readSnd (bytes)
{
    if (bytes.length < 42)
        throw new Error ("File is too short to be a sound");
    if (bytes[0] !== 0x01 || bytes[1] !== 0x04)
        throw new Error ("Not an MPC2000XL sound");

    const r = new Reader (bytes);
    r.pos = 2;
    const s = { name: r.name(), level: r.u8(), tune: r.s8() };
    const stereo = r.u8() !== 0;
    s.start = r.u32();
    s.end = r.u32();
    const frames = r.u32();
    const loopLength = r.u32();
    s.loopEnabled = r.u8() !== 0;
    s.beatCount = r.u8();
    s.sampleRate = r.u16();
    s.loopTo = s.end >= loopLength ? s.end - loopLength : 0;

    if (bytes.length - r.pos < frames * (stereo ? 4 : 2))
        throw new Error ("Sound data is cut short");

    s.left = new Int16Array (frames);
    for (let i = 0; i < frames; ++i)
        s.left[i] = r.s16();
    s.right = null;
    if (stereo)
    {
        s.right = new Int16Array (frames);
        for (let i = 0; i < frames; ++i)
            s.right[i] = r.s16();
    }
    return s;
}

//==============================================================================
// .PGM: sound names, then settings for the 64 notes the pads play, a mixer strip per note,
// the pad-to-note map and the effects settings.

// The MPC2000XL's factory pad layout (A01 plays note 37, A02 note 36, ...).
export const DEFAULT_PAD_TO_NOTE = [
    37, 36, 42, 82, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53,
    54, 69, 81, 80, 65, 66, 76, 77, 56, 62, 63, 64, 73, 74, 71, 39,
    52, 57, 58, 59, 60, 61, 67, 68, 70, 72, 75, 78, 79, 35, 41, 50,
    83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98];

// The effects board settings of a freshly initialised MPC2000XL.
const DEFAULT_EFFECTS = [
    0x02, 0x00, 0x48, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x63, 0x01, 0x14, 0x08, 0x1d, 0xfc, 0x32, 0x33,
    0x02, 0x32, 0x3c, 0x08, 0x05, 0x0a, 0x14, 0x14, 0x32, 0x00, 0x00, 0x02, 0x0f, 0x19, 0x00, 0x05,
    0x41, 0x14, 0x1e, 0x01, 0x05, 0x00, 0x00, 0x05, 0x63, 0x00, 0xf4, 0xff, 0x0c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x4f, 0x01, 0x4f, 0x01, 0x00, 0x42, 0x4f, 0x01, 0x00, 0x42,
    0x4f, 0x01, 0x00, 0x42, 0x32, 0x00, 0x63, 0x28, 0x00, 0x3c, 0x00, 0x00, 0xd0, 0x07, 0x00, 0x00,
    0x63, 0x01, 0x14, 0x08, 0x1d, 0xfc, 0x32, 0x33, 0x02, 0x32, 0x3c, 0x08, 0x05, 0x0a, 0x14, 0x14,
    0x32, 0x00, 0x00, 0x02, 0x0f, 0x19, 0x00, 0x05, 0x41, 0x14, 0x1e, 0x01, 0x05, 0x00, 0x00, 0x05,
    0x63, 0x00, 0xf4, 0xff, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x4f, 0x01,
    0x4f, 0x01, 0x00, 0x42, 0x4f, 0x01, 0x00, 0x42, 0x4f, 0x01, 0x00, 0x42, 0x32, 0x00, 0x63, 0x28,
    0x00, 0x3c, 0x00, 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x32, 0x00, 0x23, 0x00, 0x3e, 0x33,
    0x5a, 0x32, 0x14, 0x00, 0x00, 0x00, 0x32, 0x00, 0x23, 0x00, 0x3e, 0x33, 0x5a, 0x32, 0x14, 0x00,
    0x00, 0x00, 0x32, 0x00, 0x23, 0x00, 0x3e, 0x33, 0x5a, 0x32, 0x14, 0x00, 0x00, 0x00, 0x32, 0x00,
    0x23, 0x00, 0x3e, 0x33, 0x5a, 0x32, 0x14, 0x00];

const NOTE_FIELDS = [
    "soundIndex", "soundGenerationMode", "velocityRangeLower", "alsoPlayNote1", "velocityRangeUpper",
    "alsoPlayNote2", "voiceOverlap", "muteNote1", "muteNote2", "tune", "attack", "decay", "decayMode",
    "cutoff", "resonance", "filterEnvAttack", "filterEnvDecay", "filterEnvAmount", "velocityToLevel",
    "velocityToAttack", "velocityToStart", "velocityToCutoff", "sliderParameter", "velocityToPitch"];
const MIXER_FIELDS = ["fxOutput", "volume", "pan", "volumeIndividual", "output", "fxSendLevel"];

function newNote()
{
    return { soundIndex: NO_SOUND, soundGenerationMode: 0, velocityRangeLower: 44, alsoPlayNote1: 0,
             velocityRangeUpper: 88, alsoPlayNote2: 0, voiceOverlap: VoiceOverlap.poly, muteNote1: 0,
             muteNote2: 0, tune: 0, attack: 0, decay: 5, decayMode: 0, cutoff: 100, resonance: 0,
             filterEnvAttack: 0, filterEnvDecay: 0, filterEnvAmount: 0, velocityToLevel: 100,
             velocityToAttack: 0, velocityToStart: 0, velocityToCutoff: 0, sliderParameter: 0,
             velocityToPitch: 0 };
}

export function newProgram (name = "")
{
    return {
        name,
        soundNames: [],
        notes: Array.from ({ length: PAD_COUNT }, newNote),   // notes[i] is note FIRST_NOTE + i
        mixers: Array.from ({ length: PAD_COUNT }, () => ({ fxOutput: 0, volume: 100, pan: 50, volumeIndividual: 100, output: 0, fxSendLevel: 0 })),
        padToNote: DEFAULT_PAD_TO_NOTE.slice(),
        programChange: 0,
        // Fields we don't edit, kept as read so a loaded program saves back unchanged.
        header: [30, 0],
        slider: [0, 136, 120, 12, 45, 0, 20, 206, 50],
        afterProgramChange: [35, 64, 0, 25, 0],
        afterNotes: 6,
        afterMixers: [0, 64, 0],
        effects: DEFAULT_EFFECTS.slice(),
    };
}

export function noteIndexForPad (program, pad) { return program.padToNote[pad] - FIRST_NOTE; }

export function writePgm (p)
{
    const w = new Writer (2290 + p.soundNames.length * 17);
    w.u8 (0x07);
    w.u8 (0x04);
    w.u16 (p.soundNames.length);
    for (const n of p.soundNames)
        w.name (n);

    w.bytes (p.header);
    w.name (p.name);
    w.bytes (p.slider);
    w.u8 (p.programChange);
    w.bytes (p.afterProgramChange);

    for (const n of p.notes)
        for (const f of NOTE_FIELDS)
            f === "tune" ? w.u16 (n.tune) : w.u8 (n[f]);

    w.u8 (p.afterNotes);
    for (const m of p.mixers)
        for (const f of MIXER_FIELDS)
            w.u8 (m[f]);

    w.bytes (p.afterMixers);
    w.bytes (p.padToNote);
    w.bytes (p.effects);
    return w.result();
}

export function readPgm (bytes)
{
    if (bytes.length < 4 || bytes[0] !== 0x07 || bytes[1] !== 0x04)
        throw new Error ("Not an MPC2000XL program");

    const r = new Reader (bytes);
    r.pos = 2;
    const count = r.u16();
    if (bytes.length !== 2290 + count * 17)
        throw new Error ("Program file has an unexpected size");

    const p = newProgram();
    for (let i = 0; i < count; ++i)
        p.soundNames.push (r.name());

    p.header = Array.from (r.bytes (2));
    p.name = r.name();
    p.slider = Array.from (r.bytes (9));
    p.programChange = r.u8();
    p.afterProgramChange = Array.from (r.bytes (5));

    for (const n of p.notes)
        for (const f of NOTE_FIELDS)
            n[f] = f === "tune" ? r.s16() : r.u8();

    p.afterNotes = r.u8();
    for (const m of p.mixers)
        for (const f of MIXER_FIELDS)
            m[f] = r.u8();

    p.afterMixers = Array.from (r.bytes (3));
    p.padToNote = Array.from (r.bytes (PAD_COUNT));
    p.effects = Array.from (r.bytes (200));

    if (p.padToNote.some ((n) => n < FIRST_NOTE || n >= FIRST_NOTE + PAD_COUNT))
        throw new Error ("Program has a pad mapped to an invalid note");
    if (r.failed)
        throw new Error ("Program file is cut short");
    return p;
}

//==============================================================================
// A kit as files for the card: one folder with a .PGM and the .SND files it plays.
// pads: [{ pad: 0..63, sound (from newSound), volume, pan, tune, voiceOverlap }]

export function buildBank (name, pads, takenFolderNames = [])
{
    const folderName = makeUniqueName (name, takenFolderNames);
    const program = newProgram (String (name).slice (0, NAME_LENGTH));
    const sounds = [];
    const taken = [];

    for (const pad of pads)
    {
        if (pad.pad < 0 || pad.pad >= PAD_COUNT || pad.sound.left.length === 0)
            continue;
        if (sounds.length >= 255)
            break;   // a program can reference at most 255 sounds (0xFF means none)

        // The MPC finds a program's sounds by file name, so the name inside the .SND must be
        // the file name: 8 characters.
        const sound = { ...pad.sound, name: makeUniqueName (pad.sound.name, taken) };
        const index = noteIndexForPad (program, pad.pad);
        const note = program.notes[index];
        note.soundIndex = sounds.length;
        note.tune = pad.tune ?? 0;
        note.voiceOverlap = pad.voiceOverlap ?? VoiceOverlap.poly;
        program.mixers[index].volume = Math.min (pad.volume ?? 100, 100);
        program.mixers[index].pan = Math.min (pad.pan ?? 50, 100);

        program.soundNames.push (sound.name);
        sounds.push (sound);
    }

    return { folderName, program, sounds };
}

// The files of a built bank, as [path, bytes] pairs, e.g. ["BREAKS/BREAKS.PGM", ...].
export function bankFiles (bank)
{
    const files = [[bank.folderName + "/" + bank.folderName + ".PGM", writePgm (bank.program)]];
    for (const s of bank.sounds)
        files.push ([bank.folderName + "/" + s.name + ".SND", writeSnd (s)]);
    return files;
}
