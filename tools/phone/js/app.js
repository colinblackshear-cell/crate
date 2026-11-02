// Crate on the phone: record sounds while you walk around, shape them, keep them in a library,
// play them on pads, and export kits the MPC2000XL loads.

import * as dsp from "./dsp.js";
import * as audio from "./audio.js";
import * as store from "./store.js";
import * as mpc from "./mpc.js";
import { encodeWav, zip } from "./files.js";

const $ = (id) => document.getElementById (id);
const PADS_PER_BANK = 16;
const MAX_RECORD_SECONDS = 600;

//==============================================================================
// Status line and tabs

let statusTimer = 0;
function status (text, warn = false)
{
    const el = $("status");
    el.textContent = text;
    el.classList.toggle ("warn", warn);
    clearTimeout (statusTimer);
    statusTimer = setTimeout (() => { el.textContent = ""; }, warn ? 8000 : 4000);
}

function showPage (name)
{
    for (const p of document.querySelectorAll (".page"))
        p.classList.toggle ("active", p.id === "page-" + name);
    for (const b of document.querySelectorAll (".tabs button"))
        b.classList.toggle ("on", b.dataset.page === name);
    audio.stop();
    if (name === "sounds") refreshLibrary();
    if (name === "pads") renderPads();
    if (name === "record") requestAnimationFrame (drawWave);
}

for (const b of document.querySelectorAll (".tabs button"))
    b.onclick = () => showPage (b.dataset.page);

// Sharing a file: the share sheet (Save to Files, AirDrop) where there is one, else a download.
async function shareFile (name, bytes, type)
{
    const file = new File ([bytes], name, { type });
    if (navigator.canShare?.({ files: [file] }))
    {
        try { await navigator.share ({ files: [file] }); return; }
        catch (e) { if (e.name === "AbortError") return; }
    }
    const url = URL.createObjectURL (file);
    const a = Object.assign (document.createElement ("a"), { href: url, download: name });
    document.body.append (a);
    a.click();
    a.remove();
    setTimeout (() => URL.revokeObjectURL (url), 10000);
}

//==============================================================================
// The sound being edited

const ed = {
    sound: null,            // before effects
    preview: null,          // with effects, when any are set
    previewFor: null,
    selection: null,        // { start, end } or null
    markers: [],
    undo: [],
    redo: [],
    fx: audio.defaultFx(),
};

const hasSound = () => ed.sound != null && dsp.length (ed.sound) > 0;

function openSound (sound, name, keepHistory = true)
{
    if (keepHistory && hasSound())
        pushUndo();
    ed.sound = sound;
    ed.selection = null;
    ed.markers = [];
    if (name != null)
        $("name").value = name;
    invalidatePreview();
    refreshEditor();
}

function pushUndo()
{
    ed.undo.push ({ sound: ed.sound, markers: ed.markers.slice() });
    if (ed.undo.length > 50)
        ed.undo.shift();
    ed.redo = [];
}

function edit (fn, label)
{
    if (! hasSound())
        return;
    pushUndo();
    ed.sound = fn (ed.sound, ed.selection);
    ed.selection = null;
    ed.markers = [];
    invalidatePreview();
    refreshEditor();
    status (label);
}

function stepHistory (from, to)
{
    if (! from.length)
        return;
    to.push ({ sound: ed.sound, markers: ed.markers.slice() });
    const s = from.pop();
    ed.sound = s.sound;
    ed.markers = s.markers;
    ed.selection = null;
    invalidatePreview();
    refreshEditor();
}

$("undo").onclick = () => stepHistory (ed.undo, ed.redo);
$("redo").onclick = () => stepHistory (ed.redo, ed.undo);
$("select-all").onclick = () => { if (hasSound()) { ed.selection = { start: 0, end: dsp.length (ed.sound) }; refreshEditor(); } };

const edits = {
    keep: [(s, r) => r ? dsp.crop (s, r) : s, "Kept the selection"],
    delete: [(s, r) => r ? dsp.remove (s, r) : s, "Deleted the selection"],
    trim: [(s) => dsp.trimSilence (s), "Trimmed the quiet ends"],
    fadein: [(s, r) => dsp.fade (s, r ?? { start: 0, end: Math.min (dsp.length (s), 4410) }, true), "Faded in"],
    fadeout: [(s, r) => dsp.fade (s, r ?? { start: Math.max (0, dsp.length (s) - 22050), end: dsp.length (s) }, false), "Faded out"],
    normalize: [(s, r) => dsp.normalize (s, r), "Normalized"],
    reverse: [(s, r) => dsp.reverse (s, r), "Reversed"],
};
for (const b of document.querySelectorAll ("[data-edit]"))
    b.onclick = () =>
    {
        const [fn, label] = edits[b.dataset.edit];
        if ((b.dataset.edit === "keep" || b.dataset.edit === "delete") && ! ed.selection)
            return status ("Drag across the waveform to select first", true);
        edit (fn, label);
    };

function refreshEditor()
{
    const has = hasSound();
    $("wave-empty").hidden = has;
    for (const id of ["play", "select-all", "save", "to-pad", "chop-to-pads", "find-hits", "fx-apply"])
        $(id).disabled = ! has;
    for (const b of document.querySelectorAll ("[data-edit]"))
        b.disabled = ! has;
    $("undo").disabled = ! ed.undo.length;
    $("redo").disabled = ! ed.redo.length;

    if (has)
    {
        const sel = ed.selection;
        $("wave-info").textContent = dsp.formatTime (dsp.length (ed.sound)) + ", " + (ed.sound.channels.length > 1 ? "stereo" : "mono")
            + (sel ? " · selected " + dsp.formatTime (sel.end - sel.start) : " · drag to select");
    }
    else
        $("wave-info").textContent = "";

    $("chop-count").textContent = ed.markers.length ? (ed.markers.length + 1) + " slices" : "";
    $("chop-to-pads").textContent = ed.markers.length ? "Chop to " + (ed.markers.length + 1) + " pads" : "Chop to pads";
    drawWave();
}

//==============================================================================
// Waveform: drag to select; with Chop open, tap to add or remove a marker.

const canvas = $("wave");
let peaksCache = { sound: null, width: 0, peaks: null };

function drawWave()
{
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth, h = canvas.clientHeight;
    if (canvas.width !== Math.round (w * dpr) || canvas.height !== Math.round (h * dpr))
    {
        canvas.width = Math.round (w * dpr);
        canvas.height = Math.round (h * dpr);
    }
    const g = canvas.getContext ("2d");
    g.setTransform (dpr, 0, 0, dpr, 0, 0);
    g.clearRect (0, 0, w, h);
    if (! hasSound())
        return;

    const shown = $("hear-fx").checked && ed.preview && ed.previewFor === ed.sound ? ed.preview : ed.sound;
    if (peaksCache.sound !== shown || peaksCache.width !== w)
        peaksCache = { sound: shown, width: w, peaks: dsp.peaks (shown, Math.max (1, Math.floor (w))) };

    const n = dsp.length (ed.sound);
    const x = (frame) => frame / n * w;

    if (ed.selection)
    {
        g.fillStyle = "rgba(255,138,61,0.25)";
        g.fillRect (x (ed.selection.start), 0, x (ed.selection.end) - x (ed.selection.start), h);
    }

    g.fillStyle = "#8fd3c8";
    const peaks = peaksCache.peaks;
    for (let i = 0; i < peaks.length; ++i)
    {
        const ph = Math.max (1, peaks[i] * (h - 8));
        g.fillRect (i, (h - ph) / 2, 1, ph);
    }

    g.fillStyle = "#ff8a3d";
    for (const m of ed.markers)
    {
        g.fillRect (x (m) - 1, 0, 2, h);
        g.beginPath();
        g.moveTo (x (m) - 7, 0); g.lineTo (x (m) + 7, 0); g.lineTo (x (m), 10);
        g.fill();
    }

    const pos = audio.position();
    if (pos >= 0 && shown === ed.sound)
    {
        g.fillStyle = "#ffffff";
        g.fillRect (x (pos), 0, 2, h);
    }
}

let drag = null;
canvas.addEventListener ("pointerdown", (e) =>
{
    if (! hasSound())
        return;
    canvas.setPointerCapture (e.pointerId);
    const frame = frameAt (e);
    drag = { start: frame, x: e.clientX, moved: false };
});
canvas.addEventListener ("pointermove", (e) =>
{
    if (! drag)
        return;
    if (Math.abs (e.clientX - drag.x) > 6)
        drag.moved = true;
    if (drag.moved)
    {
        const f = frameAt (e);
        ed.selection = { start: Math.min (drag.start, f), end: Math.max (drag.start, f) };
        refreshEditor();
    }
});
canvas.addEventListener ("pointerup", (e) =>
{
    if (! drag)
        return;
    const tap = ! drag.moved;
    drag = null;
    if (! tap)
        return;

    const chopOpen = document.querySelector ("#chop-count").closest ("details").open;
    if (chopOpen)
    {
        const f = frameAt (e);
        const near = ed.markers.findIndex ((m) => Math.abs (m - f) < dsp.length (ed.sound) * 0.02);
        pushUndo();
        if (near >= 0) ed.markers.splice (near, 1);
        else ed.markers = [...ed.markers, f].sort ((a, b) => a - b);
    }
    else
        ed.selection = null;
    refreshEditor();
});

function frameAt (e)
{
    const r = canvas.getBoundingClientRect();
    return Math.round (Math.min (1, Math.max (0, (e.clientX - r.left) / r.width)) * dsp.length (ed.sound));
}

window.addEventListener ("resize", drawWave);

//==============================================================================
// Recording

const recorder = new audio.Recorder();
let recordTimer = 0;

$("record").onclick = async () =>
{
    if (recorder.recording)
        return stopRecording();

    try
    {
        await recorder.open();
    }
    catch (e)
    {
        return status (e.name === "NotAllowedError" ? "Microphone access is off. Turn it on in Settings > Safari > Microphone."
                                                    : "Couldn't open the microphone: " + e.message, true);
    }

    audio.stop();
    recorder.start();
    $("record").classList.add ("on");
    recordTimer = setInterval (() =>
    {
        const s = recorder.seconds();
        $("rectime").textContent = "Recording " + dsp.formatTime (Math.round (s * dsp.RATE));
        if (s >= MAX_RECORD_SECONDS)
            stopRecording();
    }, 100);
};

function stopRecording()
{
    clearInterval (recordTimer);
    $("record").classList.remove ("on");
    const take = recorder.stop();
    // Let go of the mic between takes, so the phone's mic light goes off and playback goes
    // back to the speaker.
    recorder.close();
    $("rectime").textContent = "Tap to record";
    if (dsp.length (take) < dsp.RATE * 0.05)
        return status ("That recording was too short", true);
    openSound (take, "Take " + new Date().toLocaleTimeString ([], { hour: "numeric", minute: "2-digit" }));
    status ("Recorded " + dsp.formatTime (dsp.length (take)));
}

function meterLoop()
{
    if (recorder.recording)
        $("meter").style.width = Math.min (100, Math.max (0, (20 * Math.log10 (recorder.takeLevel() + 1e-6) + 60) / 60 * 100)) + "%";
    if (audio.position() >= 0)
        drawWave();
    requestAnimationFrame (meterLoop);
}
requestAnimationFrame (meterLoop);

// A recording stops if the phone locks or you switch apps; keep what was recorded.
document.addEventListener ("visibilitychange", () =>
{
    if (document.hidden && recorder.recording)
        stopRecording();
});

//==============================================================================
// Playback and effects

async function currentOutput (selectionOnly)
{
    let s = ed.sound;
    if (selectionOnly && ed.selection)
        s = dsp.crop (s, ed.selection);
    return audio.renderFx (s, ed.fx);
}

async function ensurePreview()
{
    if (ed.previewFor === ed.sound || audio.fxIsNeutral (ed.fx))
        return;
    const forSound = ed.sound;
    const rendered = await audio.renderFx (forSound, ed.fx);
    if (forSound === ed.sound)
    {
        ed.preview = rendered;
        ed.previewFor = forSound;
        drawWave();
    }
}

function invalidatePreview()
{
    ed.preview = null;
    ed.previewFor = null;
    $("fx-state").textContent = audio.fxIsNeutral (ed.fx) ? "" : "on";
}

$("play").onclick = async () =>
{
    if (audio.position() >= 0)
    {
        audio.stop();
        $("play").textContent = "Play";
        return;
    }
    await audio.unlock();
    const withFx = $("hear-fx").checked && ! audio.fxIsNeutral (ed.fx);
    if (withFx)
        await ensurePreview();
    const s = withFx && ed.preview ? ed.preview : ed.sound;
    const range = ! withFx && ed.selection ? ed.selection : null;
    $("play").textContent = "Stop";
    audio.play (s, range, () => { $("play").textContent = "Play"; drawWave(); });
};

const fxSliders = [
    ["pitch", "Pitch", -24, 24, 1, (v) => (v > 0 ? "+" : "") + v + " st"],
    ["lowCut", "Low cut", 20, 2000, 1, (v) => v <= 20 ? "off" : Math.round (v) + " Hz", true],
    ["highCut", "High cut", 200, 20000, 1, (v) => v >= 20000 ? "off" : Math.round (v) + " Hz", true],
    ["resonance", "Resonance", 0, 1, 0.01, (v) => Math.round (v * 100) + "%"],
    ["drive", "Drive", 0, 1, 0.01, (v) => Math.round (v * 100) + "%"],
    ["bits", "Bits", 2, 16, 1, (v) => v >= 16 ? "off" : v + " bit"],
    ["rate", "Rate", 2000, 44100, 1, (v) => v >= 44100 ? "off" : (v / 1000).toFixed (1) + " kHz", true],
    ["delayTime", "Delay time", 0.03, 1, 0.01, (v) => Math.round (v * 1000) + " ms"],
    ["delayFeedback", "Feedback", 0, 0.9, 0.01, (v) => Math.round (v * 100) + "%"],
    ["delayMix", "Delay", 0, 1, 0.01, (v) => v === 0 ? "off" : Math.round (v * 100) + "%"],
    ["reverbSize", "Room size", 0, 1, 0.01, (v) => Math.round (v * 100) + "%"],
    ["reverbMix", "Reverb", 0, 1, 0.01, (v) => v === 0 ? "off" : Math.round (v * 100) + "%"],
];

// Frequency-like sliders move on a log scale so the low end is usable with a thumb.
const toSlider = (v, min, max, log) => log ? Math.log (v / min) / Math.log (max / min) : v;
const fromSlider = (t, min, max, log) => log ? min * Math.pow (max / min, t) : t;

function buildFxSliders()
{
    const box = $("fx-sliders");
    box.textContent = "";
    for (const [key, label, min, max, step, show, log] of fxSliders)
    {
        const row = document.createElement ("label");
        row.className = "slider";
        const name = document.createElement ("span");
        name.textContent = label;
        const input = document.createElement ("input");
        input.type = "range";
        input.min = log ? 0 : min;
        input.max = log ? 1 : max;
        input.step = log ? 0.001 : step;
        input.value = toSlider (ed.fx[key], min, max, log);
        const out = document.createElement ("output");
        out.textContent = show (ed.fx[key]);
        input.oninput = () =>
        {
            let v = fromSlider (Number (input.value), min, max, log);
            if (! log) v = Number (v.toFixed (3));
            else if (key === "lowCut" && v < 21) v = 20;
            else if (key === "highCut" && v > 19900) v = 20000;
            else if (key === "rate" && v > 44000) v = 44100;
            ed.fx[key] = v;
            out.textContent = show (v);
            invalidatePreview();
            drawWave();
        };
        row.append (name, input, out);
        box.append (row);
    }
}
buildFxSliders();

$("fx-reset").onclick = () => { ed.fx = audio.defaultFx(); buildFxSliders(); invalidatePreview(); drawWave(); };
$("fx-apply").onclick = async () =>
{
    if (audio.fxIsNeutral (ed.fx))
        return status ("Move an effect slider first", true);
    const rendered = await audio.renderFx (ed.sound, ed.fx);
    edit (() => rendered, "Applied the effects");
    $("fx-reset").onclick();
};
$("hear-fx").onchange = drawWave;

//==============================================================================
// Chop

$("find-hits").onclick = () =>
{
    pushUndo();
    ed.markers = dsp.findHits (ed.sound, Number ($("sensitivity").value));
    refreshEditor();
    status (ed.markers.length ? "Found " + (ed.markers.length + 1) + " slices" : "No clear hits. Try a higher sensitivity.", ! ed.markers.length);
};
$("slices").onchange = () =>
{
    const n = Number ($("slices").value);
    $("slices").value = "";
    if (! n || ! hasSound())
        return;
    pushUndo();
    ed.markers = dsp.gridMarkers (dsp.length (ed.sound), n);
    refreshEditor();
};
$("clear-markers").onclick = () => { if (ed.markers.length) { pushUndo(); ed.markers = []; refreshEditor(); } };

//==============================================================================
// Library

const soundName = () => $("name").value.trim() || "Untitled";

$("save").onclick = async () =>
{
    const out = await currentOutput (false);
    await store.saveSound (soundName(), out);
    status ("Saved " + soundName() + " to your library");
};

async function refreshLibrary()
{
    const all = await store.listSounds();
    const q = $("search").value.trim().toLowerCase();
    const shown = all.filter ((s) => ! q || s.name.toLowerCase().includes (q));
    const list = $("sound-list");
    list.textContent = "";
    $("sounds-empty").hidden = all.length > 0;

    for (const info of shown)
    {
        const li = document.createElement ("li");
        const text = document.createElement ("div");
        text.className = "info";
        const name = document.createElement ("div");
        name.className = "name";
        name.textContent = info.name;
        const sub = document.createElement ("div");
        sub.className = "dim small";
        sub.textContent = dsp.formatTime (info.frames) + (info.channelCount > 1 ? ", stereo" : "") + " · " + new Date (info.created).toLocaleDateString();
        text.append (name, sub);
        text.onclick = async () => { await audio.unlock(); audio.play (await store.loadAudio (info.id)); };

        const button = (label, fn) => { const b = document.createElement ("button"); b.textContent = label; b.onclick = fn; return b; };
        li.append (text,
            button ("Edit", async () => { openSound (await store.loadAudio (info.id), info.name); showPage ("record"); }),
            button ("Pad", async () => { placeOnPads ([{ id: info.id, name: info.name }]); showPage ("pads"); }),
            button ("Share", async () => shareFile (info.name + ".wav", encodeWav (await store.loadAudio (info.id)), "audio/wav")),
            button ("…", () => soundMenu (info)));
        list.append (li);
    }
}

async function soundMenu (info)
{
    const choice = prompt ("Rename " + info.name + ", or type DELETE to remove it:", info.name);
    if (choice == null)
        return;
    if (choice.trim() === "DELETE")
    {
        await store.deleteSound (info.id);
        status ("Deleted " + info.name);
    }
    else if (choice.trim() && choice.trim() !== info.name)
    {
        await store.renameSound (info.id, choice.trim());
    }
    refreshLibrary();
}

$("search").oninput = refreshLibrary;

$("import").onchange = async () =>
{
    const files = [...$("import").files];
    $("import").value = "";
    let ok = 0;
    for (const f of files)
    {
        try
        {
            const bytes = new Uint8Array (await f.arrayBuffer());
            let s;
            if (/\.snd$/i.test (f.name))
            {
                const snd = mpc.readSnd (bytes);
                const toFloat = (c) => Float32Array.from (c, (v) => v / 32768);
                s = { channels: snd.right ? [toFloat (snd.left), toFloat (snd.right)] : [toFloat (snd.left)] };
            }
            else
                s = await audio.decode (bytes.buffer);
            await store.saveSound (f.name.replace (/\.[^.]+$/, ""), s);
            ++ok;
        }
        catch (e)
        {
            status ("Couldn't read " + f.name, true);
        }
    }
    if (ok)
        status ("Imported " + ok + (ok === 1 ? " sound" : " sounds"));
    refreshLibrary();
};

//==============================================================================
// Pads and the kit

const kit = { name: "PHONE KIT", pads: Array (mpc.PAD_COUNT).fill (null) };   // pad: { id, name, volume, pan, tune, mode }
const buffers = new Map();   // sound id -> AudioBuffer
let padBank = 0;
let selectedPad = 0;

async function bufferFor (id)
{
    if (! buffers.has (id))
    {
        const s = await store.loadAudio (id);
        if (! s)
            return null;
        buffers.set (id, audio.toAudioBuffer (s));
    }
    return buffers.get (id);
}

const saveKit = () => store.saveKit ({ name: kit.name, pads: kit.pads });

const padLabel = (pad) => "ABCD"[Math.floor (pad / PADS_PER_BANK)] + String (pad % PADS_PER_BANK + 1).padStart (2, "0");

function renderPads()
{
    const grid = $("pads");
    grid.textContent = "";
    // Laid out like the MPC: pads 13-16 along the top, 1-4 along the bottom.
    for (let row = 3; row >= 0; --row)
        for (let col = 0; col < 4; ++col)
        {
            const pad = padBank * PADS_PER_BANK + row * 4 + col;
            const p = kit.pads[pad];
            const el = document.createElement ("div");
            el.className = "pad" + (p ? " full" : "") + (pad === selectedPad && $("pad-edit").checked ? " selected" : "");
            el.dataset.pad = pad;
            const label = document.createElement ("span");
            label.textContent = padLabel (pad);
            const name = document.createElement ("span");
            name.className = "pname";
            name.textContent = p ? p.name : "";
            el.append (label, name);
            el.onpointerdown = (e) => padDown (pad, el, e);
            el.onpointerup = el.onpointercancel = () => padUp (pad, el);
            grid.append (el);
        }

    for (const b of document.querySelectorAll ("[data-bank]"))
        b.classList.toggle ("on", Number (b.dataset.bank) === padBank);

    $("kit-name").value = kit.name;
    const used = kit.pads.filter (Boolean).length;
    $("kit-memory").textContent = used ? used + " pads" : "Tap Pad next to a sound in your library, or To pad after recording.";
    refreshPadPanel();
}

async function padDown (pad, el, e)
{
    e.preventDefault();
    el.classList.add ("lit");
    if ($("pad-edit").checked)
    {
        selectedPad = pad;
        renderPads();
    }
    const p = kit.pads[pad];
    if (! p)
        return;
    await audio.unlock();
    const b = await bufferFor (p.id);
    if (b)
        audio.playPad (pad, b, p);
}

function padUp (pad, el)
{
    el.classList.remove ("lit");
    if (kit.pads[pad]?.mode === "noteOff")
        audio.releasePad (pad);
}

for (const b of document.querySelectorAll ("[data-bank]"))
    b.onclick = () => { padBank = Number (b.dataset.bank); selectedPad = padBank * PADS_PER_BANK; renderPads(); };
$("pad-edit").onchange = renderPads;
$("kit-name").onchange = () => { kit.name = $("kit-name").value.trim() || "PHONE KIT"; saveKit(); };

function refreshPadPanel()
{
    const panel = $("pad-panel");
    panel.hidden = ! $("pad-edit").checked;
    const p = kit.pads[selectedPad];
    $("pad-title").textContent = "Pad " + padLabel (selectedPad);
    $("pad-sound").textContent = p ? p.name : "empty";
    for (const id of ["pad-volume", "pad-pan", "pad-tune", "pad-mode", "pad-chop", "pad-clear"])
        $(id).disabled = ! p;
    $("pad-volume").value = p?.volume ?? 100;
    $("pad-pan").value = p?.pan ?? 50;
    $("pad-tune").value = p?.tune ?? 0;
    $("pad-mode").value = p?.mode ?? "oneShot";
}

for (const [id, key] of [["pad-volume", "volume"], ["pad-pan", "pan"], ["pad-tune", "tune"], ["pad-mode", "mode"]])
    $(id).onchange = () =>
    {
        const p = kit.pads[selectedPad];
        if (! p) return;
        p[key] = key === "mode" ? $(id).value : Number ($(id).value);
        saveKit();
    };

$("pad-clear").onclick = () => { kit.pads[selectedPad] = null; saveKit(); renderPads(); };

// Puts library sounds on pads in order, from the selected pad.
function placeOnPads (sounds)
{
    let pad = selectedPad;
    for (const s of sounds)
    {
        if (pad >= mpc.PAD_COUNT)
            break;
        kit.pads[pad++] = { id: s.id, name: s.name, volume: 100, pan: 50, tune: 0, mode: "oneShot" };
    }
    padBank = Math.floor (selectedPad / PADS_PER_BANK);
    saveKit();
    renderPads();
    const placed = pad - selectedPad;
    status ("Put " + placed + (placed === 1 ? " sound" : " sounds") + " on pads from " + padLabel (selectedPad)
            + (placed < sounds.length ? ". The rest didn't fit." : ""), placed < sounds.length);
}

async function saveSlices (slices, base)
{
    const saved = [];
    for (let i = 0; i < slices.length; ++i)
        saved.push (await store.saveSound (base + " " + (i + 1), slices[i], { chop: true }));
    return saved;
}

$("to-pad").onclick = async () =>
{
    const out = await currentOutput (true);
    const info = await store.saveSound (soundName(), out);
    placeOnPads ([info]);
    showPage ("pads");
};

$("chop-to-pads").onclick = async () =>
{
    const out = await currentOutput (false);
    // Markers were placed on the sound before effects; pitch changes its length.
    const scale = dsp.length (out) / dsp.length (ed.sound);
    const markers = ed.markers.length ? ed.markers.map ((m) => Math.round (m * scale)) : dsp.findHits (out, Number ($("sensitivity").value));
    if (! markers.length)
        return status ("No hits found. Add markers in Chop first.", true);
    const slices = dsp.slice (out, dsp.snapToZeroCrossings (out, markers));
    placeOnPads (await saveSlices (slices, soundName().slice (0, 13)));
    showPage ("pads");
};

$("pad-chop").onclick = async () =>
{
    const p = kit.pads[selectedPad];
    if (! p) return;
    const n = prompt ("Chop " + p.name + " across pads: type 0 to cut at each hit, or 2, 4, 8 or 16 for equal slices", "0");
    if (n == null) return;
    const s = await store.loadAudio (p.id);
    const slices = dsp.chop (s, Number (n) || 0);
    if (slices.length < 2)
        return status ("Couldn't find separate hits. Try equal slices.", true);
    placeOnPads (await saveSlices (slices, p.name.slice (0, 13)));
};

//==============================================================================
// Export for the MPC: a zip with the kit's folder (.PGM plus .SND files). Unzip it in the
// Files app onto the MPC's SD card (with an SD card reader), or AirDrop it to the Mac.

const overlap = { oneShot: mpc.VoiceOverlap.poly, mono: mpc.VoiceOverlap.mono, noteOff: mpc.VoiceOverlap.noteOff };

$("export").onclick = async () =>
{
    const pads = [];
    for (let pad = 0; pad < mpc.PAD_COUNT; ++pad)
    {
        const p = kit.pads[pad];
        if (! p) continue;
        const s = await store.loadAudio (p.id);
        if (! s) continue;
        pads.push ({ pad, volume: p.volume, pan: p.pan, tune: p.tune, voiceOverlap: overlap[p.mode],
                     sound: mpc.newSound (p.name, dsp.toInt16 (s.channels[0]), s.channels[1] ? dsp.toInt16 (s.channels[1]) : null) });
    }
    if (! pads.length)
        return status ("Put some sounds on pads first", true);

    const bank = mpc.buildBank (kit.name, pads);
    const files = mpc.bankFiles (bank);
    const bytes = zip (files);
    const mb = (bytes.length / 1048576).toFixed (1);
    await shareFile (bank.folderName + ".zip", bytes, "application/zip");
    status ("Exported " + bank.folderName + " (" + pads.length + " sounds, " + mb + " MB)");
};

//==============================================================================

async function start()
{
    refreshEditor();
    const saved = await store.loadKit().catch (() => null);
    if (saved)
    {
        kit.name = saved.name;
        kit.pads = saved.pads;
    }
    const kept = await store.persist();
    $("persist-note").textContent = kept ? "" : "Tip: add Crate to your Home Screen so iOS keeps your sounds.";

    if ("serviceWorker" in navigator)
        navigator.serviceWorker.register ("sw.js").catch (() => {});
}

start();
