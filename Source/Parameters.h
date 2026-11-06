#pragma once
//
// Parameters.h
// Every knob the host can see and automate is declared here, plus the
// modulation "plumbing" shared by the engine and the UI.
//
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace refract
{
    constexpr int numLayers   = 3;    // sample layers
    constexpr int numVoices   = 16;   // polyphony
    constexpr int numModSlots = 16;   // rows in the mod matrix
    constexpr int numLfos     = 4;
    constexpr int numEnvs     = 4;    // envelope 1 is the amp envelope, 2-4 are free
    constexpr int numMacros   = 4;

    // The spectral mask is a fixed grid, independent of sample length:
    // maskWidth cells across time, maskHeight cells across frequency.
    // Frequency rows are log-spaced (like a piano), 20 Hz at row 0, 20 kHz at the top.
    constexpr int   maskWidth   = 1024;
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

    constexpr int numOscs     = 2;
    constexpr int maxUnison   = 8;    // oscillator unison voices
    constexpr int maxLayerUnison = 4; // sample layer unison copies

    // Oscillators
    enum OscMode   { oscAnalog = 0, oscWavetable };
    inline juce::StringArray oscModeNames()   { return { "Analog", "Wavetable" }; }

    // Voice modes and glide
    enum VoiceMode { voicePoly = 0, voiceMono, voiceLegato };
    enum GlideMode { glideOff = 0, glideAlways, glideLegato };
    inline juce::StringArray voiceModeNames() { return { "Poly", "Mono", "Legato" }; }
    inline juce::StringArray glideModeNames() { return { "Off", "Always", "Legato Only" }; }

    // How a layer's loop behaves (same idea as Ableton's Sampler)
    enum LoopMode { loopOff = 0, loopForward, loopPingPong, loopRelease };
    inline juce::StringArray loopModeNames() { return { "Off", "Forward", "Ping-Pong", "Fwd + Release" }; }

    //==========================================================================
    //  LFOs
    //==========================================================================
    enum LfoShape    { lfoSine = 0, lfoTriangle, lfoSawUp, lfoSawDown, lfoSquare, lfoRandom };
    enum LfoRateMode { rateHz = 0, rateSync, rateFreq };
    inline juce::StringArray lfoShapeNames()    { return { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random" }; }
    inline juce::StringArray lfoRateModeNames() { return { "Hz", "Sync", "Freq" }; }

    // Tempo-synced lengths, and how many beats (quarter notes) each one lasts
    inline juce::StringArray syncNames()
    {
        return { "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/2 D", "1/2 T", "1/4", "1/4 D", "1/4 T",
                 "1/8", "1/8 D", "1/8 T", "1/16", "1/16 D", "1/16 T", "1/32" };
    }
    inline double syncBeats (int index)
    {
        static const double beats[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 3.0, 4.0 / 3.0, 1.0, 1.5, 2.0 / 3.0,
                                        0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125 };
        return beats[juce::jlimit (0, 16, index)];
    }

    //==========================================================================
    //  Modulation sources
    //==========================================================================
    enum ModSource
    {
        srcNone = 0,
        srcEnv1, srcEnv2, srcEnv3, srcEnv4,          // srcEnv1 = amp envelope
        srcLfo1, srcLfo2, srcLfo3, srcLfo4,
        srcVelocity, srcModWheel, srcAftertouch, srcPitchBend, srcKeyTrack, srcNoteRandom,
        srcMacro1, srcMacro2, srcMacro3, srcMacro4,
        numModSources
    };
    inline juce::StringArray modSourceNames()
    {
        return { "None", "Amp Env", "Env 2", "Env 3", "Env 4", "LFO 1", "LFO 2", "LFO 3", "LFO 4",
                 "Velocity", "Mod Wheel", "Aftertouch", "Pitch Bend", "Key Track", "Note Random",
                 "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    }
    // Stable names used when saving, so adding sources later never breaks old sets
    inline juce::StringArray modSourceKeys()
    {
        return { "none", "env1", "env2", "env3", "env4", "lfo1", "lfo2", "lfo3", "lfo4",
                 "velocity", "wheel", "aftertouch", "bend", "key", "random",
                 "macro1", "macro2", "macro3", "macro4" };
    }
    // Bipolar sources swing both ways (-1..+1); the rest go 0..+1
    inline bool isBipolarSource (int s)
    {
        return (s >= srcLfo1 && s <= srcLfo4) || s == srcPitchBend || s == srcKeyTrack || s == srcNoteRandom;
    }

    // Parameter IDs. Layer IDs look like "l1_level", LFO IDs "lfo2_rate", envelopes "env3_a".
    namespace ids
    {
        inline juce::String layer (int index, const char* name) { return "l" + juce::String (index + 1) + "_" + name; }
        inline juce::String lfo   (int index, const char* name) { return "lfo" + juce::String (index + 1) + "_" + name; }
        inline juce::String env   (int index, const char* stage) { return (index == 0 ? juce::String ("amp") : "env" + juce::String (index + 1)) + "_" + stage; }
        inline juce::String modAmount (int slot) { return "mod" + juce::String (slot + 1) + "_amt"; }
        inline juce::String macro (int index) { return "macro" + juce::String (index + 1); }
        inline juce::String osc   (int index, const char* name) { return "osc" + juce::String (index + 1) + "_" + name; }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==========================================================================
    //  ParamTable: the list of everything that can be modulated
    //==========================================================================
    // Every continuous (float or whole-number) parameter becomes a modulation
    // target, except the mod amounts and macros themselves. Modulation works on
    // the knob's 0..1 position, so an amount of +0.5 always means "push this
    // knob half of its travel", whatever the parameter's units are.
    class ParamTable
    {
    public:
        struct Target
        {
            juce::String id, name, group;
            std::atomic<float>* raw = nullptr;
            juce::NormalisableRange<float> range;
            juce::RangedAudioParameter* param = nullptr;
            bool global = false;   // affects the whole plugin (effects), not one voice
        };

        void build (juce::AudioProcessorValueTreeState& state, juce::AudioProcessor& processor);

        int size() const                     { return (int) targets.size(); }
        int indexOf (const juce::String& id) const;
        const Target& operator[] (int i) const { return targets[(size_t) i]; }

        // The parameter's value after adding a modulation offset (in knob travel, -1..1)
        float value (int i, float offset) const
        {
            const auto& t = targets[(size_t) i];
            const float base = t.raw->load (std::memory_order_relaxed);
            if (offset == 0.0f)
                return base;
            return t.range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, t.range.convertTo0to1 (base) + offset));
        }

    private:
        std::vector<Target> targets;
    };

    // Indices into the ParamTable (for modulatable parameters), or raw pointers
    // (for switches and menus, which aren't modulated). The audio thread uses
    // these to find every value quickly.
    struct ParamRefs
    {
        using P = std::atomic<float>*;   // read directly
        using I = int;                   // index into ParamTable

        struct Layer
        {
            P on, loopMode, reverse;
            I level, pan, root, coarse, fine, start, end, loopStart, loopEnd, crossfade;
            I unison, detune, spread;
        };
        struct Osc
        {
            P on, mode, table;
            I wave, pulseWidth, position, octave, semi, fine, level, pan, unison, detune, spread, randomPhase;
        };
        struct Env { I attack, decay, sustain, release; };
        struct Lfo { P shape, mode, sync, retrig; I rate, ratio, depth, phase, jitter, smooth; };

        std::array<Layer, numLayers> layers;
        P subOctave, subWave;  I subLevel;
        std::array<Osc, numOscs> oscs;
        P osc2Sync;            I osc2Fm;
        I noiseLevel, noiseColour;
        P voiceMode, glideMode; I glideTime;
        std::array<Env, numEnvs> envs;
        std::array<Lfo, numLfos> lfos;
        P filterType;          I cutoff, resonance;
        I pitch, pan, voiceLevel;
        std::array<P, numModSlots> modAmounts;
        std::array<P, numMacros> macros;
        I delayTime, delayFeedback, delayMix, reverbSize, reverbDamping, reverbMix, master;

        ParamRefs (juce::AudioProcessorValueTreeState& state, const ParamTable& table);
    };

    // Which source feeds which target, for each mod matrix row. Set from the UI,
    // read by the audio thread - plain atomics, so no locking is needed.
    struct ModRouting
    {
        std::array<std::atomic<int>, numModSlots> source;   // ModSource
        std::array<std::atomic<int>, numModSlots> target;   // ParamTable index, -1 = none
        ModRouting() { for (int i = 0; i < numModSlots; ++i) { source[(size_t) i] = srcNone; target[(size_t) i] = -1; } }
    };
}
