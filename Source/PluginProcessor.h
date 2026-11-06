#pragma once
//
// PluginProcessor.h
// The heart of the plugin. The host (Ableton) talks to this class:
//   - processBlock() is called on the real-time audio thread, hundreds of
//     times a second, and must never block, allocate or wait.
//   - everything else (loading files, rendering edits, saving state) happens
//     on the message thread or a background thread.
//
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "SampleData.h"
#include "SpectralProcessor.h"
#include "SynthVoice.h"

class RefractAudioProcessor : public juce::AudioProcessor,
                              public juce::ChangeBroadcaster,   // tells the UI "something changed"
                              private juce::Timer
{
public:
    RefractAudioProcessor();
    ~RefractAudioProcessor() override;

    // --- Audio ---------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    // --- Plugin info ---------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                  { return true; }
    const juce::String getName() const override      { return JucePlugin_Name; }
    bool acceptsMidi() const override                { return true; }
    bool producesMidi() const override               { return false; }
    bool isMidiEffect() const override               { return false; }
    double getTailLengthSeconds() const override     { return 4.0; }
    int getNumPrograms() override                    { return 1; }
    int getCurrentProgram() override                 { return 0; }
    void setCurrentProgram (int) override            {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // --- Saving / loading with your Ableton set ------------------------------
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Sample layers (call these from the UI / message thread only) --------
    void loadSample (int layer, const juce::File& file);
    void clearSample (int layer);
    refract::spectral::Mask& getMask (int layer)   { return layers[(size_t) layer].mask; }
    void maskEdited (int layer);                    // re-render after painting

    struct LayerDisplayInfo
    {
        juce::String fileName;
        double lengthSeconds = 0.0;
        bool missing = false;    // file saved in the set can't be found any more
        bool busy = false;       // loading or rendering in the background
        juce::Image spectrogram;
        refract::SampleDataPtr playback;        // the audio you actually hear (after painting)
        refract::WaveformPeaksPtr peaks;        // zoomed-out overview of it
    };
    LayerDisplayInfo getDisplayInfo (int layer) const;

    // Where each voice currently is in a layer's sample (0..1, or -1 if silent)
    float getPlayhead (int layer, int voice) const { return playheads[(size_t) layer][(size_t) voice].load (std::memory_order_relaxed); }

    // Estimates the pitch of a layer's sample as a (fractional) MIDI note,
    // or returns -1 if no clear pitch was found.
    float detectPitch (int layer) const;

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formatManager;

private:
    struct LayerSlot
    {
        // Message thread only:
        juce::File file;
        refract::spectral::Mask mask = refract::spectral::makeEmptyMask();

        // Written by background jobs, read by the UI - guarded by dataLock:
        refract::SampleDataPtr original;
        refract::WaveformPeaksPtr peaks;
        juce::Image spectrogram;
        bool missing = false;

        // Each new request bumps a counter; a job whose number is out of date
        // knows a newer request replaced it and quietly gives up.
        std::atomic<int> loadGeneration { 0 };
        std::atomic<int> renderGeneration { 0 };
        std::atomic<int> pendingJobs { 0 };
    };

    void startJob (int layer, bool reloadFile);
    void publish (int layer, int renderGeneration, refract::SampleDataPtr playback, refract::WaveformPeaksPtr peaks);
    refract::SampleDataPtr readAudioFile (const juce::File& file);
    void timerCallback() override;   // frees replaced samples, off the audio thread
    void processDelay (juce::AudioBuffer<float>& buffer);

    refract::ParamRefs params;
    refract::PlaybackBank bank;
    refract::PlayheadArray playheads;
    refract::RefractSynth synth;
    std::array<LayerSlot, refract::numLayers> layers;

    juce::CriticalSection dataLock, publishLock;
    std::vector<refract::SampleDataPtr> graveyard;   // old samples waiting to be freed

    // Effects
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay { 4 };
    juce::SmoothedValue<float> delaySamples;
    juce::dsp::Reverb reverb;
    juce::dsp::Gain<float> master;
    double currentSampleRate = 44100.0;

    // Declared last so it is destroyed first: its destructor waits for any
    // running jobs, which still need everything above to exist.
    juce::ThreadPool pool;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RefractAudioProcessor)
};
