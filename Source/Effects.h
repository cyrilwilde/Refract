#pragma once
//
// Effects.h
// The effects that can go in the 6 FX slots (3 on Bus A, 3 on Bus B).
// Every slot has four general-purpose dials (P1-P4) plus Mix; what P1-P4 do
// depends on the effect chosen (the UI relabels them).
//
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "Filters.h"

namespace refract
{
    enum FxType { fxOff = 0, fxDelay, fxReverb, fxChorus, fxPhaser, fxDistortion, fxBitcrush, fxFilter, numFxTypes };
    inline juce::StringArray fxTypeNames() { return { "Off", "Delay", "Reverb", "Chorus", "Phaser", "Distortion", "Bitcrush", "Filter" }; }

    enum DelayMode { delayDigital = 0, delayPingPong, delayTape, delayGranular };
    inline juce::StringArray delayModeNames() { return { "Digital", "Ping-Pong", "Tape", "Granular" }; }

    // What P1-P4 mean for each effect (and delay mode)
    inline juce::StringArray fxKnobLabels (int type, int delayMode)
    {
        switch (type)
        {
            case fxDelay:
                if (delayMode == delayGranular) return { "Time", "Feedback", "Pitch", "Spray" };
                if (delayMode == delayTape)     return { "Time", "Feedback", "Tone", "Wow" };
                return { "Time", "Feedback", "Tone", "Width" };
            case fxReverb:     return { "Size", "Damping", "Width", "Pre-Delay" };
            case fxChorus:     return { "Rate", "Depth", "Feedback", "Delay" };
            case fxPhaser:     return { "Rate", "Depth", "Centre", "Feedback" };
            case fxDistortion: return { "Drive", "Shape", "Tone", "Output" };
            case fxBitcrush:   return { "Bits", "Downsample", "Tone", "Output" };
            case fxFilter:     return { "Cutoff", "Resonance", "LP-BP-HP", "Drive" };
            default:           return { "-", "-", "-", "-" };
        }
    }

    struct FxSettings
    {
        int type = fxOff, delayMode = delayDigital, division = 7;
        bool sync = false;
        float p[4] = { 0.5f, 0.4f, 0.7f, 0.3f };
        float mix = 1.0f;
    };

    //==========================================================================
    // Multi-mode delay: Digital, Ping-Pong, Tape (wow, flutter, saturation),
    // Granular (the echoes are re-played as little pitch-shifted grains).
    //==========================================================================
    class FxDelay
    {
    public:
        void prepare (double sr)
        {
            sampleRate = sr;
            size = juce::nextPowerOfTwo ((int) (sr * 5.0));
            mask = size - 1;
            for (auto& b : buffer) b.assign ((size_t) size, 0.0f);
            reset();
        }

        void reset()
        {
            for (auto& b : buffer) std::fill (b.begin(), b.end(), 0.0f);
            write = 0;
            lp[0] = lp[1] = 0.0f;
            current = -1.0;
            for (auto& g : grains) g.active = false;
            spawnCounter = 0;
        }

        void process (float* L, float* R, int n, double delaySamples, float feedback, float toneCoef,
                      int mode, float p3, float p4)
        {
            delaySamples = juce::jlimit (1.0, (double) size - 4.0, delaySamples);
            if (current < 0.0) current = delaySamples;
            const double glide = 1.0 - std::exp (-1.0 / (0.05 * sampleRate));   // time changes glide over ~50 ms

            for (int i = 0; i < n; ++i)
            {
                current += (delaySamples - current) * glide;
                const float xL = L[i], xR = R != nullptr ? R[i] : L[i];
                float outL = 0.0f, outR = 0.0f;

                switch (mode)
                {
                    case delayPingPong:
                    {
                        const float yL = read (0, current), yR = read (1, current);
                        lp[0] += (yL - lp[0]) * toneCoef;
                        lp[1] += (yR - lp[1]) * toneCoef;
                        // Mono in on the left; each channel feeds the *other* -> echoes bounce L, R, L...
                        push (0, (xL + xR) * 0.5f + lp[1] * feedback);
                        push (1, lp[0] * feedback);
                        const float mid = (lp[0] + lp[1]) * 0.5f;
                        outL = mid + (lp[0] - mid) * p4;
                        outR = mid + (lp[1] - mid) * p4;
                        break;
                    }
                    case delayTape:
                    {
                        // Wow (slow) and flutter (fast) wobble the tape speed = the delay time
                        wowPhase += 0.6 / sampleRate;     if (wowPhase >= 1.0) wowPhase -= 1.0;
                        flutterPhase += 7.0 / sampleRate; if (flutterPhase >= 1.0) flutterPhase -= 1.0;
                        const double wobble = (double) p4 * sampleRate
                                            * (0.004 * std::sin (juce::MathConstants<double>::twoPi * wowPhase)
                                             + 0.0008 * std::sin (juce::MathConstants<double>::twoPi * flutterPhase));
                        const double d = juce::jlimit (1.0, (double) size - 4.0, current + wobble);
                        const float yL = read (0, d), yR = read (1, d);
                        lp[0] += (yL - lp[0]) * toneCoef;
                        lp[1] += (yR - lp[1]) * toneCoef;
                        // Saturation in the loop: repeats get warmer, and high feedback
                        // "runs away" musically instead of exploding
                        push (0, xL + std::tanh (lp[0] * feedback * 1.1f));
                        push (1, xR + std::tanh (lp[1] * feedback * 1.1f));
                        outL = lp[0]; outR = lp[1];
                        break;
                    }
                    case delayGranular:
                    {
                        float gL = 0.0f, gR = 0.0f;
                        runGrains (current, p3, p4, gL, gR);
                        lp[0] += (gL - lp[0]) * 0.8f;
                        lp[1] += (gR - lp[1]) * 0.8f;
                        push (0, xL + lp[0] * feedback);
                        push (1, xR + lp[1] * feedback);
                        outL = lp[0]; outR = lp[1];
                        break;
                    }
                    default:   // digital: clean, with a small right-channel offset for width
                    {
                        const float yL = read (0, current);
                        const float yR = read (1, current + (double) p4 * 0.03 * sampleRate);
                        lp[0] += (yL - lp[0]) * toneCoef;
                        lp[1] += (yR - lp[1]) * toneCoef;
                        push (0, xL + lp[0] * feedback);
                        push (1, xR + lp[1] * feedback);
                        outL = lp[0]; outR = lp[1];
                        break;
                    }
                }

                write = (write + 1) & mask;
                L[i] = outL;
                if (R != nullptr) R[i] = outR;
            }
        }

    private:
        struct Grain { bool active = false; double pos = 0.0, ratio = 1.0; int age = 0, length = 1; float panL = 1.0f, panR = 1.0f; };

        float read (int ch, double delay) const
        {
            double rp = (double) write - delay;
            if (rp < 0.0) rp += (double) size;
            return readAt (ch, rp);
        }
        float readAt (int ch, double pos) const
        {
            const auto& b = buffer[(size_t) ch];
            const int i0 = (int) pos;
            const float t = (float) (pos - (double) i0);
            const float a = b[(size_t) (i0 & mask)], c = b[(size_t) ((i0 + 1) & mask)];
            return a + t * (c - a);
        }
        void push (int ch, float v) { buffer[(size_t) ch][(size_t) write] = v; }

        // Grains: 80 ms snippets read from around the delay time, at a new pitch,
        // with a smooth fade in/out. A new one starts every 20 ms (4 overlapping).
        void runGrains (double delay, float pitchKnob, float spray, float& outL, float& outR)
        {
            const int length = juce::jmax (64, (int) (0.08 * sampleRate));
            if (--spawnCounter <= 0)
            {
                spawnCounter = length / 4;
                for (auto& g : grains)
                    if (! g.active)
                    {
                        const double semis = (double) (pitchKnob - 0.5f) * 24.0;   // -12..+12 semitones
                        g.ratio = std::pow (2.0, semis / 12.0);
                        double offset = delay * (1.0 + (double) spray * (random.nextDouble() - 0.5));
                        offset = juce::jmax (offset, (double) length * juce::jmax (g.ratio, 1.0) + 8.0);   // never overtake the write head
                        g.pos = (double) write - offset;
                        if (g.pos < 0.0) g.pos += (double) size;
                        g.age = 0;
                        g.length = length;
                        const float pan = (random.nextFloat() * 2.0f - 1.0f) * spray;
                        g.panL = std::sqrt (0.5f * (1.0f - pan));
                        g.panR = std::sqrt (0.5f * (1.0f + pan));
                        g.active = true;
                        break;
                    }
            }

            for (auto& g : grains)
            {
                if (! g.active) continue;
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) g.age / (float) g.length);
                outL += readAt (0, g.pos) * w * g.panL;
                outR += readAt (1, g.pos) * w * g.panR;
                g.pos += g.ratio;
                if (g.pos >= (double) size) g.pos -= (double) size;
                if (++g.age >= g.length) g.active = false;
            }
            outL *= 0.7f; outR *= 0.7f;   // 4 overlapping Hann windows add up to ~2
        }

        double sampleRate = 44100.0;
        int size = 1 << 18, mask = (1 << 18) - 1, write = 0;
        std::array<std::vector<float>, 2> buffer;
        float lp[2] = { 0.0f, 0.0f };
        double current = -1.0, wowPhase = 0.0, flutterPhase = 0.0;
        std::array<Grain, 8> grains;
        int spawnCounter = 0;
        juce::Random random;
    };

    //==========================================================================
    // One FX slot: holds every effect, runs the one that's selected.
    //==========================================================================
    class FxSlot
    {
    public:
        void prepare (double sr, int maxBlock)
        {
            sampleRate = sr;
            maxBlockSize = maxBlock;
            const juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
            delay.prepare (sr);
            reverb.setSampleRate (sr);
            chorus.prepare (spec);
            phaser.prepare (spec);
            filter.prepare (sr);
            preDelaySize = juce::nextPowerOfTwo ((int) (sr * 0.3));
            for (auto& b : preDelay) b.assign ((size_t) preDelaySize, 0.0f);
            for (auto& d : dry) d.assign ((size_t) maxBlock, 0.0f);
            reset();
        }

        void reset()
        {
            delay.reset(); reverb.reset(); chorus.reset(); phaser.reset(); filter.reset();
            for (auto& b : preDelay) std::fill (b.begin(), b.end(), 0.0f);
            preDelayWrite = 0;
            crushHold[0] = crushHold[1] = 0.0f; crushCounter = 0.0f;
            toneLp[0] = toneLp[1] = 0.0f;
        }

        // Processes up to maxBlockSize samples of a stereo signal in place
        void process (float* L, float* R, int n, const FxSettings& s, double bpm)
        {
            if (s.type != currentType)
            {
                currentType = s.type;
                reset();   // switching effect: start clean (no old echoes)
            }
            if (s.type == fxOff || n > maxBlockSize)
                return;

            std::copy (L, L + n, dry[0].begin());
            std::copy (R, R + n, dry[1].begin());

            const float p1 = s.p[0], p2 = s.p[1], p3 = s.p[2], p4 = s.p[3];
            switch (s.type)
            {
                case fxDelay:
                {
                    const double seconds = s.sync ? syncBeats (s.division) * 60.0 / juce::jmax (20.0, bpm)
                                                  : 0.01 * std::pow (200.0, (double) p1);   // 10 ms .. 2 s
                    const float feedback = s.delayMode == delayTape ? p2 : p2 * 0.98f;
                    const float toneHz = 200.0f * std::pow (100.0f, s.delayMode == delayTape ? p3 * 0.85f : p3);   // 200 Hz .. 20 kHz
                    delay.process (L, R, n, seconds * sampleRate, feedback, coef (toneHz), s.delayMode, p3, p4);
                    break;
                }
                case fxReverb:
                {
                    // Pre-delay: a short gap before the reverb starts (keeps the dry hit clear)
                    const int pd = (int) (p4 * 0.25f * (float) sampleRate);
                    const int pmask = preDelaySize - 1;
                    for (int i = 0; i < n; ++i)
                    {
                        float* chans[2] = { L, R };
                        for (int ch = 0; ch < 2; ++ch)
                        {
                            auto& b = preDelay[(size_t) ch];
                            b[(size_t) preDelayWrite] = chans[ch][i];
                            chans[ch][i] = b[(size_t) ((preDelayWrite - pd) & pmask)];
                        }
                        preDelayWrite = (preDelayWrite + 1) & pmask;
                    }
                    juce::Reverb::Parameters rp;
                    rp.roomSize = p1; rp.damping = p2; rp.width = p3;
                    rp.wetLevel = 1.0f; rp.dryLevel = 0.0f;
                    reverb.setParameters (rp);
                    reverb.processStereo (L, R, n);
                    break;
                }
                case fxChorus:
                {
                    chorus.setRate (0.05f * std::pow (100.0f, p1));        // 0.05 .. 5 Hz
                    chorus.setDepth (p2);
                    chorus.setFeedback ((p3 * 2.0f - 1.0f) * 0.9f);
                    chorus.setCentreDelay (1.0f + p4 * 29.0f);             // 1 .. 30 ms
                    chorus.setMix (0.5f);
                    runDsp (chorus, L, R, n);
                    break;
                }
                case fxPhaser:
                {
                    phaser.setRate (0.02f * std::pow (100.0f, p1));        // 0.02 .. 2 Hz
                    phaser.setDepth (p2);
                    phaser.setCentreFrequency (200.0f * std::pow (20.0f, p3));
                    phaser.setFeedback ((p4 * 2.0f - 1.0f) * 0.9f);
                    phaser.setMix (0.5f);
                    runDsp (phaser, L, R, n);
                    break;
                }
                case fxDistortion:
                {
                    // Shape morphs: soft saturation -> hard clipping -> wave folding
                    const float drive = 1.0f + p1 * p1 * 40.0f;
                    const float makeup = 1.0f / std::sqrt (drive) * p4 * 2.0f;
                    const float c = coef (500.0f * std::pow (40.0f, p3));
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        float* x = ch == 0 ? L : R;
                        for (int i = 0; i < n; ++i)
                        {
                            const float v = x[i] * drive;
                            const float soft = std::tanh (v);
                            const float hard = juce::jlimit (-1.0f, 1.0f, v);
                            const float fold = std::sin (v * 0.5f * juce::MathConstants<float>::pi);
                            const float y = p2 < 0.5f ? soft + (hard - soft) * (p2 * 2.0f)
                                                      : hard + (fold - hard) * ((p2 - 0.5f) * 2.0f);
                            toneLp[ch] += (y - toneLp[ch]) * c;
                            x[i] = toneLp[ch] * makeup;
                        }
                    }
                    break;
                }
                case fxBitcrush:
                {
                    const float steps = std::pow (2.0f, 16.0f - p1 * 14.0f) * 0.5f;   // 16 bits .. 2 bits
                    const float hold = 1.0f + p2 * p2 * 39.0f;                         // keep each sample for up to 40
                    const float c = coef (500.0f * std::pow (40.0f, p3));
                    const float out = p4 * 2.0f;
                    for (int i = 0; i < n; ++i)
                    {
                        if ((crushCounter += 1.0f) >= hold)
                        {
                            crushCounter -= hold;
                            crushHold[0] = std::round (L[i] * steps) / steps;
                            crushHold[1] = std::round (R[i] * steps) / steps;
                        }
                        toneLp[0] += (crushHold[0] - toneLp[0]) * c;
                        toneLp[1] += (crushHold[1] - toneLp[1]) * c;
                        L[i] = toneLp[0] * out;
                        R[i] = toneLp[1] * out;
                    }
                    break;
                }
                case fxFilter:
                {
                    const int model = p3 < 0.33f ? fltSvfLow : (p3 < 0.66f ? fltSvfBand : fltSvfHigh);
                    filter.setParameters (model, 20.0f * std::pow (1000.0f, p1), p2, p4, 0.0f);
                    for (int i = 0; i < n; ++i)
                    {
                        L[i] = filter.process (0, L[i]);
                        R[i] = filter.process (1, R[i]);
                    }
                    break;
                }
                default: break;
            }

            // Mix: blend the dry input with the effect
            if (s.mix < 0.999f)
                for (int i = 0; i < n; ++i)
                {
                    L[i] = dry[0][(size_t) i] + s.mix * (L[i] - dry[0][(size_t) i]);
                    R[i] = dry[1][(size_t) i] + s.mix * (R[i] - dry[1][(size_t) i]);
                }
        }

    private:
        float coef (float hz) const
        {
            return (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * juce::jmin ((double) hz, sampleRate * 0.45) / sampleRate));
        }

        template <typename Effect>
        static void runDsp (Effect& fx, float* L, float* R, int n)
        {
            float* chans[2] = { L, R };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            fx.process (ctx);
        }

        double sampleRate = 44100.0;
        int maxBlockSize = 512, currentType = -1;
        FxDelay delay;
        juce::Reverb reverb;
        juce::dsp::Chorus<float> chorus;
        juce::dsp::Phaser<float> phaser;
        VoiceFilter filter;
        std::array<std::vector<float>, 2> preDelay, dry;
        int preDelaySize = 16384, preDelayWrite = 0;
        float crushHold[2] = { 0.0f, 0.0f }, crushCounter = 0.0f;
        float toneLp[2] = { 0.0f, 0.0f };
    };
}
