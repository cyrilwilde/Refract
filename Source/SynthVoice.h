#pragma once
//
// SynthVoice.h
// One voice = one playing note. The synth owns 16 of these and hands each new
// note to a free one. Each voice plays the 3 sample layers, 2 oscillators,
// sub and noise; runs its own 4 envelopes, 4 LFOs and filter; applies the
// mod matrix; and handles unison and glide.
//
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "SampleData.h"
#include "Wavetable.h"
#include "Filters.h"

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
        const ParamTable*    table = nullptr;
        const ParamRefs*     params = nullptr;
        const ModRouting*    routing = nullptr;
        const PlaybackBank*  bank = nullptr;
        const WavetableBank* wavetables = nullptr;
        PlayheadArray*       playheads = nullptr;

        HostInfo host;
        float modWheel = 0.0f, aftertouch = 0.0f;
        std::array<double, numLfos> globalLfoPhase {};

        juce::uint32 noteCounter = 0, latestNote = 0;
        bool leadActive = false;
        std::array<float, numModSources> leadSources {};
        std::vector<float> leadOffsets;

        // Glide bookkeeping
        float lastNote = -1.0f;      // pitch of the previous note (-1 = none yet)
        int   keysDown = 0;          // how many keys are held right now
        bool  overlapping = false;   // the note being started overlaps a held one

        // Tempo in Hz for an LFO in Sync mode
        double syncHz (int syncIndex) const { return host.bpm / 60.0 / syncBeats (syncIndex); }
    };

    //==========================================================================
    class RefractVoice : public juce::SynthesiserVoice
    {
    public:
        RefractVoice (int voiceIndex, VoiceShared& shared);

        void prepare (double sampleRate, int maxBlockSize);

        // Mono/Legato: the next startNote changes the pitch of this voice instead
        // of starting from scratch. retrigger = restart envelopes and samples.
        void prepareLegato (bool retrigger) { legatoPending = true; legatoRetrigger = retrigger; }

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

        // One copy of a sample being read (a layer has 1-4 for unison)
        struct SampleReader
        {
            bool   active = false;
            double position = 0.0;
            int    direction = 1;        // +1 forwards, -1 backwards (ping-pong)
            double ratio = 1.0;          // unison detune as a speed ratio
            float  panL = 1.0f, panR = 1.0f;
        };

        struct LayerState
        {
            SampleDataPtr data;           // keeps the sample alive while this note plays
            bool   reversed = false;      // reading the reversed copy of the sample
            double increment = 0.0;       // speed for the un-detuned copy
            double startPos = 0.0;

            // Region in the buffer being read (already mirrored when reversed)
            double regionEnd = 0.0, loopStart = 0.0, loopEnd = 0.0, crossfade = 0.0;
            int    loopMode = loopOff;

            int numCopies = 1;
            std::array<SampleReader, maxLayerUnison> copies;
            float gain = 0.0f, gainStep = 0.0f;   // level, ramped smoothly

            bool anyActive() const
            {
                for (int c = 0; c < numCopies; ++c) if (copies[(size_t) c].active) return true;
                return false;
            }
        };

        // One oscillator with up to 8 unison voices
        struct OscState
        {
            bool   on = false;
            int    mode = oscAnalog;
            int    numVoices = 1;
            std::array<double, maxUnison> phase {};
            std::array<double, maxUnison> increment {};
            std::array<float,  maxUnison> panL {}, panR {};
            std::array<float,  maxUnison> lastOut {};      // for FM (osc 1 -> osc 2)
            std::array<bool,   maxUnison> wrapped {};      // for hard sync
            std::array<double, maxUnison> wrapFraction {};
            float wave = 2.0f, pulseWidth = 0.5f;
            WavetablePtr table;
            const float* frameA = nullptr; const float* frameB = nullptr;
            float frameMix = 0.0f;
            float gain = 0.0f, gainStep = 0.0f;
        };

        // A parameter's value for this voice, including its modulation
        float v (int index) const { return shared.table->value (index, offsets[(size_t) index]); }

        void computeModulation (int numSamples);
        void updateControl (int numSamples);
        void updateRegion (LayerState& ls, const ParamRefs::Layer& lp, bool setStart);
        void updateOscillator (int index, double baseHz, int numSamples);
        void restartLayers();
        void finishNote();
        void publishPlayheads();
        float renderOsc (OscState& o, float& left, float& right, const OscState* master, float fm, bool sync);
        static float readSample (const juce::AudioBuffer<float>& buffer, int channel, double position);

        const int index;
        VoiceShared& shared;

        double sampleRate = 44100.0;
        std::array<LayerState, numLayers> layers;
        std::array<OscState, numOscs> oscs;

        // Modulation state
        std::vector<float> offsets;                    // per ParamTable entry, in knob travel
        std::array<float, numModSources> sources {};
        std::array<juce::ADSR, numEnvs> envs;          // envs[0] is the amp envelope
        std::array<float, numEnvs> envValues {};
        std::array<Lfo, numLfos> lfos;
        juce::Random random;
        float noteRandom = 0.0f;
        juce::uint32 noteId = 0;

        // Sub and noise
        double subPhase = 0.0, subIncrement = 0.0, subIncStep = 0.0, subEnvTime = 0.0;
        int    subWave = 0;
        float  subGain = 0.0f, subStep = 0.0f, subPanL = 1.0f, subPanR = 1.0f;
        float  noiseGain = 0.0f, noiseStep = 0.0f, noiseLp = 0.0f, noiseLpCoef = 1.0f, noiseHpMix = 0.0f;

        // Mixer
        std::array<int, numMixSources> routes {};
        std::array<float, numMixSources> sendA {}, sendB {};
        float voiceSendA = 0.0f, voiceSendB = 0.0f;
        float noisePanL = 1.0f, noisePanR = 1.0f;

        std::array<VoiceFilter, 2> filters;
        std::array<float, 2> filterMix { 1.0f, 1.0f };
        int filterRouting = routeSerial;

        // Pitch and glide
        int   note = 60;                  // the key that was pressed
        float currentPitch = 60.0f;       // where the pitch is now (glides towards `note`)
        float glideRate = 0.0f;           // semitones per sample; 0 = no glide
        bool  legatoPending = false, legatoRetrigger = false;

        float velocity = 1.0f;
        float pitchBend = 0.0f;           // semitones
        float polyAftertouch = 0.0f;
        float ampGain = 1.0f;
        bool  keyReleased = false;        // note-off received (for "Fwd + Release" loops)
    };

    //==========================================================================
    // The synth: hands notes to voices. Also handles Mono/Legato modes, and
    // remembers the mod wheel and aftertouch so notes started later know them.
    //==========================================================================
    class RefractSynth : public juce::Synthesiser
    {
    public:
        RefractSynth() { heldNotes.reserve (128); }
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

        void noteOn (int midiChannel, int midiNoteNumber, float velocity) override;
        void noteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) override;
        void allNotesOff (int midiChannel, bool allowTailOff) override;

    private:
        int voiceMode() const;
        std::vector<int> heldNotes;   // in the order they were pressed (Mono/Legato)
        float lastVelocity = 1.0f;
    };
}
