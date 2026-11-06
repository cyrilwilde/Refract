#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace refract;

namespace
{
    // The painted mask is mostly 255s, so it compresses to almost nothing.
    juce::String encodeMask (const spectral::Mask& mask)
    {
        if (spectral::isMaskEmpty (mask))
            return {};
        juce::MemoryOutputStream out;
        {
            juce::GZIPCompressorOutputStream gz (out, 9);
            gz.write (mask.data(), mask.size());
        }   // gz flushes when it goes out of scope
        return out.getMemoryBlock().toBase64Encoding();
    }

    spectral::Mask decodeMask (const juce::String& text)
    {
        auto mask = spectral::makeEmptyMask();
        if (text.isEmpty())
            return mask;

        juce::MemoryBlock block;
        if (! block.fromBase64Encoding (text))
            return mask;

        juce::MemoryInputStream in (block, false);
        juce::GZIPDecompressorInputStream gz (in);
        spectral::Mask decoded (mask.size());
        int total = 0;
        while (total < (int) decoded.size())
        {
            const int got = gz.read (decoded.data() + total, (int) decoded.size() - total);
            if (got <= 0) break;
            total += got;
        }
        return total == (int) decoded.size() ? decoded : mask;
    }
}

//==============================================================================
const ParamTable& RefractAudioProcessor::buildTable (ParamTable& t, juce::AudioProcessorValueTreeState& s, juce::AudioProcessor& p)
{
    t.build (s, p);   // must happen before ParamRefs looks things up in it
    return t;
}

RefractAudioProcessor::RefractAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout()),
      params (apvts, buildTable (table, apvts, *this)),
      liveOffsets ((size_t) table.size())
{
    formatManager.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg, MP3 (on macOS)

    for (auto& layerHeads : playheads)
        for (auto& h : layerHeads)
            h.store (-1.0f);
    for (auto& o : liveOffsets)
        o.store (0.0f);
    for (auto& s : liveSources)
        s.store (0.0f);
    globalOffsets.assign ((size_t) table.size(), 0.0f);

    // Everything the voices share
    shared.table = &table;
    shared.params = &params;
    shared.routing = &routing;
    shared.bank = &bank;
    shared.wavetables = &wavetables;
    shared.playheads = &playheads;
    shared.leadOffsets.assign ((size_t) table.size(), 0.0f);
    synth.shared = &shared;

    synth.addSound (new RefractSound());
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new RefractVoice (i, shared));

    // Starting routes, all at amount 0 - turn an amount up in the matrix to hear them
    setModRouting (0, srcEnv2,     table.indexOf ("flt1_cutoff"));
    setModRouting (1, srcLfo1,     table.indexOf ("voice_pitch"));
    setModRouting (2, srcModWheel, table.indexOf ("flt1_cutoff"));

    // The built-in wavetables take a moment to calculate, so build them in the
    // background; oscillators in Wavetable mode stay silent until they're ready.
    pool.addJob ([this]
    {
        for (int t = 0; t < numBuiltInTables; ++t)
        {
            auto table = makeBuiltInTable (t);
            const juce::ScopedLock sl (publishLock);
            wavetables.exchange (t, std::move (table));
        }
    });

    startTimerHz (2);
}

RefractAudioProcessor::~RefractAudioProcessor()
{
    stopTimer();
    pool.removeAllJobs (true, 10000);
}

//==============================================================================
void RefractAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<RefractVoice*> (synth.getVoice (i)))
            v->prepare (sampleRate, samplesPerBlock);

    for (int k = 0; k < numLfos; ++k)
    {
        globalLfos[(size_t) k].setSampleRate (sampleRate);
        globalLfos[(size_t) k].start (0.0f);
    }

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };

    // Voices render into 6 channels: main L/R, Bus A L/R, Bus B L/R
    maxBlock = juce::jmax (1, samplesPerBlock);
    work.setSize (6, maxBlock, false, true, false);
    for (auto& bus : fxSlots)
        for (auto& slot : bus)
            slot.prepare (sampleRate, maxBlock);

    master.prepare (spec);
    master.setRampDurationSeconds (0.05);
}

bool RefractAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void RefractAudioProcessor::updateHostInfo()
{
    // Tempo and song position from Live, for synced LFOs
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) shared.host.bpm = juce::jlimit (20.0, 999.0, *bpm);
            if (auto ppq = pos->getPpqPosition()) shared.host.ppq = *ppq;
            shared.host.playing = pos->getIsPlaying();
        }
}

void RefractAudioProcessor::computeGlobalModulation (int numSamples)
{
    // Free-running LFOs: voices with Retrigger off lock onto these. In Sync mode
    // with Live playing they follow the song position, so they stay on the beat.
    std::array<float, numModSources> src {};
    for (int k = 0; k < numLfos; ++k)
    {
        const auto& L = params.lfos[(size_t) k];
        auto& lfo = globalLfos[(size_t) k];
        const int mode = (int) L.mode->load();
        const double hz = mode == rateSync ? shared.syncHz ((int) L.sync->load())
                        : mode == rateFreq ? 261.63 * (double) table.value (L.ratio, 0.0f)   // as if playing C3
                                           : (double) table.value (L.rate, 0.0f);
        if (mode == rateSync && shared.host.playing)
            lfo.setPhase (shared.host.ppq / syncBeats ((int) L.sync->load()));
        src[(size_t) (srcLfo1 + k)] = lfo.advance (numSamples, hz, (int) L.shape->load(),
                                                   table.value (L.jitter, 0.0f), table.value (L.smooth, 0.0f))
                                    * table.value (L.depth, 0.0f);
        shared.globalLfoPhase[(size_t) k] = lfo.getPhase();
    }

    // While a note plays, the effects follow that note's modulation; otherwise
    // only the "global" sources (LFOs, wheel, aftertouch, macros) apply.
    if (shared.leadActive)
    {
        src = shared.leadSources;
    }
    else
    {
        src[srcModWheel]   = shared.modWheel;
        src[srcAftertouch] = shared.aftertouch;
        for (int m = 0; m < numMacros; ++m)
            src[(size_t) (srcMacro1 + m)] = params.macros[(size_t) m]->load();
    }

    std::fill (globalOffsets.begin(), globalOffsets.end(), 0.0f);
    for (int slot = 0; slot < numModSlots; ++slot)
    {
        const int s = routing.source[(size_t) slot].load (std::memory_order_relaxed);
        const int t = routing.target[(size_t) slot].load (std::memory_order_relaxed);
        if (s > srcNone && s < numModSources && t >= 0 && t < table.size() && table[t].global)
            globalOffsets[(size_t) t] += src[(size_t) s] * params.modAmounts[(size_t) slot]->load();
    }

    // Publish for the dial rings
    for (int t = 0; t < table.size(); ++t)
    {
        const float live = table[t].global ? globalOffsets[(size_t) t]
                         : shared.leadActive ? shared.leadOffsets[(size_t) t] : 0.0f;
        liveOffsets[(size_t) t].store (live, std::memory_order_relaxed);
    }
    noteActive.store (shared.leadActive, std::memory_order_relaxed);
    for (int s = 0; s < numModSources; ++s)
        liveSources[(size_t) s].store (src[(size_t) s], std::memory_order_relaxed);
}

void RefractAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;   // stops tiny numbers slowing the CPU to a crawl
    updateHostInfo();

    // Hosts occasionally send bigger blocks than they promised; handle those in pieces
    const int total = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || total == 0)
        return;
    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - start);
        juce::MidiBuffer chunkMidi;
        if (n == total)
            chunkMidi.swapWith (midi);
        else
            for (const auto m : midi)
                if (m.samplePosition >= start && m.samplePosition < start + n)
                    chunkMidi.addEvent (m.getMessage(), m.samplePosition - start);

        float* outs[2] = { buffer.getWritePointer (0), buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr };
        juce::AudioBuffer<float> view (outs, buffer.getNumChannels() > 1 ? 2 : 1, start, n);
        renderChunk (view, chunkMidi, n);
        if (n == total)
            chunkMidi.swapWith (midi);
    }
}

void RefractAudioProcessor::renderChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int numSamples)
{
    work.clear (0, numSamples);

    // 1. Voices: MIDI in, audio out on 6 channels (each voice also reports if it's the newest note)
    shared.leadActive = false;
    synth.renderNextBlock (work, midi, 0, numSamples);

    // 2. Modulation for the effects
    computeGlobalModulation (numSamples);

    // 3. The two FX buses: each runs its 3 slots in order, then returns to the main mix
    for (int bus = 0; bus < numBuses; ++bus)
    {
        float* L = work.getWritePointer (2 + bus * 2);
        float* R = work.getWritePointer (3 + bus * 2);
        for (int s = 0; s < numFxSlots; ++s)
        {
            const auto& F = params.fx[(size_t) bus][(size_t) s];
            FxSettings fx;
            fx.type      = (int) F.type->load();
            fx.delayMode = (int) F.delayMode->load();
            fx.sync      = F.sync->load() > 0.5f;
            fx.division  = (int) F.division->load();
            fx.p[0] = gv (F.p1); fx.p[1] = gv (F.p2); fx.p[2] = gv (F.p3); fx.p[3] = gv (F.p4);
            fx.mix  = gv (F.mix);
            fxSlots[(size_t) bus][(size_t) s].process (L, R, numSamples, fx, shared.host.bpm);
        }
        const float ret = gv (params.busReturn[(size_t) bus]);
        work.addFrom (0, 0, L, numSamples, ret);
        work.addFrom (1, 0, R, numSamples, ret);
    }

    // 4. Master volume, then copy to Live's output
    {
        float* chans[2] = { work.getWritePointer (0), work.getWritePointer (1) };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        master.setGainDecibels (gv (params.master));
        master.process (context);
    }
    if (buffer.getNumChannels() >= 2)
    {
        buffer.copyFrom (0, 0, work, 0, 0, numSamples);
        buffer.copyFrom (1, 0, work, 1, 0, numSamples);
    }
    else if (buffer.getNumChannels() == 1)
    {
        buffer.copyFrom (0, 0, work, 0, 0, numSamples);
        buffer.addFrom (0, 0, work, 1, 0, numSamples);
        buffer.applyGain (0.5f);
    }
}

//==============================================================================
//  Mod matrix routing
//==============================================================================
void RefractAudioProcessor::setModRouting (int slot, int source, int target)
{
    if (slot < 0 || slot >= numModSlots) return;
    routing.source[(size_t) slot].store (juce::jlimit (0, numModSources - 1, source));
    routing.target[(size_t) slot].store (target >= 0 && target < table.size() ? target : -1);
    sendChangeMessage();
}

int RefractAudioProcessor::addModulation (int source, int target)
{
    // Prefer a completely empty slot; otherwise one with no amount set
    for (int pass = 0; pass < 2; ++pass)
        for (int slot = 0; slot < numModSlots; ++slot)
        {
            const bool empty = getModSource (slot) == srcNone || getModTarget (slot) < 0;
            const bool unused = pass == 1 && std::abs (params.modAmounts[(size_t) slot]->load()) < 0.0001f;
            if (empty || unused)
            {
                setModRouting (slot, source, target);
                if (auto* p = apvts.getParameter (ids::modAmount (slot)))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 (0.25f));   // a gentle starting amount
                    p->endChangeGesture();
                }
                return slot;
            }
        }
    return -1;
}

void RefractAudioProcessor::clearModSlot (int slot)
{
    setModRouting (slot, srcNone, -1);
    if (auto* p = apvts.getParameter (ids::modAmount (slot)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
        p->endChangeGesture();
    }
}

juce::AudioProcessorEditor* RefractAudioProcessor::createEditor()
{
    return new RefractAudioProcessorEditor (*this);
}

//==============================================================================
//  Sample layers
//==============================================================================
void RefractAudioProcessor::loadSample (int layer, const juce::File& file)
{
    auto& slot = layers[(size_t) layer];
    slot.file = file;
    slot.mask = spectral::makeEmptyMask();   // a new sample starts unedited
    startJob (layer, true);
}

void RefractAudioProcessor::clearSample (int layer)
{
    auto& slot = layers[(size_t) layer];
    slot.file = juce::File();
    slot.mask = spectral::makeEmptyMask();
    ++slot.loadGeneration;
    const int gen = ++slot.renderGeneration;
    {
        const juce::ScopedLock sl (dataLock);
        slot.original = nullptr;
        slot.spectrogram = juce::Image();
        slot.missing = false;
    }
    publish (layer, gen, nullptr, nullptr);
    publishWavetable (layer, gen, nullptr);
    sendChangeMessage();
}

void RefractAudioProcessor::maskEdited (int layer)
{
    startJob (layer, false);
}

refract::SampleDataPtr RefractAudioProcessor::readAudioFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return nullptr;

    const auto maxLength = (juce::int64) (reader->sampleRate * maxSampleSeconds);
    const int length   = (int) juce::jmin (reader->lengthInSamples, maxLength);
    const int channels = (int) juce::jmin (reader->numChannels, (unsigned int) 2);
    if (length <= 0 || channels <= 0)
        return nullptr;

    juce::AudioBuffer<float> audio (channels, length);
    reader->read (&audio, 0, length, 0, true, channels > 1);
    return makeSampleData (std::move (audio), reader->sampleRate);
}

// Pitch detection using the YIN method: compare the sound with delayed copies
// of itself; the delay where it matches best is one cycle of the waveform.
float RefractAudioProcessor::detectPitch (int layer) const
{
    SampleDataPtr data;
    {
        const juce::ScopedLock sl (dataLock);
        data = layers[(size_t) layer].original;
    }
    return data != nullptr ? detectPitchOf (*data) : -1.0f;
}

float RefractAudioProcessor::detectPitchOf (const SampleData& sample)
{
    const auto& audio = sample.audio;
    const double sr = sample.sampleRate;
    const int len = audio.getNumSamples();
    const int tauMin = juce::jmax (2, (int) (sr / 2000.0));   // up to ~2 kHz
    const int tauMax = (int) (sr / 40.0);                    // down to 40 Hz
    const int window = tauMax * 2;
    const int segment = window + tauMax + 2;
    if (len < segment)
        return -1.0f;

    // Use the loudest of a few candidate spots (skips silence and sharp attacks)
    int bestStart = 0;
    float bestRms = 0.0f;
    for (float frac : { 0.1f, 0.25f, 0.4f, 0.55f })
    {
        const int s = juce::jmin ((int) (frac * (float) len), len - segment);
        float sum = 0.0f;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            sum += audio.getRMSLevel (ch, s, segment);
        if (sum > bestRms) { bestRms = sum; bestStart = s; }
    }
    if (bestRms < 1.0e-4f)
        return -1.0f;

    std::vector<float> x ((size_t) segment);
    for (int i = 0; i < segment; ++i)
    {
        float v = 0.0f;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            v += audio.getSample (ch, bestStart + i);
        x[(size_t) i] = v;
    }

    // Difference function, then "cumulative mean normalised difference"
    std::vector<float> d ((size_t) tauMax + 2, 1.0f);
    float runningSum = 0.0f;
    for (int tau = 1; tau <= tauMax + 1; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < window; ++j)
        {
            const float diff = x[(size_t) j] - x[(size_t) (j + tau)];
            sum += diff * diff;
        }
        runningSum += sum;
        d[(size_t) tau] = runningSum > 0.0f ? sum * (float) tau / runningSum : 1.0f;
    }

    // First dip below the threshold, then walk to the bottom of that dip
    int tau = -1;
    for (int t = tauMin; t <= tauMax; ++t)
        if (d[(size_t) t] < 0.15f)
        {
            while (t + 1 <= tauMax && d[(size_t) (t + 1)] < d[(size_t) t]) ++t;
            tau = t;
            break;
        }
    if (tau < 0)
    {
        // No confident dip: use the deepest one, but only if it's reasonably clear
        tau = tauMin;
        for (int t = tauMin; t <= tauMax; ++t)
            if (d[(size_t) t] < d[(size_t) tau]) tau = t;
        if (d[(size_t) tau] > 0.4f)
            return -1.0f;
    }

    // Parabolic interpolation for sub-sample accuracy
    const float a = d[(size_t) (tau - 1)], b = d[(size_t) tau], c = d[(size_t) (tau + 1)];
    const float denom = a - 2.0f * b + c;
    const float exactTau = (float) tau + (std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f);

    const double freq = sr / (double) exactTau;
    return (float) (69.0 + 12.0 * std::log2 (freq / 440.0));
}

void RefractAudioProcessor::startJob (int layer, bool reloadFile)
{
    auto& slot = layers[(size_t) layer];

    // Take copies of everything the job needs, so the UI can keep editing
    // the live mask while the job works on its own snapshot.
    const int loadGen   = reloadFile ? ++slot.loadGeneration : slot.loadGeneration.load();
    const int renderGen = ++slot.renderGeneration;
    const auto file = slot.file;
    auto mask = slot.mask;

    SampleDataPtr original;
    {
        const juce::ScopedLock sl (dataLock);
        original = slot.original;
    }
    if (! reloadFile && original == nullptr)
        return;   // nothing loaded yet - nothing to render

    ++slot.pendingJobs;
    sendChangeMessage();

    pool.addJob ([this, layer, loadGen, renderGen, file, reloadFile,
                  mask = std::move (mask), original]() mutable
    {
        auto& s = layers[(size_t) layer];

        // Step 1 (new files only): read from disk and draw the spectrogram.
        if (reloadFile)
        {
            original = readAudioFile (file);
            auto picture = original != nullptr ? spectral::renderSpectrogram (original->audio, original->sampleRate)
                                               : juce::Image();

            const juce::ScopedLock sl (dataLock);
            if (loadGen == s.loadGeneration.load())
            {
                s.original = original;
                s.spectrogram = picture;
                s.missing = (original == nullptr);
            }
        }

        // Step 2: apply the painted mask (skipped entirely if nothing is painted).
        SampleDataPtr playback;
        bool aborted = false;
        if (original != nullptr)
        {
            if (spectral::isMaskEmpty (mask))
            {
                playback = original;
            }
            else
            {
                auto shouldAbort = [&s, renderGen] { return renderGen != s.renderGeneration.load(); };
                auto edited = spectral::applyMask (original->audio, original->sampleRate, mask, shouldAbort);
                if (edited.getNumSamples() == 0)
                {
                    aborted = true;
                }
                else
                {
                    playback = makeSampleData (std::move (edited), original->sampleRate);
                }
            }
        }

        // Step 3: hand the result to the audio thread (and its overview to the UI).
        if (! aborted)
        {
            publish (layer, renderGen, playback, playback != nullptr ? makePeaks (playback->audio) : nullptr);

            // A wavetable made from this sound, for the oscillators' "From Layer" tables.
            // If the pitch is known, each frame is exactly one cycle of it.
            WavetablePtr wt;
            if (playback != nullptr)
            {
                const float midi = detectPitchOf (*playback);
                const double period = midi > 0.0f ? playback->sampleRate / (440.0 * std::pow (2.0, (midi - 69.0) / 12.0)) : 0.0;
                wt = makeTableFromSample (*playback, period);
            }
            publishWavetable (layer, renderGen, std::move (wt));
        }

        --s.pendingJobs;
        sendChangeMessage();   // safe from any thread - the UI is updated later on the message thread
    });
}

void RefractAudioProcessor::publish (int layer, int renderGen, SampleDataPtr playback, WaveformPeaksPtr peaks)
{
    const juce::ScopedLock sl (publishLock);
    if (renderGen != layers[(size_t) layer].renderGeneration.load())
        return;   // a newer edit is on its way; don't overwrite it with this older one

    {
        const juce::ScopedLock dl (dataLock);
        layers[(size_t) layer].peaks = std::move (peaks);
    }

    // Swap in the new sample. Voices already playing keep the old one until
    // their note ends; we park it in the graveyard rather than free it here.
    if (auto old = bank.exchange (layer, std::move (playback)))
        graveyard.push_back (std::move (old));
}

void RefractAudioProcessor::publishWavetable (int layer, int renderGen, WavetablePtr table)
{
    const juce::ScopedLock sl (publishLock);
    if (renderGen != layers[(size_t) layer].renderGeneration.load())
        return;
    if (auto old = wavetables.exchange (numBuiltInTables + layer, std::move (table)))
        wavetableGraveyard.push_back (std::move (old));
}

void RefractAudioProcessor::timerCallback()
{
    // An old sample is only referenced by the graveyard once no voice uses it
    // (use_count == 1). Freeing memory here, on the message thread, keeps the
    // audio thread from ever doing a slow deallocation.
    const juce::ScopedLock sl (publishLock);
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [] (const SampleDataPtr& p) { return p.use_count() <= 1; }),
                     graveyard.end());
    wavetableGraveyard.erase (std::remove_if (wavetableGraveyard.begin(), wavetableGraveyard.end(),
                                              [] (const WavetablePtr& p) { return p.use_count() <= 1; }),
                              wavetableGraveyard.end());
}

RefractAudioProcessor::LayerDisplayInfo RefractAudioProcessor::getDisplayInfo (int layer) const
{
    const auto& slot = layers[(size_t) layer];
    LayerDisplayInfo info;
    info.fileName = slot.file.getFileName();
    info.busy = slot.pendingJobs.load() > 0;

    const juce::ScopedLock sl (dataLock);
    info.missing = slot.missing;
    info.spectrogram = slot.spectrogram;
    info.peaks = slot.peaks;
    info.playback = bank.get (layer);
    if (slot.original != nullptr)
        info.lengthSeconds = slot.original->audio.getNumSamples() / slot.original->sampleRate;
    return info;
}

//==============================================================================
//  State: parameters + which file each layer uses + its painted mask
//==============================================================================
juce::ValueTree RefractAudioProcessor::createStateTree()
{
    juce::ValueTree root ("RefractState");
    root.setProperty ("preset", presetName, nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    juce::ValueTree layerTree ("Layers");
    for (int i = 0; i < numLayers; ++i)
    {
        const auto& slot = layers[(size_t) i];
        juce::ValueTree l ("Layer");
        l.setProperty ("index", i, nullptr);
        l.setProperty ("file", slot.file.getFullPathName(), nullptr);
        l.setProperty ("mask", encodeMask (slot.mask), nullptr);
        layerTree.appendChild (l, nullptr);
    }
    root.appendChild (layerTree, nullptr);

    // Mod routing is saved by name (source key + parameter ID), not by number,
    // so it survives future versions that add sources or parameters.
    juce::ValueTree modTree ("ModRouting");
    const auto keys = modSourceKeys();
    for (int s = 0; s < numModSlots; ++s)
    {
        juce::ValueTree slot ("Slot");
        slot.setProperty ("index", s, nullptr);
        slot.setProperty ("source", keys[getModSource (s)], nullptr);
        const int t = getModTarget (s);
        slot.setProperty ("target", t >= 0 ? table[t].id : juce::String(), nullptr);
        modTree.appendChild (slot, nullptr);
    }
    root.appendChild (modTree, nullptr);
    return root;
}

void RefractAudioProcessor::restoreStateTree (const juce::ValueTree& root)
{
    if (! root.hasType ("RefractState"))
        return;

    presetName = root.getProperty ("preset", "Init").toString();

    const auto paramTree = root.getChildWithName (apvts.state.getType());
    if (paramTree.isValid())
        apvts.replaceState (paramTree);

    const auto modTree = root.getChildWithName ("ModRouting");
    if (modTree.isValid())
    {
        const auto keys = modSourceKeys();
        for (int s = 0; s < numModSlots; ++s)
            setModRouting (s, srcNone, -1);
        for (const auto slot : modTree)
        {
            const int s = slot.getProperty ("index", -1);
            if (s >= 0 && s < numModSlots)
                setModRouting (s, juce::jmax (0, keys.indexOf (slot.getProperty ("source").toString())),
                               table.indexOf (slot.getProperty ("target").toString()));
        }
    }

    // Layers not mentioned (or with no file) are cleared, so a preset never
    // inherits samples from whatever was loaded before.
    std::array<bool, numLayers> seen {};
    for (const auto l : root.getChildWithName ("Layers"))
    {
        const int i = l.getProperty ("index", -1);
        if (i < 0 || i >= numLayers)
            continue;
        seen[(size_t) i] = true;

        const auto path = l.getProperty ("file").toString();
        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
        {
            clearSample (i);
            continue;
        }

        auto& slot = layers[(size_t) i];
        slot.file = juce::File (path);
        slot.mask = decodeMask (l.getProperty ("mask").toString());
        startJob (i, true);
    }
    for (int i = 0; i < numLayers; ++i)
        if (! seen[(size_t) i])
            clearSample (i);

    sendChangeMessage();
}

void RefractAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void RefractAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        restoreStateTree (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
//  Presets
//==============================================================================
juce::File RefractAudioProcessor::getPresetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Refract").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::File RefractAudioProcessor::getSampleFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Refract").getChildFile ("Samples");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> RefractAudioProcessor::getPresetList()
{
    auto files = getPresetFolder().findChildFiles (juce::File::findFiles, true, "*" + juce::String (presetExtension));
    files.sort();   // juce::File sorts by full path, so sub-folders group together
    return files;
}

bool RefractAudioProcessor::savePreset (const juce::File& chosen)
{
    const auto file = chosen.withFileExtension (presetExtension);

    // Copy each layer's sample into Music/Refract/Samples (once), so presets
    // keep working even if the original files are moved or deleted.
    const auto sampleDir = getSampleFolder();
    for (int i = 0; i < numLayers; ++i)
    {
        auto& slot = layers[(size_t) i];
        if (! slot.file.existsAsFile() || slot.file.isAChildOf (sampleDir))
            continue;
        auto target = sampleDir.getChildFile (slot.file.getFileName());
        if (target.existsAsFile() && target.getSize() != slot.file.getSize())
            target = target.getNonexistentSibling();
        if (target.existsAsFile() || slot.file.copyFileTo (target))
            slot.file = target;
    }

    presetName = file.getFileNameWithoutExtension();
    currentPresetFile = file;
    const auto xml = createStateTree().createXml();
    const bool ok = xml != nullptr && xml->writeTo (file);
    sendChangeMessage();
    return ok;
}

bool RefractAudioProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr)
        return false;
    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType ("RefractState"))
        return false;

    restoreStateTree (tree);
    presetName = file.getFileNameWithoutExtension();
    currentPresetFile = file;
    sendChangeMessage();
    return true;
}

void RefractAudioProcessor::initialiseState()
{
    // Every dial back to its default, samples cleared, matrix back to its starting routes
    for (auto* p : getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());
    for (int i = 0; i < numLayers; ++i)
        clearSample (i);
    for (int s = 0; s < numModSlots; ++s)
        setModRouting (s, srcNone, -1);
    setModRouting (0, srcEnv2,     table.indexOf ("flt1_cutoff"));
    setModRouting (1, srcLfo1,     table.indexOf ("voice_pitch"));
    setModRouting (2, srcModWheel, table.indexOf ("flt1_cutoff"));

    presetName = "Init";
    currentPresetFile = juce::File();
    sendChangeMessage();
}

//==============================================================================
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RefractAudioProcessor();
}
