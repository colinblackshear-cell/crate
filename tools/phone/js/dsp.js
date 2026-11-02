// Sound as the phone app passes it around: { channels: [Float32Array, ...] } at 44.1 kHz, the
// MPC2000XL's rate (and every Crate tool's). Every edit returns a new sound; nothing is changed
// in place, so undo can keep the old one. No browser APIs here, so the tests run in Node.

export const RATE = 44100;

export const length = (s) => s.channels[0].length;
export const seconds = (s) => length (s) / RATE;

export function make (channelCount, frames)
{
    return { channels: Array.from ({ length: channelCount }, () => new Float32Array (frames)) };
}

export function formatTime (frames)
{
    const s = frames / RATE;
    if (s < 60)
        return s.toFixed (s < 10 ? 2 : 1) + " s";
    const m = Math.floor (s / 60);
    return m + ":" + (s - m * 60).toFixed (1).padStart (4, "0");
}

// Resamples to 44.1 kHz with 4-point cubic interpolation (a phone records at 48 kHz).
export function resample (channels, fromRate)
{
    if (Math.abs (fromRate - RATE) < 0.5)
        return { channels: channels.map ((c) => Float32Array.from (c)) };

    const ratio = fromRate / RATE;
    const outLength = Math.max (1, Math.floor (channels[0].length / ratio));
    return {
        channels: channels.map ((input) =>
        {
            const out = new Float32Array (outLength);
            const n = input.length;
            const at = (i) => input[Math.min (n - 1, Math.max (0, i))];
            for (let i = 0; i < outLength; ++i)
            {
                const x = i * ratio, k = Math.floor (x), t = x - k;
                const y0 = at (k - 1), y1 = at (k), y2 = at (k + 1), y3 = at (k + 2);
                out[i] = y1 + 0.5 * t * (y2 - y0 + t * (2 * y0 - 5 * y1 + 4 * y2 - y3 + t * (3 * (y1 - y2) + y3 - y0)));
            }
            return out;
        }),
    };
}

//==============================================================================
// Edits. A range is { start, end } in frames; null means the whole sound.

const whole = (s, r) => r && r.end > r.start ? r : { start: 0, end: length (s) };

export const crop = (s, r) => { r = whole (s, r); return { channels: s.channels.map ((c) => c.slice (r.start, r.end)) }; };

export function remove (s, r)
{
    r = whole (s, r);
    return {
        channels: s.channels.map ((c) =>
        {
            const out = new Float32Array (c.length - (r.end - r.start));
            out.set (c.subarray (0, r.start));
            out.set (c.subarray (r.end), r.start);
            return out;
        }),
    };
}

export function reverse (s, r)
{
    r = whole (s, r);
    const out = { channels: s.channels.map ((c) => Float32Array.from (c)) };
    for (const c of out.channels)
        c.subarray (r.start, r.end).reverse();
    return out;
}

export function peak (s, r)
{
    r = whole (s, r);
    let p = 0;
    for (const c of s.channels)
        for (let i = r.start; i < r.end; ++i)
            p = Math.max (p, Math.abs (c[i]));
    return p;
}

export function gain (s, r, g)
{
    r = whole (s, r);
    const out = { channels: s.channels.map ((c) => Float32Array.from (c)) };
    for (const c of out.channels)
        for (let i = r.start; i < r.end; ++i)
            c[i] *= g;
    return out;
}

export function normalize (s, r)
{
    const p = peak (s, r);
    return p > 0.0001 ? gain (s, r, 0.98 / p) : s;
}

// Fades in or out across the range, on an equal-power curve.
export function fade (s, r, fadeIn)
{
    r = whole (s, r);
    const out = { channels: s.channels.map ((c) => Float32Array.from (c)) };
    const n = Math.max (1, r.end - r.start);
    for (const c of out.channels)
        for (let i = r.start; i < r.end; ++i)
        {
            const t = (i - r.start) / n;
            c[i] *= Math.sin ((fadeIn ? t : 1 - t) * Math.PI / 2);
        }
    return out;
}

// The part of the sound louder than threshold (0.01 = -40 dB), with a little room either side.
export function audibleRange (s, threshold = 0.01)
{
    const n = length (s);
    const loud = (i) => s.channels.some ((c) => Math.abs (c[i]) > threshold);
    let start = 0, end = n;
    while (start < n && ! loud (start)) ++start;
    while (end > start && ! loud (end - 1)) --end;
    if (start >= end)
        return { start: 0, end: n };
    return { start: Math.max (0, start - 220), end: Math.min (n, end + 2205) };
}

export const trimSilence = (s) => crop (s, audibleRange (s));

export function toMono (s)
{
    if (s.channels.length === 1)
        return s;
    const [l, r] = s.channels;
    return { channels: [l.map ((v, i) => 0.5 * (v + r[i]))] };
}

//==============================================================================
// Chopping. Markers are frames where slices start; 0 is implied.

// Energy-based hit detection, the same as the desktop tools: a short frame much louder than
// the frames before it is a hit.
export function findHits (s, sensitivity = 0.5, maxHits = 64)
{
    const frame = 512;
    const n = length (s);
    const frames = Math.floor (n / frame);
    const energy = new Float32Array (frames);

    for (let f = 0; f < frames; ++f)
    {
        let sum = 0;
        for (const c of s.channels)
            for (let i = f * frame; i < (f + 1) * frame; ++i)
                sum += c[i] * c[i];
        energy[f] = sum / (frame * s.channels.length);
    }

    const ratio = 8 + (1.6 - 8) * Math.min (1, Math.max (0, sensitivity));
    const floor = 1e-5;
    const minGap = Math.floor (0.06 * RATE / frame);
    const hits = [];
    let lastHit = -minGap;
    const d = s.channels[0];

    for (let f = 1; f < frames; ++f)
    {
        const window = Math.min (f, 8);
        let before = 0;
        for (let k = 1; k <= window; ++k)
            before += energy[f - k];
        before /= window;

        if (energy[f] > floor && energy[f] > before * ratio && f - lastHit >= minGap)
        {
            // the start of the attack, inside the frame before the jump
            let position = (f - 1) * frame;
            const level = Math.sqrt (energy[f]) * 0.25;
            for (let i = (f - 1) * frame; i < (f + 1) * frame && i < n; ++i)
                if (Math.abs (d[i]) > level) { position = i; break; }
            hits.push ({ position: Math.max (0, position - 32), strength: energy[f] / (before + floor) });
            lastHit = f;
        }
    }

    hits.sort ((a, b) => b.strength - a.strength);
    return hits.slice (0, maxHits)
               .map ((h) => h.position)
               .filter ((p) => p > 0.02 * RATE)   // a hit right at the start is the start
               .sort ((a, b) => a - b);
}

export function gridMarkers (frames, slices)
{
    const markers = [];
    for (let i = 1; i < slices; ++i)
        markers.push (Math.floor (frames * i / slices));
    return markers;
}

// Moves each marker to the nearest zero crossing within 5 ms, so slices don't click.
export function snapToZeroCrossings (s, markers)
{
    const reach = Math.floor (0.005 * RATE);
    const d = s.channels[0];
    return markers.map ((m) =>
    {
        for (let offset = 0; offset < reach; ++offset)
            for (const c of [m - offset, m + offset])
                if (c > 0 && c < d.length && (d[c - 1] <= 0) !== (d[c] <= 0))
                    return c;
        return m;
    });
}

export function slice (s, markers)
{
    const n = length (s);
    const edges = [0];
    for (const m of [...markers].sort ((a, b) => a - b))
        if (m > edges[edges.length - 1] && m < n)
            edges.push (m);
    edges.push (n);

    const slices = [];
    for (let i = 0; i + 1 < edges.length; ++i)
    {
        const piece = crop (s, { start: edges[i], end: edges[i + 1] });
        const f = Math.min (64, Math.floor (length (piece) / 4));   // a tiny fade so slices end cleanly
        slices.push (fade (piece, { start: length (piece) - f, end: length (piece) }, false));
    }
    return slices;
}

export function chop (s, slices, sensitivity = 0.5)
{
    const markers = slices > 0 ? gridMarkers (length (s), slices) : findHits (s, sensitivity);
    return slice (s, snapToZeroCrossings (s, markers));
}

//==============================================================================
// For the MPC: 16-bit samples, with the pad's settings baked in.

export function toInt16 (c)
{
    const out = new Int16Array (c.length);
    for (let i = 0; i < c.length; ++i)
        out[i] = Math.round (Math.max (-1, Math.min (1, c[i])) * 32767);
    return out;
}

// Peaks for drawing: `buckets` maximum levels across the sound.
export function peaks (s, buckets)
{
    const n = length (s);
    const out = new Float32Array (buckets);
    for (let b = 0; b < buckets; ++b)
    {
        const from = Math.floor (n * b / buckets), to = Math.max (from + 1, Math.floor (n * (b + 1) / buckets));
        let p = 0;
        for (const c of s.channels)
            for (let i = from; i < to && i < n; ++i)
                p = Math.max (p, Math.abs (c[i]));
        out[b] = p;
    }
    return out;
}
