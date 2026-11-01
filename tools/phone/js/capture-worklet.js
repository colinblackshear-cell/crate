// Runs on the audio thread: hands each block of mic input to the page while recording, and
// always reports the level for the meter.
class Capture extends AudioWorkletProcessor
{
    constructor()
    {
        super();
        this.recording = false;
        this.port.onmessage = (e) => { this.recording = e.data === "start"; };
    }

    process (inputs)
    {
        const input = inputs[0];
        if (input && input.length > 0)
        {
            let peak = 0;
            for (const c of input)
                for (let i = 0; i < c.length; ++i)
                    peak = Math.max (peak, Math.abs (c[i]));

            this.port.postMessage ({ peak, block: this.recording ? input.map ((c) => c.slice()) : null });
        }
        return true;
    }
}

registerProcessor ("crate-capture", Capture);
