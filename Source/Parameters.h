#pragma once
//
// Parameters.h
// Every knob the host can see and automate is declared here, plus a few
// constants shared by the engine and the UI.
//
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cmath>

namespace refract
{
    constexpr int numLayers   = 3;    // sample layers
    constexpr int numModSlots = 4;    // rows in the mod matrix
    constexpr int numVoices   = 16;   // polyphony

    // The spectral mask is a fixed grid, independent of sample length:
    // maskWidth cells across time, maskHeight cells across frequency.
    // Frequency rows are log-spaced (like a piano), 20 Hz at row 0, 20 kHz at the top.
    constexpr int   maskWidth   = 512;
    constexpr int   maskHeight  = 256;
    constexpr float minMaskFreq = 20.0f;
    constexpr float maxMaskFreq = 20000.0f;

    constexpr double maxSampleSeconds = 30.0;   // longer files are trimmed on load

    // 0..1 position on the log axis  <->  frequency in Hz
    inline float logPosToFreq (float p) { return minMaskFreq * std::pow (maxMaskFreq / minMaskFreq, p); }
    inline float freqToLogPos (float f)
    {
        if (f <= minMaskFreq) return 0.0f;
        return juce::jlimit (0.0f, 1.0f, std::log (f / minMaskFreq) / std::log (maxMaskFreq / minMaskFreq));
    }

    // Modulation sources and destinations. The order must match the name lists below.
    enum ModSource { srcNone = 0, srcModEnv, srcLfo1, srcLfo2, srcVelocity, srcModWheel, srcKeyTrack, numModSources };
    enum ModDest   { dstNone = 0, dstPitch, dstCutoff, dstResonance, dstLayer1, dstLayer2, dstLayer3,
                     dstSub, dstPan, dstAmp, numModDests };

    inline juce::StringArray modSourceNames() { return { "None", "Mod Env", "LFO 1", "LFO 2", "Velocity", "Mod Wheel", "Key Track" }; }
    inline juce::StringArray modDestNames()   { return { "None", "Pitch", "Cutoff", "Resonance", "Layer 1 Level",
                                                         "Layer 2 Level", "Layer 3 Level", "Sub Level", "Pan", "Amp" }; }
    inline juce::StringArray lfoShapeNames()  { return { "Sine", "Triangle", "Saw", "Square", "S&H" }; }
    inline juce::StringArray filterTypeNames(){ return { "Low Pass", "Band Pass", "High Pass" }; }
    inline juce::StringArray subWaveNames()   { return { "Sine", "Triangle", "Square" }; }
    inline juce::StringArray subOctaveNames() { return { "-2 Oct", "-1 Oct", "0 Oct" }; }

    // Parameter IDs. Layer IDs look like "l1_level", mod slots like "mod2_amt".
    namespace ids
    {
        inline juce::String layer (int index, const char* name) { return "l" + juce::String (index + 1) + "_" + name; }
        inline juce::String mod   (int slot,  const char* name) { return "mod" + juce::String (slot + 1) + "_" + name; }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Fast, lock-free pointers to each parameter's current value, so the audio
    // thread can read knobs without touching the ValueTree.
    struct ParamRefs
    {
        using P = std::atomic<float>*;

        struct Layer  { P on, level, coarse, fine, pan, start, loop; };
        struct Env    { P attack, decay, sustain, release; };
        struct Lfo    { P rate, shape; };
        struct Slot   { P source, dest, amount; };

        std::array<Layer, numLayers> layers;
        P subLevel, subOctave, subWave;
        Env ampEnv, modEnv;
        std::array<Lfo, 2> lfos;
        P filterType, cutoff, resonance;
        std::array<Slot, numModSlots> mods;
        P delayTime, delayFeedback, delayMix;
        P reverbSize, reverbDamping, reverbMix;
        P masterGain;

        explicit ParamRefs (juce::AudioProcessorValueTreeState& state);
    };
}
