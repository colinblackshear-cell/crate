#include "MpcProcessor.h"

#include "core/control/Launchkey.h"

#include "MpcView.h"

namespace
{
    constexpr float kReleaseStep = 1.0f / 256.0f;   // about 6 ms fade when a pad stops
    constexpr int kClickChannel = 16;
}

MpcProcessor::MpcProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      library ((formats.registerBasicFormats(), formats))
{
    voices.reserve (kMaxVoices);

    padForNoteNumber.fill (-1);
    for (int pad = 0; pad < mpc::kPadCount; ++pad)
        padForNoteNumber[(size_t) noteForPad (pad)] = pad;

    // Inside the Rig (no plugin wrapper) the Rig maps the drum pads itself and sends MPC notes.
    launchkeyPads = wrapperType != wrapperType_Undefined;

    clicks.reset (44100.0);
    sequenced.ensureSize (8192);
    project.addChangeListener (this);
    project.sequenceEdits.addChangeListener (this);
    rebuildKit();
    rebuildSequence();
    startTimerHz (30);
}

MpcProcessor::~MpcProcessor()
{
    stopTimer();
    project.removeChangeListener (this);
    project.sequenceEdits.removeChangeListener (this);
}

void MpcProcessor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &project)
        rebuildKit();
    followTransport();
    rebuildSequence();

    // An edit reaches the sequence waiting to play next too.
    if (songPlaying)
        queueForSong();
    else if (queuedSequence >= 0)
        sequencer.queue (SequencerEngine::makeSnapshot (project.getSequences()[(size_t) queuedSequence], queuedSequence), switchAtBar);
}

void MpcProcessor::rebuildSequence()
{
    // If the audio thread has just moved on to the next sequence, rebuilding the one it left
    // would send it back there; the one playing is the one to rebuild.
    const int id = sequencer.getPlayingId();
    if (sequencer.isPlaying() && immediateSequence < 0 && id != project.getCurrentSequenceIndex()
        && juce::isPositiveAndBelow (id, (int) project.getSequences().size()))
        project.setCurrentSequence (id);
    sequencer.setSnapshot (SequencerEngine::makeSnapshot (project.currentSequence(), project.getCurrentSequenceIndex(), songPlaying));
}

void MpcProcessor::selectSequence (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) project.getSequences().size()))
        return;

    const bool leavingSong = songPlaying;
    songPlaying = false;
    sequencer.stopAtEnd = false;

    songStartQueued = false;
    const bool canQueue = ! sequencer.isFollowingHost() || sequencer.switchWhileFollowing.load();
    if (sequencer.isPlaying() && canQueue && index != project.getCurrentSequenceIndex())
    {
        sequencer.queue (SequencerEngine::makeSnapshot (project.getSequences()[(size_t) index], index), switchAtBar);
        queuedSequence = index;
        queuedAtPasses = sequencer.getPasses();
        if (leavingSong)
            rebuildSequence();
    }
    else
    {
        sequencer.queue (nullptr, false);
        queuedSequence = -1;
        if (sequencer.isPlaying())
            immediateSequence = index;   // straight away (playing along with Logic)
        project.setCurrentSequence (index);
        rebuildSequence();
    }
    project.sequenceChanged();
}

bool MpcProcessor::playSong (int fromStep)
{
    const auto& song = project.currentSong();
    if (! juce::isPositiveAndBelow (fromStep, (int) song.steps.size()))
        return false;

    songPlaying = true;
    songStep = fromStep;
    songRepeat = 0;
    queuedSequence = -1;
    songStartQueued = false;
    const int index = song.steps[(size_t) fromStep].sequence;
    if (! juce::isPositiveAndBelow (index, (int) project.getSequences().size()))
        return songPlaying = false;

    // In the Rig, a song started while the clock runs comes in on the next bar line, in time.
    if (sequencer.isPlaying() && sequencer.isFollowingHost() && sequencer.switchWhileFollowing.load())
    {
        sequencer.stopAtEnd = false;
        sequencer.queue (SequencerEngine::makeSnapshot (project.getSequences()[(size_t) index], index, true), true);
        songStartQueued = true;
        lastPasses = sequencer.getPasses();
        project.sequenceChanged();
        return true;
    }

    if (sequencer.isPlaying())
        immediateSequence = index;
    project.setCurrentSequence (index);
    rebuildSequence();
    lastPasses = sequencer.getPasses();
    queueForSong();
    // (In the Rig the song starts the Rig's clock, and plays along with it.)
    pressPlay (true);
    project.sequenceChanged();
    return true;
}

void MpcProcessor::queueForSong()
{
    const auto& song = project.currentSong();
    const auto& steps = song.steps;
    if (! juce::isPositiveAndBelow (songStep, (int) steps.size()))
        return;

    sequencer.stopAtEnd = false;
    if (songRepeat + 1 < steps[(size_t) songStep].repeats)
    {
        sequencer.queue (nullptr, false);   // this step goes round again
        return;
    }

    int next = songStep + 1;
    if (next >= (int) steps.size())
    {
        if (! song.loop)
        {
            sequencer.queue (nullptr, false);
            sequencer.stopAtEnd = true;     // the end of the song
            return;
        }
        next = 0;
    }
    const int index = steps[(size_t) next].sequence;
    if (juce::isPositiveAndBelow (index, (int) project.getSequences().size()))
        sequencer.queue (SequencerEngine::makeSnapshot (project.getSequences()[(size_t) index], index, true), false);
}

void MpcProcessor::advanceSong()
{
    const auto& song = project.currentSong();
    if (song.steps.empty())
        return stopTransport();

    if (++songRepeat >= song.steps[(size_t) juce::jlimit (0, (int) song.steps.size() - 1, songStep)].repeats)
    {
        songRepeat = 0;
        if (++songStep >= (int) song.steps.size())
        {
            if (! song.loop)
            {
                songPlaying = false;
                songStep = 0;
                return;
            }
            songStep = 0;
        }
    }
    queueForSong();
}

void MpcProcessor::stopTransport()
{
    sequencer.stop();
    sequencer.queue (nullptr, false);
    sequencer.stopAtEnd = false;
    queuedSequence = -1;
    immediateSequence = -1;
    songStartQueued = false;
    if (std::exchange (songPlaying, false))
        rebuildSequence();
}

void MpcProcessor::pressPlay (bool fromStart)
{
    if (hostCommand)
        return hostCommand (fromStart ? HostCommand::playFromStart : HostCommand::play);
    sequencer.play (fromStart);
}

void MpcProcessor::pressStop()
{
    if (hostCommand)
        return hostCommand (HostCommand::stop);
    stopTransport();
}

void MpcProcessor::pressRecord()
{
    if (hostCommand)
        return hostCommand (HostCommand::record);
    toggleRecord();
}

void MpcProcessor::setFollowing (bool follow)
{
    if (hostCommand)
        return hostCommand (follow ? HostCommand::beatOn : HostCommand::beatOff);
    sequencer.followHost = follow;
}

void MpcProcessor::followTransport()
{
    const bool playing = sequencer.isPlaying();
    const int id = sequencer.getPlayingId();
    const int passes = sequencer.getPasses();

    if (immediateSequence >= 0 && (! playing || id == immediateSequence))
        immediateSequence = -1;
    if (immediateSequence >= 0)
    {
        wasPlaying = playing;
        return;
    }

    // The audio thread moved on to a queued sequence: it becomes the one on screen.
    if (playing && id != project.getCurrentSequenceIndex() && juce::isPositiveAndBelow (id, (int) project.getSequences().size()))
    {
        project.setCurrentSequence (id);
        rebuildSequence();
    }
    if (queuedSequence >= 0 && (! playing || (id == queuedSequence && passes != queuedAtPasses)))
        queuedSequence = -1;

    if (songPlaying && songStartQueued)
    {
        // Waiting for the first step to take over at the bar; then the song runs as usual.
        if (! playing)
        {
            songPlaying = songStartQueued = false;
            sequencer.queue (nullptr, false);
            project.sequenceChanged();
        }
        else if (passes != lastPasses)
        {
            songStartQueued = false;
            lastPasses = passes;
            queueForSong();
            project.sequenceChanged();
        }
    }
    else if (songPlaying)
    {
        if (! playing && wasPlaying)
        {
            songPlaying = false;
            rebuildSequence();
            project.sequenceChanged();
        }
        else if (playing && passes != lastPasses)
        {
            while (lastPasses != passes && songPlaying)
            {
                ++lastPasses;
                advanceSong();
            }
            lastPasses = passes;
            project.sequenceChanged();
        }
    }
    wasPlaying = playing;
}

void MpcProcessor::toggleRecord()
{
    if (! sequencer.isRecording())
        beginEdit();
    sequencer.record();
}

void MpcProcessor::beginEdit()
{
    if (undoSequence != project.getCurrentSequenceIndex())
    {
        undoStack.clear();
        redoStack.clear();
        undoSequence = project.getCurrentSequenceIndex();
    }
    undoStack.push_back (project.currentSequence());
    if ((int) undoStack.size() > kMaxUndo)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool MpcProcessor::undoEdit()
{
    if (! canUndo())
        return false;
    redoStack.push_back (std::exchange (project.currentSequence(), std::move (undoStack.back())));
    undoStack.pop_back();
    rebuildSequence();
    project.sequenceChanged();
    return true;
}

bool MpcProcessor::redoEdit()
{
    if (! canRedo())
        return false;
    undoStack.push_back (std::exchange (project.currentSequence(), std::move (redoStack.back())));
    redoStack.pop_back();
    rebuildSequence();
    project.sequenceChanged();
    return true;
}

int MpcProcessor::collectRecorded()
{
    std::array<SequencerEngine::Recorded, 256> notes;
    int total = 0;
    for (int n; (n = sequencer.takeRecorded (notes.data(), (int) notes.size())) > 0; total += n)
    {
        auto& sq = project.currentSequence();
        auto& track = sq.tracks[(size_t) juce::jlimit (0, (int) sq.tracks.size() - 1, recordTrack)];
        for (int i = 0; i < n; ++i)
        {
            const auto& r = notes[(size_t) i];
            if (juce::isPositiveAndBelow (r.tick, sq.lengthTicks()))
                track.add ({ r.tick, r.pad, r.velocity, r.length });
        }
    }
    if (total > 0)
    {
        rebuildSequence();
        project.sequenceChanged();
    }
    return total;
}

void MpcProcessor::rebuildKit()
{
    auto next = std::make_shared<Kit>();
    const auto& bank = project.currentBank();
    next->delayBeats = juce::jlimit (0.125, 4.0, bank.delayBeats);
    next->delayFeedback = juce::jlimit (0, 90, bank.delayFeedback) / 100.0f;
    next->reverbSize = juce::jlimit (0, 100, bank.reverbSize) / 100.0f;

    for (int i = 0; i < mpc::kPadCount; ++i)
    {
        const auto& pad = bank.pads[(size_t) i];
        if (pad.isEmpty())
            continue;

        auto& s = (*next)[(size_t) i];
        s.audio = bank.audioToPlay (i);
        s.reverse = pad.reverse;
        s.mode = pad.mode;
        s.muteGroup = pad.muteGroup;
        s.rate = std::pow (2.0, pad.tune / 120.0);
        s.cutoff = juce::jlimit (20.0f, 20000.0f, pad.cutoff);
        s.resonance = juce::jlimit (0.0f, 1.0f, pad.resonance);
        s.attackSeconds = juce::jlimit (0, 2000, pad.attack) / 1000.0;
        s.decaySeconds = juce::jlimit (0, 5000, pad.decay) / 1000.0;
        s.delaySend = juce::jlimit (0, 100, pad.delaySend) / 100.0f;
        s.reverbSend = juce::jlimit (0, 100, pad.reverbSend) / 100.0f;
        next->sends |= s.delaySend > 0 || s.reverbSend > 0;

        float gain = (float) pad.volume / 100.0f;
        if (pad.normalize)
        {
            const auto peak = s.audio->getMagnitude (0, s.audio->getNumSamples());
            if (peak > 0.0001f)
                gain *= 0.98f / peak;
        }

        // Constant-power pan, unity in the middle.
        const float pan = (float) pad.pan / 100.0f;
        s.gainLeft = gain * std::cos (pan * juce::MathConstants<float>::halfPi) * juce::MathConstants<float>::sqrt2;
        s.gainRight = gain * std::sin (pan * juce::MathConstants<float>::halfPi) * juce::MathConstants<float>::sqrt2;
    }

    std::shared_ptr<const Kit> old;
    {
        const juce::SpinLock::ScopedLockType sl (kitLock);
        old = std::exchange (kit, std::move (next));
    }

    if (old != nullptr)
        retired.push_back (std::move (old));
}

void MpcProcessor::timerCallback()
{
    // Free kits no voice is playing any more. The audio thread never picks up a retired kit
    // again, so once we hold the only reference it's safe to let it go.
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& k) { return k.use_count() == 1; }),
                   retired.end());
    sequencer.freeUnusedSnapshots();
    collectRecorded();
    followTransport();
}

void MpcProcessor::playPad (int pad, float velocity)
{
    auto m = juce::MidiMessage::noteOn (kClickChannel, noteForPad (pad), juce::jlimit (0.01f, 1.0f, velocity));
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    clicks.addMessageToQueue (m);
}

void MpcProcessor::releasePad (int pad)
{
    auto m = juce::MidiMessage::noteOff (kClickChannel, noteForPad (pad));
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    clicks.addMessageToQueue (m);
}

void MpcProcessor::stopAll()
{
    stopRequested = true;
}

void MpcProcessor::preview (SharedAudio audio, juce::Range<int> range)
{
    SharedAudio old;
    {
        const juce::SpinLock::ScopedLockType sl (previewLock);
        old = std::exchange (previewAudio, std::move (audio));
        if (previewAudio != nullptr)
            range = range.getIntersectionWith ({ 0, previewAudio->getNumSamples() });
        previewRange = range.isEmpty() && previewAudio != nullptr ? juce::Range<int> (0, previewAudio->getNumSamples()) : range;
        previewPos = previewRange.getStart();
        previewFade = 1;
        previewStopping = false;
        previewDone = previewAudio == nullptr;
    }
    previewPosition = previewAudio != nullptr ? previewRange.getStart() : -1;
}

void MpcProcessor::stopPreview()
{
    const juce::SpinLock::ScopedLockType sl (previewLock);
    previewStopping = true;
}

void MpcProcessor::renderPreview (juce::AudioBuffer<float>& buffer)
{
    const juce::SpinLock::ScopedTryLockType sl (previewLock);
    if (! sl.isLocked() || previewAudio == nullptr || previewDone)
        return;

    const auto& audio = *previewAudio;
    const float* l = audio.getReadPointer (0);
    const float* r = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const double step = kMpcSampleRate / hostRate;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int index = (int) previewPos;
        if (index >= previewRange.getEnd() || previewFade <= 0.0f)
        {
            // Keep the sound: letting go of it here could free it on the audio thread.
            previewDone = true;
            previewPosition = -1;
            return;
        }
        const float frac = (float) (previewPos - index);
        const int next = juce::jmin (index + 1, audio.getNumSamples() - 1);
        const float sl0 = (l[index] + (l[next] - l[index]) * frac) * previewFade;
        const float sr0 = (r[index] + (r[next] - r[index]) * frac) * previewFade;
        if (outR != nullptr) { outL[i] += sl0; outR[i] += sr0; }
        else outL[i] += 0.5f * (sl0 + sr0);
        previewPos += step;
        if (previewStopping)
            previewFade -= kReleaseStep;
    }
    previewPosition = (int) previewPos;
}

void MpcProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate > 0 ? sampleRate : 44100.0;
    clicks.reset (hostRate);
    sequencer.prepare (hostRate);
    remapped.ensureSize (8192);
    levelsScratch.ensureSize (8192);
    const int block = juce::jmax (512, samplesPerBlock);
    delaySend.setSize (2, block);
    reverbSend.setSize (2, block);
    delayLine.setSize (2, (int) (hostRate * 4.0) + 1);
    delayLine.clear();
    delayWrite = 0;
    reverb.setSampleRate (hostRate);
    reverb.reset();
    sendTail = 0;
}

void MpcProcessor::remapDrumPads (juce::MidiBuffer& midi)
{
    bool any = false;
    for (const auto metadata : midi)
        any |= crate::launchkey::drumPadForMessage (metadata.getMessage()) >= 0;
    if (! any)
        return;

    const int bank = juce::jlimit (0, 3, padBankOnScreen.load());
    remapped.clear();
    for (const auto metadata : midi)
    {
        auto m = metadata.getMessage();
        if (const int pad = crate::launchkey::drumPadForMessage (m); pad >= 0)
        {
            m.setNoteNumber (noteForPad (bank * kPadsPerBank + pad));
            m.setChannel (1);
        }
        remapped.addEvent (m, metadata.samplePosition);
    }
    midi.swapWith (remapped);
}

bool MpcProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void MpcProcessor::handleMidi (const juce::MidiMessage& m, const std::shared_ptr<const Kit>& current)
{
    if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& v : voices)
            v.releasing = true;
        return;
    }

    if (levels::isVariation (m))
    {
        if (const int pad = padForNoteNumber[(size_t) m.getNoteNumber()]; pad >= 0)
            pendingVariation[(size_t) pad] = m.getAfterTouchValue();
        return;
    }

    if (! (m.isNoteOn() || m.isNoteOff()))
        return;

    const int pad = padForNoteNumber[(size_t) m.getNoteNumber()];
    if (pad < 0 || current == nullptr)
        return;
    const int variation = m.isNoteOn() ? std::exchange (pendingVariation[(size_t) pad], 0) : 0;

    const auto& sound = (*current)[(size_t) pad];

    if (m.isNoteOff())
    {
        if (sound.mode == PadMode::noteOff)
            for (auto& v : voices)
                if (v.pad == pad)
                    v.releasing = true;
        return;
    }

    if (sound.audio == nullptr || sound.audio->getNumSamples() == 0)
        return;

    for (auto& played : playedPads)
        played.fetch_or ((juce::uint64) 1 << pad);

    // Mono and note-off pads cut off their own earlier hits, like on the MPC, and a pad cuts off
    // the others in its mute group (a closed hat stops the open one).
    for (auto& v : voices)
        if ((v.pad == pad && sound.mode != PadMode::oneShot)
            || (v.pad != pad && sound.muteGroup > 0 && (*current)[(size_t) v.pad].muteGroup == sound.muteGroup))
            v.releasing = true;

    if ((int) voices.size() >= kMaxVoices)
        voices.erase (voices.begin());

    Voice v;
    v.kit = current;
    v.pad = pad;
    v.velocity = fullLevel.load() ? 1.0f : m.getFloatVelocity();
    v.step = sound.rate * kMpcSampleRate / hostRate;
    v.position = sound.reverse ? sound.audio->getNumSamples() - 1 : 0;

    // 16 Levels and the pad's live sound.
    const int type = levels::typeOf (variation), step = levels::stepOf (variation);
    if (const double semis = levels::semitones (type, step); semis > 0.0)
        v.step *= std::pow (2.0, semis / 12.0);

    float cutoff = sound.cutoff;
    if (const float c = levels::cutoff (type, step); c > 0.0f)
        cutoff = juce::jmin (cutoff, c);
    if (cutoff < 19000.0f || sound.resonance > 0.0f)
    {
        const float fc = juce::jlimit (20.0f, (float) (hostRate * 0.45), cutoff);
        v.g = (float) std::tan (juce::MathConstants<double>::pi * fc / hostRate);
        v.k = 2.0f - 1.9f * sound.resonance;
    }

    double decay = sound.decaySeconds;
    if (const double d = levels::decaySeconds (type, step); d > 0.0)
        decay = decay > 0.0 ? juce::jmin (decay, d) : d;
    v.attackLength = (int) (sound.attackSeconds * hostRate);
    v.decayLength = (int) (decay * hostRate);
    if (v.decayLength > 0)
        v.decayFactor = (float) std::pow (0.001, 1.0 / v.decayLength);   // -60 dB by the end

    voices.push_back (std::move (v));
}

void MpcProcessor::render (juce::AudioBuffer<float>& buffer, int start, int count)
{
    if (count <= 0)
        return;

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (auto& v : voices)
    {
        const auto& sound = (*v.kit)[(size_t) v.pad];
        const auto& audio = *sound.audio;
        const int length = audio.getNumSamples();
        const float* l = audio.getReadPointer (0);
        const float* r = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);
        const float gl = sound.gainLeft * v.velocity, gr = sound.gainRight * v.velocity;
        const bool sends = (sound.delaySend > 0 || sound.reverbSend > 0) && start + count <= delaySend.getNumSamples();
        const bool shaped = v.attackLength > 0 || v.decayLength > 0;
        // The filter's coefficients (a 2-pole state-variable low-pass).
        const float a1 = 1.0f / (1.0f + v.g * (v.g + v.k)), a2 = v.g * a1, a3 = v.g * a2;

        for (int i = start; i < start + count; ++i)
        {
            const int index = (int) v.position;
            if (index < 0 || index >= length || v.fade <= 0.0f)
            {
                v.fade = 0.0f;
                break;
            }

            float env = v.fade;
            if (shaped)
            {
                if (v.age < v.attackLength)
                    env *= (float) v.age / (float) v.attackLength;
                else if (v.decayLength > 0)
                {
                    if (v.age >= v.attackLength + v.decayLength)
                    {
                        v.fade = 0.0f;
                        break;
                    }
                    env *= v.decayGain;
                    v.decayGain *= v.decayFactor;
                }
                ++v.age;
            }

            const float frac = (float) (v.position - index);
            const int next = juce::jlimit (0, length - 1, index + (sound.reverse ? -1 : 1));
            float sl = (l[index] + (l[next] - l[index]) * frac) * gl * env;
            float sr = (r[index] + (r[next] - r[index]) * frac) * gr * env;

            if (v.g > 0.0f)
            {
                float* in[2] = { &sl, &sr };
                for (int c = 0; c < 2; ++c)
                {
                    const float v3 = *in[c] - v.s2[c];
                    const float v1 = a1 * v.s1[c] + a2 * v3;
                    const float v2 = v.s2[c] + a2 * v.s1[c] + a3 * v3;
                    v.s1[c] = 2.0f * v1 - v.s1[c];
                    v.s2[c] = 2.0f * v2 - v.s2[c];
                    *in[c] = v2;
                }
            }

            if (sends)
            {
                delaySend.addSample (0, i, sl * sound.delaySend);
                delaySend.addSample (1, i, sr * sound.delaySend);
                reverbSend.addSample (0, i, sl * sound.reverbSend);
                reverbSend.addSample (1, i, sr * sound.reverbSend);
            }

            if (right != nullptr)
            {
                left[i] += sl;
                right[i] += sr;
            }
            else
            {
                left[i] += 0.5f * (sl + sr);
            }

            v.position += sound.reverse ? -v.step : v.step;
            if (v.releasing)
                v.fade -= kReleaseStep;
        }
    }

    voices.erase (std::remove_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.fade <= 0.0f; }),
                  voices.end());
}

void MpcProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    std::shared_ptr<const Kit> current;
    {
        const juce::SpinLock::ScopedTryLockType sl (kitLock);
        if (sl.isLocked())
            current = kit;
    }
    // The message thread is swapping kits right now; keep playing the voices we have.
    if (current == nullptr && ! voices.empty())
        current = voices.back().kit;

    if (stopRequested.exchange (false))
        for (auto& v : voices)
            v.releasing = true;

    if (launchkeyPads.load())
        remapDrumPads (midi);
    clicks.removeNextBlockOfMessages (midi, buffer.getNumSamples());
    applySixteenLevels (midi);

    // The sequencer adds its notes to what you played, so both go down the same path.
    SequencerEngine::Host host;
    bool hasHost = false;
    if (wrapperType != wrapperType_Standalone && sequencer.followHost.load())
        if (auto* playHead = getPlayHead())
            if (const auto info = playHead->getPosition())
            {
                hasHost = true;
                host.playing = info->getIsPlaying();
                host.ppq = info->getPpqPosition().orFallback (0.0);
                host.bpm = info->getBpm().orFallback (120.0);
            }
    int numClicks = 0;
    sequenced.clear();
    sequencer.process (buffer.getNumSamples(), hasHost ? &host : nullptr, midi, sequenced, newClicks, numClicks);
    midi.addEvents (sequenced, 0, buffer.getNumSamples(), 0);
    for (int i = 0; i < numClicks; ++i)
        for (auto& c : clickVoices)
            if (! c.active)
            {
                c = { -newClicks[(size_t) i].offset, newClicks[(size_t) i].accent, true };
                break;
            }

    if (buffer.getNumSamples() <= delaySend.getNumSamples())
    {
        delaySend.clear (0, buffer.getNumSamples());
        reverbSend.clear (0, buffer.getNumSamples());
    }

    int position = 0;
    for (const auto metadata : midi)
    {
        const int at = juce::jlimit (0, buffer.getNumSamples(), metadata.samplePosition);
        render (buffer, position, at - position);
        position = at;
        handleMidi (metadata.getMessage(), current);
    }
    render (buffer, position, buffer.getNumSamples() - position);
    renderSends (buffer, current != nullptr ? current.get() : nullptr);
    renderPreview (buffer);
    renderClicks (buffer);

    midi.clear();
}

void MpcProcessor::applySixteenLevels (juce::MidiBuffer& midi)
{
    const int type = levelsType.load();
    const int bank = juce::jlimit (0, 3, padBankOnScreen.load());
    const int target = juce::jlimit (0, mpc::kPadCount - 1, levelsPad.load());

    auto affects = [&] (const juce::MidiMessage& m, int pad)
    {
        return pad >= 0 && ((m.isNoteOn() && type != levels::off && pad / kPadsPerBank == bank)
                            || (m.isNoteOff() && levelsHeld[(size_t) pad] > 0));
    };

    bool any = false;
    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();
        any |= m.isNoteOnOrOff() && ! levels::isVariation (m) && affects (m, padForNoteNumber[(size_t) m.getNoteNumber()]);
    }
    if (! any)
        return;

    levelsScratch.clear();
    for (const auto metadata : midi)
    {
        auto m = metadata.getMessage();
        const int at = metadata.samplePosition;
        const int pad = m.isNoteOnOrOff() ? padForNoteNumber[(size_t) m.getNoteNumber()] : -1;
        if (affects (m, pad))
        {
            if (m.isNoteOn())
            {
                const int step = pad % kPadsPerBank;
                levelsHeld[(size_t) pad] = target + 1;
                for (auto& played : playedPads)
                    played.fetch_or ((juce::uint64) 1 << pad);   // the pad you hit lights, as well as the sound's
                if (type == levels::velocity)
                    m = juce::MidiMessage::noteOn (m.getChannel(), noteForPad (target), (juce::uint8) levels::velocityFor (step));
                else
                {
                    levelsScratch.addEvent (levels::message (noteForPad (target), levels::pack (type, step)), at);
                    m.setNoteNumber (noteForPad (target));
                }
            }
            else
            {
                m.setNoteNumber (noteForPad (levelsHeld[(size_t) pad] - 1));
                levelsHeld[(size_t) pad] = 0;
            }
        }
        levelsScratch.addEvent (m, at);
    }
    midi.swapWith (levelsScratch);
}

void MpcProcessor::renderSends (juce::AudioBuffer<float>& buffer, const Kit* current)
{
    const int n = buffer.getNumSamples();
    if (n > delaySend.getNumSamples() || delayLine.getNumSamples() < 2)
        return;

    // Keep the delay and reverb running while pads send to them, and for a while after so their
    // tails ring out (8 seconds is longer than any feedback or room here takes to die away).
    if (current != nullptr && current->sends)
        sendTail = (int) (hostRate * 8.0);
    if (sendTail <= 0)
        return;
    sendTail -= n;

    const double beats = current != nullptr ? current->delayBeats : 0.75;
    const float feedback = current != nullptr ? current->delayFeedback : 0.35f;
    const double bpm = juce::jlimit (30.0, 300.0, sequencer.getBpm());
    const int length = delayLine.getNumSamples();
    const int delay = juce::jlimit (1, length - 1, (int) (beats * 60.0 / bpm * hostRate));

    juce::Reverb::Parameters params;
    params.roomSize = current != nullptr ? 0.3f + 0.68f * current->reverbSize : 0.6f;
    params.damping = 0.45f;
    params.wetLevel = 1.0f;
    params.dryLevel = 0.0f;
    params.width = 1.0f;
    reverb.setParameters (params);
    reverb.processStereo (reverbSend.getWritePointer (0), reverbSend.getWritePointer (1), n);

    float* out[2] = { buffer.getWritePointer (0), buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr };
    for (int i = 0; i < n; ++i)
    {
        const int read = (delayWrite - delay + length) % length;
        for (int c = 0; c < 2; ++c)
        {
            float* line = delayLine.getWritePointer (c);
            const float echo = line[read];
            line[delayWrite] = delaySend.getSample (c, i) + echo * feedback;
            const float wet = echo + reverbSend.getSample (c, i) * 0.6f;
            if (out[1] != nullptr)
                out[c][i] += wet;
            else
                out[0][i] += 0.5f * wet;
        }
        delayWrite = (delayWrite + 1) % length;
    }
}

void MpcProcessor::renderClicks (juce::AudioBuffer<float>& buffer)
{
    // A short sine blip: higher on the first beat of the bar.
    const int length = (int) (0.03 * hostRate);
    for (auto& c : clickVoices)
    {
        if (! c.active)
            continue;
        const double freq = c.accent ? 1600.0 : 1100.0;
        const float gain = c.accent ? 0.45f : 0.3f;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const int a = c.age + i;
            if (a < 0)
                continue;
            if (a >= length)
            {
                c.active = false;
                break;
            }
            const float v = gain * (float) (std::sin (juce::MathConstants<double>::twoPi * freq * a / hostRate) * std::exp (-a / (0.006 * hostRate)));
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addSample (ch, i, v);
        }
        c.age += buffer.getNumSamples();
    }
}

juce::AudioProcessorEditor* MpcProcessor::createEditor()
{
    return new MpcView (*this);
}

void MpcProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    dest = project.save();
}

void MpcProcessor::setStateInformation (const void* data, int size)
{
    if (size > 0)
        project.load (data, (size_t) size);
}
