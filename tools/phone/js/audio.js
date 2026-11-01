// Everything that makes sound: recording from the mic, playing sounds and pads, and rendering
// effects. Browsers only start audio after a tap, so call unlock() from a tap first.

import * as dsp from "./dsp.js";

let ctx = null;
let micOpen = false;

export function context()
{
    ctx ??= new AudioContext ({ latencyHint: "interactive" });
    return ctx;
}

// On iPhone, Safari mutes web audio when the ring/silent switch is on unless the page says it
// plays media. It switches to play-and-record while the mic is open.
function setSession (type)
{
    try { if (navigator.audioSession) navigator.audioSession.type = type; } catch {}
}

export async function unlock()
{
    const c = context();
    if (! micOpen)
        setSession ("playback");
    if (c.state !== "running")
        await c.resume();
    return c;
}

export function toAudioBuffer (sound)
{
    const c = context();
    const b = c.createBuffer (sound.channels.length, Math.max (1, dsp.length (sound)), dsp.RATE);
    sound.channels.forEach ((ch, i) => b.copyToChannel (ch, i));
    return b;
}

export async function decode (arrayBuffer)
{
    const b = await context().decodeAudioData (arrayBuffer);
    const channels = [];
    for (let i = 0; i < Math.min (2, b.numberOfChannels); ++i)
        channels.push (b.getChannelData (i));
    return dsp.resample (channels, b.sampleRate);
}

//==============================================================================
// Recording

export class Recorder
{
    constructor() { this.blocks = []; this.level = 0; this.recording = false; }

    async open()
    {
        if (this.node)
            return;
        try { await this.openMic(); }
        catch (e) { this.close(); throw e; }
    }

    async openMic()
    {
        micOpen = true;
        setSession ("play-and-record");
        const c = await unlock();
        // Raw sound: the phone's call processing makes field recordings sound pumped and dull.
        this.stream = await navigator.mediaDevices.getUserMedia ({
            audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: 2 } });
        await c.audioWorklet.addModule (new URL ("./capture-worklet.js", import.meta.url));
        this.source = c.createMediaStreamSource (this.stream);
        this.node = new AudioWorkletNode (c, "crate-capture", { numberOfOutputs: 0 });
        this.node.port.onmessage = (e) =>
        {
            this.level = Math.max (this.level, e.data.peak);
            if (e.data.block && this.recording)
                this.blocks.push (e.data.block);
        };
        this.source.connect (this.node);
    }

    start()
    {
        this.blocks = [];
        this.recording = true;
        this.node.port.postMessage ("start");
    }

    seconds() { return this.blocks.reduce ((n, b) => n + b[0].length, 0) / context().sampleRate; }

    takeLevel() { const l = this.level; this.level = 0; return l; }

    // Ends the recording and returns it at 44.1 kHz. A stereo input whose channels are the
    // same (most phone mics) comes back mono.
    stop()
    {
        this.recording = false;
        this.node.port.postMessage ("stop");
        const frames = this.blocks.reduce ((n, b) => n + b[0].length, 0);
        const channelCount = Math.min (2, this.blocks[0]?.length ?? 1);
        const channels = Array.from ({ length: channelCount }, () => new Float32Array (frames));
        let pos = 0;
        for (const b of this.blocks)
        {
            for (let ch = 0; ch < channelCount; ++ch)
                channels[ch].set (b[Math.min (ch, b.length - 1)], pos);
            pos += b[0].length;
        }
        this.blocks = [];

        if (channelCount === 2 && channels[0].every ((v, i) => Math.abs (v - channels[1][i]) < 1e-4))
            channels.pop();
        return dsp.resample (channels, context().sampleRate);
    }

    close()
    {
        this.stream?.getTracks().forEach ((t) => t.stop());
        this.source?.disconnect();
        this.node = this.source = this.stream = null;
        micOpen = false;
        setSession ("playback");
    }
}

//==============================================================================
// Playback of the sound being edited

let preview = null;

export function play (sound, range, onEnded)
{
    stop();
    const c = context();
    const src = c.createBufferSource();
    src.buffer = toAudioBuffer (sound);
    src.connect (c.destination);
    const start = range ? range.start / dsp.RATE : 0;
    const dur = range ? (range.end - range.start) / dsp.RATE : undefined;
    src.start (0, start, dur);
    const startedAt = c.currentTime;
    preview = { src, startedAt, offset: start };
    src.onended = () => { if (preview?.src === src) { preview = null; onEnded?.(); } };
}

export function stop()
{
    if (preview)
    {
        const p = preview;
        preview = null;
        try { p.src.stop(); } catch {}
    }
}

// Where the preview is, in frames, or -1.
export function position()
{
    return preview ? Math.round ((context().currentTime - preview.startedAt + preview.offset) * dsp.RATE) : -1;
}

//==============================================================================
// Pads

const playing = new Map();   // pad -> sources still sounding

export function playPad (pad, buffer, { volume = 100, pan = 50, tune = 0, mode = "oneShot" } = {})
{
    const c = context();
    if (mode !== "oneShot")
        releasePad (pad);

    const src = c.createBufferSource();
    src.buffer = buffer;
    src.playbackRate.value = Math.pow (2, tune / 120);
    const g = c.createGain();
    g.gain.value = volume / 100;
    const p = c.createStereoPanner();
    p.pan.value = (pan - 50) / 50;
    src.connect (g).connect (p).connect (c.destination);
    src.start();

    const list = playing.get (pad) ?? [];
    list.push ({ src, g });
    playing.set (pad, list);
    src.onended = () => playing.set (pad, (playing.get (pad) ?? []).filter ((v) => v.src !== src));
}

// Stops a pad with a short fade so it doesn't click.
export function releasePad (pad)
{
    const c = context();
    for (const { src, g } of playing.get (pad) ?? [])
    {
        g.gain.setTargetAtTime (0, c.currentTime, 0.005);
        try { src.stop (c.currentTime + 0.05); } catch {}
    }
    playing.set (pad, []);
}

//==============================================================================
// Effects, rendered offline: the result is a new sound.

export const defaultFx = () => ({ pitch: 0, lowCut: 20, highCut: 20000, resonance: 0, drive: 0, bits: 16,
                                  rate: 44100, delayTime: 0.25, delayFeedback: 0.4, delayMix: 0, reverbSize: 0.5, reverbMix: 0 });

export function fxIsNeutral (fx)
{
    const d = defaultFx();
    return Object.keys (d).every ((k) => k === "delayTime" || k === "delayFeedback" || k === "reverbSize" || fx[k] === d[k]);
}

function impulse (c, size)
{
    const seconds = 0.3 + size * 3.5;
    const n = Math.floor (seconds * dsp.RATE);
    const b = c.createBuffer (2, n, dsp.RATE);
    for (let ch = 0; ch < 2; ++ch)
    {
        const d = b.getChannelData (ch);
        for (let i = 0; i < n; ++i)
            d[i] = (Math.random() * 2 - 1) * Math.pow (1 - i / n, 2 + (1 - size) * 4);
    }
    return b;
}

export async function renderFx (sound, fx)
{
    if (fxIsNeutral (fx))
        return sound;

    const rate = Math.pow (2, fx.pitch / 12);   // tape style: faster is higher
    const body = dsp.length (sound) / rate;
    const tail = (fx.delayMix > 0 ? Math.min (4, fx.delayTime * 12 * fx.delayFeedback + fx.delayTime) : 0)
               + (fx.reverbMix > 0 ? 0.3 + fx.reverbSize * 3.5 : 0);
    const frames = Math.ceil (body + Math.min (4, tail) * dsp.RATE);

    const off = new OfflineAudioContext (2, frames, dsp.RATE);
    const src = off.createBufferSource();
    const buffer = off.createBuffer (sound.channels.length, dsp.length (sound), dsp.RATE);
    sound.channels.forEach ((ch, i) => buffer.copyToChannel (ch, i));
    src.buffer = buffer;
    src.playbackRate.value = rate;

    const q = 0.7 + fx.resonance * 12;
    const hp = off.createBiquadFilter();
    hp.type = "highpass"; hp.frequency.value = fx.lowCut; hp.Q.value = fx.lowCut > 20 ? q : 0.7;
    const lp = off.createBiquadFilter();
    lp.type = "lowpass"; lp.frequency.value = fx.highCut; lp.Q.value = fx.highCut < 20000 ? q : 0.7;

    const shaper = off.createWaveShaper();
    if (fx.drive > 0)
    {
        const k = 1 + fx.drive * 20;
        const curve = new Float32Array (2048);
        for (let i = 0; i < curve.length; ++i)
        {
            const x = i / (curve.length - 1) * 2 - 1;
            curve[i] = Math.tanh (k * x) / Math.tanh (k);
        }
        shaper.curve = curve;
        shaper.oversample = "4x";
    }

    src.connect (hp).connect (lp).connect (shaper);
    const out = off.createGain();
    shaper.connect (out);

    if (fx.delayMix > 0)
    {
        const delay = off.createDelay (2);
        delay.delayTime.value = fx.delayTime;
        const fb = off.createGain();
        fb.gain.value = fx.delayFeedback;
        const wet = off.createGain();
        wet.gain.value = fx.delayMix;
        shaper.connect (delay).connect (fb).connect (delay);
        delay.connect (wet).connect (out);
    }

    if (fx.reverbMix > 0)
    {
        const conv = off.createConvolver();
        conv.buffer = impulse (off, fx.reverbSize);
        const wet = off.createGain();
        wet.gain.value = fx.reverbMix * 0.6;
        shaper.connect (conv).connect (wet).connect (out);
    }

    out.connect (off.destination);
    src.start();
    const rendered = await off.startRendering();

    let result = { channels: sound.channels.length === 1 && fx.reverbMix === 0 && fx.delayMix === 0
                                 ? [rendered.getChannelData (0).slice()]
                                 : [rendered.getChannelData (0).slice(), rendered.getChannelData (1).slice()] };
    result = crush (result, fx.bits, fx.rate);

    // Cut the silence at the end of the tail, and keep it from clipping.
    const end = dsp.audibleRange (result, 0.001).end;
    result = dsp.crop (result, { start: 0, end: Math.max (Math.min (dsp.length (result), Math.ceil (body)), end) });
    return dsp.peak (result) > 1 ? dsp.normalize (result) : result;
}

// Bit crusher and sample rate reducer.
export function crush (sound, bits, rate)
{
    if (bits >= 16 && rate >= dsp.RATE)
        return sound;
    const steps = Math.pow (2, bits - 1);
    const hold = Math.max (1, dsp.RATE / rate);
    return {
        channels: sound.channels.map ((c) =>
        {
            const out = new Float32Array (c.length);
            let held = 0, next = 0;
            for (let i = 0; i < c.length; ++i)
            {
                if (i >= next) { held = Math.round (c[i] * steps) / steps; next += hold; }
                out[i] = held;
            }
            return out;
        }),
    };
}
