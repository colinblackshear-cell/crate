// The phone app's sound editing, chopping and file writing.
import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

import * as dsp from "../../tools/phone/js/dsp.js";
import { encodeWav, zip, crc32 } from "../../tools/phone/js/files.js";

const ramp = (n) => ({ channels: [Float32Array.from ({ length: n }, (_, i) => i / n)] });

// Clicks every `gap` samples on silence: easy hits to find.
function clicks (count, gap)
{
    const c = new Float32Array (count * gap);
    for (let k = 0; k < count; ++k)
        for (let i = 0; i < 400; ++i)
            c[k * gap + i] = 0.8 * Math.exp (-i / 80) * Math.sin (i * 0.3);
    return { channels: [c] };
}

test ("edits", () =>
{
    const s = ramp (1000);
    assert.equal (dsp.length (dsp.crop (s, { start: 100, end: 300 })), 200);
    assert.equal (dsp.crop (s, { start: 100, end: 300 }).channels[0][0], s.channels[0][100]);
    const removed = dsp.remove (s, { start: 100, end: 300 });
    assert.equal (dsp.length (removed), 800);
    assert.equal (removed.channels[0][100], s.channels[0][300]);
    assert.equal (dsp.reverse (s).channels[0][0], s.channels[0][999]);
    assert.ok (Math.abs (dsp.peak (dsp.normalize (dsp.gain (s, null, 0.1))) - 0.98) < 1e-6);
    assert.equal (dsp.fade (s, null, true).channels[0][0], 0);
    assert.equal (s.channels[0][500], 0.5, "edits never change the original");
});

test ("trim silence keeps the sound with a little room", () =>
{
    const c = new Float32Array (44100);
    c.fill (0.5, 20000, 21000);
    const t = dsp.trimSilence ({ channels: [c] });
    assert.ok (dsp.length (t) > 1000 && dsp.length (t) < 4000);
});

test ("resampling 48 kHz to 44.1 kHz keeps the length in seconds", () =>
{
    const s = dsp.resample ([new Float32Array (48000).fill (0.25)], 48000);
    assert.ok (Math.abs (dsp.length (s) - 44100) <= 1);
    assert.ok (Math.abs (s.channels[0][1000] - 0.25) < 1e-6);
});

test ("chopping", () =>
{
    const s = clicks (8, 11025);
    assert.equal (dsp.findHits (s, 0.5).length, 7, "8 clicks, 7 markers after the first");
    assert.equal (dsp.chop (s, 0).length, 8);
    const even = dsp.chop (s, 4);
    assert.equal (even.length, 4);
    assert.equal (even.reduce ((n, p) => n + dsp.length (p), 0), dsp.length (s));
});

test ("a WAV file has the right header and data", () =>
{
    const wav = encodeWav ({ channels: [new Float32Array ([0, 0.5, -1])] }, { bpm: 90, beats: 4 });
    const v = new DataView (wav.buffer);
    const text = (at) => String.fromCharCode (...wav.slice (at, at + 4));
    assert.equal (text (0), "RIFF");
    assert.equal (v.getUint32 (4, true), wav.length - 8);
    assert.equal (v.getUint32 (24, true), 44100);
    assert.equal (text (36), "acid");
    assert.equal (v.getFloat32 (36 + 8 + 20, true), 90);
    assert.equal (text (68), "data");
    assert.equal (v.getInt16 (76 + 2, true), Math.round (0.5 * 32767));
    assert.equal (v.getInt16 (76 + 4, true), -32767);
});

test ("zip files open with unzip", { skip: ! fs.existsSync ("/usr/bin/unzip") }, () =>
{
    assert.equal (crc32 (new TextEncoder().encode ("123456789")), 0xcbf43926);
    const dir = fs.mkdtempSync (path.join (os.tmpdir(), "crate-zip-"));
    const data = new Uint8Array (5000).map ((_, i) => i * 7);
    fs.writeFileSync (path.join (dir, "kit.zip"), zip ([["KIT/KIT.PGM", new Uint8Array ([7, 4])], ["KIT/A.SND", data]]));
    execFileSync ("unzip", ["-q", "kit.zip"], { cwd: dir });
    assert.deepEqual (new Uint8Array (fs.readFileSync (path.join (dir, "KIT/A.SND"))), data);
    fs.rmSync (dir, { recursive: true });
});
