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
RefractAudioProcessor::RefractAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout()),
      params (apvts)
{
    formatManager.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg, MP3 (on macOS)

    synth.addSound (new RefractSound());
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new RefractVoice (params, bank, synth.modWheel));

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

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };

    delay.setMaximumDelayInSamples ((int) (sampleRate * 2.1));
    delay.prepare (spec);
    delaySamples.reset (sampleRate, 0.25);   // glide delay-time changes over 250 ms
    delaySamples.setCurrentAndTargetValue (params.delayTime->load() * 0.001f * (float) sampleRate);

    reverb.prepare (spec);
    reverb.reset();

    master.prepare (spec);
    master.setRampDurationSeconds (0.05);
}

bool RefractAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void RefractAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;   // stops tiny numbers slowing the CPU to a crawl
    buffer.clear();

    // 1. Voices: MIDI in, audio out
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    // 2. Delay
    processDelay (buffer);

    // 3. Reverb
    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = params.reverbSize->load();
    rp.damping  = params.reverbDamping->load();
    rp.wetLevel = params.reverbMix->load();
    rp.dryLevel = 1.0f - params.reverbMix->load() * 0.5f;
    rp.width    = 1.0f;
    reverb.setParameters (rp);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    reverb.process (context);

    // 4. Master volume
    master.setGainDecibels (params.masterGain->load());
    master.process (context);
}

void RefractAudioProcessor::processDelay (juce::AudioBuffer<float>& buffer)
{
    const float feedback = params.delayFeedback->load();
    const float mix      = params.delayMix->load();
    delaySamples.setTargetValue (juce::jlimit (1.0f, (float) currentSampleRate * 2.0f,
                                               params.delayTime->load() * 0.001f * (float) currentSampleRate));

    const int channels = juce::jmin (buffer.getNumChannels(), 2);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float d = delaySamples.getNextValue();
        for (int ch = 0; ch < channels; ++ch)
        {
            const float dry = buffer.getSample (ch, i);
            const float wet = delay.popSample (ch, d);
            delay.pushSample (ch, dry + wet * feedback);   // feed some of the echo back in
            buffer.setSample (ch, i, dry + wet * mix);
        }
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
    publish (layer, gen, nullptr);
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

    auto data = std::make_shared<SampleData>();
    data->sampleRate = reader->sampleRate;
    data->audio.setSize (channels, length);
    reader->read (&data->audio, 0, length, 0, true, channels > 1);
    return data;
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
                    auto data = std::make_shared<SampleData>();
                    data->audio = std::move (edited);
                    data->sampleRate = original->sampleRate;
                    playback = data;
                }
            }
        }

        // Step 3: hand the result to the audio thread.
        if (! aborted)
            publish (layer, renderGen, playback);

        --s.pendingJobs;
        sendChangeMessage();   // safe from any thread - the UI is updated later on the message thread
    });
}

void RefractAudioProcessor::publish (int layer, int renderGen, SampleDataPtr playback)
{
    const juce::ScopedLock sl (publishLock);
    if (renderGen != layers[(size_t) layer].renderGeneration.load())
        return;   // a newer edit is on its way; don't overwrite it with this older one

    // Swap in the new sample. Voices already playing keep the old one until
    // their note ends; we park it in the graveyard rather than free it here.
    if (auto old = bank.exchange (layer, std::move (playback)))
        graveyard.push_back (std::move (old));
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
    if (slot.original != nullptr)
        info.lengthSeconds = slot.original->audio.getNumSamples() / slot.original->sampleRate;
    return info;
}

//==============================================================================
//  State: parameters + which file each layer uses + its painted mask
//==============================================================================
void RefractAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("RefractState");
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

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void RefractAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("RefractState"))
        return;

    const auto paramTree = root.getChildWithName (apvts.state.getType());
    if (paramTree.isValid())
        apvts.replaceState (paramTree);

    for (const auto l : root.getChildWithName ("Layers"))
    {
        const int i = l.getProperty ("index", -1);
        if (i < 0 || i >= numLayers)
            continue;

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
}

//==============================================================================
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RefractAudioProcessor();
}
