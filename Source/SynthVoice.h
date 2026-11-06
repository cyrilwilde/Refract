#pragma once
//
// SynthVoice.h
// One voice = one playing note. The synth owns 16 of these and hands each new
// note to a free one. Each voice plays the 3 sample layers + sub oscillator,
// runs its own 4 envelopes, 4 LFOs and filter, and applies the mod matrix.
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

    //==========================================================================
    // LFO with 6 shapes, jitter (random cycle lengths) and smoothing.
    // Evaluated once per control block (every 32 samples).
    //==========================================================================
    class Lfo
    {
    public:
        void setSampleRate (double sr) { sampleRate = sr; }

        void start (float startPhase)
        {
            phase = startPhase - std::floor (startPhase);
            jitterMul = 1.0;
            randomValue = random.nextFloat() * 2.0f - 1.0f;
            primed = false;
        }

        double getPhase() const       { return phase; }
        void   setPhase (double p)    { phase = p - std::floor (p); }

        // Returns the value now (-1..1), then moves forward by numSamples.
        float advance (int numSamples, double hz, int shape, float jitter, float smooth)
        {
            const float raw = shapeValue (shape);

            // Smooth: a simple low-pass "glide" on the output. At 0 it's off; at 1
            // each change takes about half a cycle - turns steps into curves.
            if (! primed || smooth <= 0.0001f)
            {
                smoothed = raw;
                primed = true;
            }
            else
            {
                const double cycle = 1.0 / juce::jmax (hz, 0.001);
                const double tau = (double) (smooth * smooth) * cycle * 0.5;
                const float coef = (float) (1.0 - std::exp (-(double) numSamples / (tau * sampleRate)));
                smoothed += (raw - smoothed) * coef;
            }

            phase += juce::jlimit (0.0, 2000.0, hz) * jitterMul * (double) numSamples / sampleRate;
            if (phase >= 1.0)
            {
                phase -= std::floor (phase);
                randomValue = random.nextFloat() * 2.0f - 1.0f;   // next random step
                // Jitter: each new cycle runs up to an octave faster or slower
                jitterMul = jitter > 0.0f ? std::pow (2.0, (double) jitter * (random.nextDouble() * 2.0 - 1.0)) : 1.0;
            }
            return smoothed;
        }

        float shapeValue (int shape) const
        {
            const float p = (float) phase;
            switch (shape)
            {
                case lfoSine:     return std::sin (juce::MathConstants<float>::twoPi * p);
                case lfoTriangle: return 1.0f - 4.0f * std::abs (p - 0.5f);
                case lfoSawUp:    return 2.0f * p - 1.0f;
                case lfoSawDown:  return 1.0f - 2.0f * p;
                case lfoSquare:   return p < 0.5f ? 1.0f : -1.0f;
                default:          return randomValue;
            }
        }

    private:
        double sampleRate = 44100.0;
        double phase = 0.0, jitterMul = 1.0;
        float randomValue = 0.0f, smoothed = 0.0f;
        bool primed = false;
        juce::Random random;
    };

    //==========================================================================
    // Things every voice shares: settings, host tempo, and the "lead voice"
    // (most recent note) whose modulation drives the global effects and the
    // modulation rings on the dials. Used only on the audio thread.
    //==========================================================================
    struct HostInfo { double bpm = 120.0, ppq = 0.0; bool playing = false; };

    struct VoiceShared
    {
        const ParamTable*   table = nullptr;
        const ParamRefs*    params = nullptr;
        const ModRouting*   routing = nullptr;
        const PlaybackBank* bank = nullptr;
        PlayheadArray*      playheads = nullptr;

        HostInfo host;
        float modWheel = 0.0f, aftertouch = 0.0f;
        std::array<double, numLfos> globalLfoPhase {};

        juce::uint32 noteCounter = 0, latestNote = 0;
        bool leadActive = false;
        std::array<float, numModSources> leadSources {};
        std::vector<float> leadOffsets;

        // Tempo in Hz for an LFO in Sync mode
        double syncHz (int syncIndex) const { return host.bpm / 60.0 / syncBeats (syncIndex); }
    };

    // Synthesiser that also remembers the mod wheel and channel aftertouch, so
    // notes started *after* they moved still know where they are.
    class RefractSynth : public juce::Synthesiser
    {
    public:
        VoiceShared* shared = nullptr;

        void handleController (int midiChannel, int controllerNumber, int controllerValue) override
        {
            if (controllerNumber == 1 && shared != nullptr)
                shared->modWheel = (float) controllerValue / 127.0f;
            juce::Synthesiser::handleController (midiChannel, controllerNumber, controllerValue);
        }

        void handleChannelPressure (int midiChannel, int value) override
        {
            if (shared != nullptr)
                shared->aftertouch = (float) value / 127.0f;
            juce::Synthesiser::handleChannelPressure (midiChannel, value);
        }
    };

    //==========================================================================
    class RefractVoice : public juce::SynthesiserVoice
    {
    public:
        RefractVoice (int voiceIndex, VoiceShared& shared);

        void prepare (double sampleRate, int maxBlockSize);

        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<RefractSound*> (s) != nullptr; }
        void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
        void stopNote (float velocity, bool allowTailOff) override;
        void pitchWheelMoved (int newValue) override;
        void controllerMoved (int, int) override {}
        void aftertouchChanged (int value) override        { polyAftertouch = (float) value / 127.0f; }
        void channelPressureChanged (int value) override   { polyAftertouch = (float) value / 127.0f; }

        using juce::SynthesiserVoice::renderNextBlock;
        void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override;

    private:
        // Modulation and parameter changes are calculated every 32 samples
        // ("control rate") rather than every sample - much cheaper, inaudible.
        static constexpr int controlBlockSize = 32;

        struct LayerState
        {
            SampleDataPtr data;           // keeps the sample alive while this note plays
            bool   active = false;
            bool   reversed = false;      // reading the reversed copy of the sample
            double position = 0.0;        // read position in samples (fractional)
            double increment = 0.0;       // how far to move per output sample
            int    direction = 1;         // +1 forwards, -1 backwards (ping-pong)

            // Region in the buffer being read (already mirrored when reversed)
            double regionEnd = 0.0, loopStart = 0.0, loopEnd = 0.0, crossfade = 0.0;
            int    loopMode = loopOff;

            float  gainL = 0.0f, gainR = 0.0f;       // current (ramped) gains
            float  stepL = 0.0f, stepR = 0.0f;       // per-sample ramp toward the target
        };

        // A parameter's value for this voice, including its modulation
        float v (int index) const { return shared.table->value (index, offsets[(size_t) index]); }

        void computeModulation (int numSamples);
        void updateControl (int numSamples);
        void updateRegion (LayerState& ls, const ParamRefs::Layer& lp, bool setStart);
        void finishNote();
        void publishPlayheads();
        static float readSample (const juce::AudioBuffer<float>& buffer, int channel, double position);

        const int index;
        VoiceShared& shared;

        double sampleRate = 44100.0;
        std::array<LayerState, numLayers> layers;

        // Modulation state
        std::vector<float> offsets;                    // per ParamTable entry, in knob travel
        std::array<float, numModSources> sources {};
        std::array<juce::ADSR, numEnvs> envs;          // envs[0] is the amp envelope
        std::array<float, numEnvs> envValues {};
        std::array<Lfo, numLfos> lfos;
        juce::Random random;
        float noteRandom = 0.0f;
        juce::uint32 noteId = 0;

        double subPhase = 0.0, subIncrement = 0.0;
        int    subWave = 0;
        float  subGain = 0.0f, subStep = 0.0f, subPanL = 1.0f, subPanR = 1.0f;

        juce::dsp::StateVariableTPTFilter<float> filter;
        int filterType = -1;

        int   note = 60;
        float velocity = 1.0f;
        float pitchBend = 0.0f;           // semitones
        float polyAftertouch = 0.0f;
        float ampGain = 1.0f;
        bool  keyReleased = false;        // note-off received (for "Fwd + Release" loops)
    };
}
