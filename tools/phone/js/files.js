// WAV files (what the shared library holds) and zip archives (how a kit leaves the phone).

import { RATE } from "./dsp.js";

// A 16-bit WAV at 44.1 kHz, with the ACID chunk the desktop tools read for tempo when `bpm`
// and `beats` are given.
export function encodeWav (sound, { bpm = 0, beats = 0 } = {})
{
    const channels = sound.channels.length;
    const frames = sound.channels[0].length;
    const dataBytes = frames * channels * 2;
    const acid = bpm > 0 && beats > 0;
    const size = 44 + dataBytes + (acid ? 32 : 0);
    const view = new DataView (new ArrayBuffer (size));
    let pos = 0;
    const tag = (t) => { for (const c of t) view.setUint8 (pos++, c.charCodeAt (0)); };
    const u16 = (v) => { view.setUint16 (pos, v, true); pos += 2; };
    const u32 = (v) => { view.setUint32 (pos, v, true); pos += 4; };

    tag ("RIFF"); u32 (size - 8); tag ("WAVE");
    tag ("fmt "); u32 (16); u16 (1); u16 (channels); u32 (RATE); u32 (RATE * channels * 2); u16 (channels * 2); u16 (16);

    if (acid)
    {
        // flags 0x01 = one-shot off (a loop); root note 60; beats; 4/4; tempo
        tag ("acid"); u32 (24); u32 (0); u16 (60); u16 (0x8000); view.setFloat32 (pos, 0, true); pos += 4;
        u32 (Math.round (beats)); u16 (4); u16 (4); view.setFloat32 (pos, bpm, true); pos += 4;
    }

    tag ("data"); u32 (dataBytes);
    for (let i = 0; i < frames; ++i)
        for (let ch = 0; ch < channels; ++ch)
        {
            view.setInt16 (pos, Math.round (Math.max (-1, Math.min (1, sound.channels[ch][i])) * 32767), true);
            pos += 2;
        }

    return new Uint8Array (view.buffer);
}

//==============================================================================
// A zip with stored (uncompressed) entries: audio doesn't compress much, and it keeps this small.

const crcTable = (() =>
{
    const t = new Uint32Array (256);
    for (let n = 0; n < 256; ++n)
    {
        let c = n;
        for (let k = 0; k < 8; ++k)
            c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        t[n] = c >>> 0;
    }
    return t;
})();

export function crc32 (bytes)
{
    let c = 0xffffffff;
    for (let i = 0; i < bytes.length; ++i)
        c = crcTable[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

// files: [[path, Uint8Array], ...]
export function zip (files)
{
    const encoder = new TextEncoder();
    // MS-DOS date and time, so files show when they were exported.
    const now = new Date();
    const dosTime = (now.getHours() << 11) | (now.getMinutes() << 5) | (now.getSeconds() >> 1);
    const dosDate = ((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate();
    const parts = [];
    const central = [];
    let offset = 0;

    const header = (size) => { const v = new DataView (new ArrayBuffer (size)); return v; };

    for (const [path, data] of files)
    {
        const name = encoder.encode (path);
        const crc = crc32 (data);

        const local = header (30);
        local.setUint32 (0, 0x04034b50, true);
        local.setUint16 (4, 20, true);
        local.setUint16 (10, dosTime, true);
        local.setUint16 (12, dosDate, true);
        local.setUint32 (14, crc, true);
        local.setUint32 (18, data.length, true);
        local.setUint32 (22, data.length, true);
        local.setUint16 (26, name.length, true);
        parts.push (new Uint8Array (local.buffer), name, data);

        const entry = header (46);
        entry.setUint32 (0, 0x02014b50, true);
        entry.setUint16 (4, 20, true);
        entry.setUint16 (6, 20, true);
        entry.setUint16 (12, dosTime, true);
        entry.setUint16 (14, dosDate, true);
        entry.setUint32 (16, crc, true);
        entry.setUint32 (20, data.length, true);
        entry.setUint32 (24, data.length, true);
        entry.setUint16 (28, name.length, true);
        entry.setUint32 (42, offset, true);
        central.push (new Uint8Array (entry.buffer), name);

        offset += 30 + name.length + data.length;
    }

    const centralSize = central.reduce ((n, p) => n + p.length, 0);
    const end = header (22);
    end.setUint32 (0, 0x06054b50, true);
    end.setUint16 (8, files.length, true);
    end.setUint16 (10, files.length, true);
    end.setUint32 (12, centralSize, true);
    end.setUint32 (16, offset, true);

    const all = [...parts, ...central, new Uint8Array (end.buffer)];
    const out = new Uint8Array (all.reduce ((n, p) => n + p.length, 0));
    let pos = 0;
    for (const p of all) { out.set (p, pos); pos += p.length; }
    return out;
}
