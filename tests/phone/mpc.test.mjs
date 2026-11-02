// The phone app's MPC file code. Set MPC_FIXTURES to a folder of files saved by an MPC2000XL
// (CI uses the vMPC2000XL demo kits) to also check that every one reads and writes back byte
// for byte, like the C++ format tests.
import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";

import * as mpc from "../../tools/phone/js/mpc.js";

test ("names are card-safe and unique", () =>
{
    assert.equal (mpc.toMpcName ("Kick Drum"), "KICKDRUM");
    assert.equal (mpc.toMpcName ("a.b c"), "ABC");
    assert.equal (mpc.toMpcName ("snäre*"), "SN_RE_");
    assert.equal (mpc.toMpcName (""), "SOUND");
    const taken = [];
    assert.equal (mpc.makeUniqueName ("kick", taken), "KICK");
    assert.equal (mpc.makeUniqueName ("KICK", taken), "KICK2");
    assert.equal (mpc.makeUniqueName ("breakbeat", taken), "BREAKBEA");
    assert.equal (mpc.makeUniqueName ("breakbeat", taken), "BREAKB_2");
});

test ("a sound writes and reads back", () =>
{
    const left = Int16Array.from ({ length: 1000 }, (_, i) => (i * 37) % 65536 - 32768);
    const right = left.map ((v) => -v - 1);
    const s = mpc.newSound ("STEREO", left, right);
    s.tune = -12;
    s.beatCount = 2;
    const bytes = mpc.writeSnd (s);
    assert.equal (bytes.length, 42 + 4000);
    const back = mpc.readSnd (bytes);
    assert.equal (back.name, "STEREO");
    assert.equal (back.tune, -12);
    assert.equal (back.beatCount, 2);
    assert.deepEqual (back.left, left);
    assert.deepEqual (back.right, right);
    assert.deepEqual (mpc.writeSnd (back), bytes);
});

test ("a kit builds into a program and sounds", () =>
{
    const sound = (name, n) => mpc.newSound (name, new Int16Array (n).fill (100));
    const bank = mpc.buildBank ("Test Kit", [
        { pad: 0, sound: sound ("Kick Drum", 100), tune: -25 },
        { pad: 1, sound: sound ("Kick Drum", 50), voiceOverlap: mpc.VoiceOverlap.mono, volume: 80, pan: 20 },
        { pad: 17, sound: sound ("snare", 10) },
    ]);
    assert.equal (bank.folderName, "TESTKIT");
    assert.deepEqual (bank.program.soundNames, ["KICKDRUM", "KICKDR_2", "SNARE"]);

    const files = mpc.bankFiles (bank);
    assert.deepEqual (files.map (([p]) => p), ["TESTKIT/TESTKIT.PGM", "TESTKIT/KICKDRUM.SND", "TESTKIT/KICKDR_2.SND", "TESTKIT/SNARE.SND"]);
    assert.equal (files[0][1].length, 2290 + 3 * 17);

    const p = mpc.readPgm (files[0][1]);
    const a02 = mpc.noteIndexForPad (p, 1);
    assert.equal (p.name, "Test Kit");
    assert.equal (p.notes[mpc.noteIndexForPad (p, 0)].tune, -25);
    assert.equal (p.notes[a02].soundIndex, 1);
    assert.equal (p.notes[a02].voiceOverlap, mpc.VoiceOverlap.mono);
    assert.equal (p.mixers[a02].volume, 80);
    assert.equal (p.mixers[a02].pan, 20);
    assert.equal (p.notes[mpc.noteIndexForPad (p, 17)].soundIndex, 2);
    assert.equal (p.notes[mpc.noteIndexForPad (p, 5)].soundIndex, mpc.NO_SOUND);
});

test ("files saved by an MPC2000XL read and write back byte for byte", { skip: ! process.env.MPC_FIXTURES }, () =>
{
    let sounds = 0, programs = 0;
    for (const file of fs.readdirSync (process.env.MPC_FIXTURES, { recursive: true }))
    {
        const full = path.join (process.env.MPC_FIXTURES, file);
        const ext = path.extname (file).toUpperCase();
        if (ext !== ".SND" && ext !== ".PGM")
            continue;
        const bytes = new Uint8Array (fs.readFileSync (full));
        const back = ext === ".SND" ? mpc.writeSnd (mpc.readSnd (bytes)) : mpc.writePgm (mpc.readPgm (bytes));
        assert.deepEqual (back, bytes, file);
        ext === ".SND" ? ++sounds : ++programs;
    }
    assert.ok (sounds > 0 && programs > 0);
});
