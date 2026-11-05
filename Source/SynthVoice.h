#pragma once
//
// SynthVoice.h
// One voice = one playing note. The synth owns 16 of these and hands each new
// note to a free one. Each voice plays the 3 sample layers + sub oscillator,
// runs its own envelopes, LFOs and filter, and applies the mod matrix.
//
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "SampleData.h"

namespace refract
{
    // Our one "sound": it plays on every note and every MIDI channel.
    struct RefractSound : public juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    // Synthesiser that also remembers the mod wheel, so notes started *after*
    // the wheel moved still know where it is.
    class RefractSynth : public juce::Synthesiser
    {
    public:
        float modWheel = 0.0f;

        void handleController (int midiChannel, int controllerNumber, int controllerValue) override
        {
            if (controllerNumber == 1)
                modWheel = (float) controllerValue / 127.0f;
            juce::Synthesiser::handleController (midiChannel, controllerNumber, controllerValue);
        }
    };

    // Low-frequency oscillator. Evaluated once per control block (see voice).
    class Lfo
    {
    public:
        void setSampleRate (double sr) { sampleRate = sr; }
        void reset()                   { phase = 0.0f; held = random.nextFloat() * 2.0f - 1.0f; }

        // Returns the value now, then moves forward by numSamples.
        float advance (int numSamples, float rateHz, int shape)
        {
            float v = 0.0f;
            switch (shape)
            {
                case 0:  v = std::sin (juce::MathConstants<float>::twoPi * phase); break;   // sine
                case 1:  v = 1.0f - 4.0f * std::abs (phase - 0.5f);                 break;   // triangle
                case 2:  v = 2.0f * phase - 1.0f;                                   break;   // saw
                case 3:  v = phase < 0.5f ? 1.0f : -1.0f;                           break;   // square
                default: v = held;                                                  break;   // sample & hold
            }

            phase += rateHz * (float) numSamples / (float) sampleRate;
            if (phase >= 1.0f)
            {
                phase -= std::floor (phase);
                held = random.nextFloat() * 2.0f - 1.0f;   // new random step each cycle
            }
            return v;
        }

    private:
        double sampleRate = 44100.0;
        float phase = 0.0f, held = 0.0f;
        juce::Random random;
    };

    class RefractVoice : public juce::SynthesiserVoice
    {
    public:
        RefractVoice (const ParamRefs& params, const PlaybackBank& bank, const float& modWheel);

        void prepare (double sampleRate, int maxBlockSize);

        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<RefractSound*> (s) != nullptr; }
        void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
        void stopNote (float velocity, bool allowTailOff) override;
        void pitchWheelMoved (int newValue) override;
        void controllerMoved (int, int) override {}

        using juce::SynthesiserVoice::renderNextBlock;
        void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override;

    private:
        // Modulation and parameter changes are calculated every 32 samples
        // ("control rate") rather than every sample - much cheaper, inaudible.
        static constexpr int controlBlockSize = 32;

        void updateControl (int numSamples);
        void finishNote();
        float readSample (const SampleData& data, int channel, double position) const;

        const ParamRefs& params;
        const PlaybackBank& bank;
        const float& modWheel;

        double sampleRate = 44100.0;

        struct LayerState
        {
            SampleDataPtr data;           // keeps the sample alive while this note plays
            bool   active = false;
            double position = 0.0;        // read position in samples (fractional)
            double increment = 0.0;       // how far to move per output sample
            double loopStart = 0.0;
            bool   loop = false;
            float  gainL = 0.0f, gainR = 0.0f;       // current (ramped) gains
            float  stepL = 0.0f, stepR = 0.0f;       // per-sample ramp toward the target
        };
        std::array<LayerState, numLayers> layers;

        double subPhase = 0.0, subIncrement = 0.0;
        int    subWave = 0;
        float  subGain = 0.0f, subStep = 0.0f, subPanL = 1.0f, subPanR = 1.0f;

        juce::ADSR ampEnv, modEnv;
        Lfo lfo1, lfo2;
        juce::dsp::StateVariableTPTFilter<float> filter;
        int filterType = -1;

        int   note = 60;
        float velocity = 1.0f;
        float pitchBend = 0.0f;           // semitones
        float modEnvValue = 0.0f;
        float ampGain = 1.0f;
    };
}
