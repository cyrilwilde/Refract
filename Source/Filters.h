#pragma once
//
// Filters.h
// The per-voice filters. One VoiceFilter can be any of these models:
//
//   SVF      - clean "state variable" filter: low/band/high pass and notch
//   Ladder   - Moog-style 4-pole ladder: thick, round, bass drops as resonance rises
//   MS-20    - Korg-35 style 2-pole: aggressive, screams and self-oscillates
//   Comb     - a short tuned delay with feedback: metallic, flanger-like tones
//   Formant  - three band-passes shaped like a voice saying A-E-I-O-U
//
// All the "zero-delay feedback" designs (SVF, Ladder, MS-20) are stable at
// any cutoff and resonance, even when modulated hard.
//
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

namespace refract
{
    enum FilterModel
    {
        fltOff = 0,
        fltSvfLow, fltSvfBand, fltSvfHigh, fltSvfNotch,
        fltLadderLow24, fltLadderLow12, fltLadderHigh24,
        fltMs20Low, fltMs20High,
        fltCombPlus, fltCombMinus,
        fltFormant,
        numFilterModels
    };
    inline juce::StringArray filterModelNames()
    {
        return { "Off", "SVF Low Pass", "SVF Band Pass", "SVF High Pass", "SVF Notch",
                 "Ladder LP 24", "Ladder LP 12", "Ladder HP 24",
                 "MS-20 Low Pass", "MS-20 High Pass",
                 "Comb +", "Comb -", "Formant" };
    }

    enum FilterRouting { routeSerial = 0, routeParallel };
    inline juce::StringArray filterRoutingNames() { return { "Serial", "Parallel" }; }

    class VoiceFilter
    {
    public:
        void prepare (double sr)
        {
            sampleRate = sr;
            combSize = juce::nextPowerOfTwo ((int) (sr / 15.0) + 4);   // long enough for 15 Hz
            for (auto& c : comb) c.assign ((size_t) combSize, 0.0f);
            reset();
        }

        void reset()
        {
            for (auto& ch : state) ch = {};
            for (auto& c : comb) std::fill (c.begin(), c.end(), 0.0f);
            combWrite = 0;
        }

        // Called once per control block. cutoff in Hz, resonance/drive/shape 0..1
        void setParameters (int newModel, float cutoff, float resonance, float drive, float shape)
        {
            if (newModel != model)
            {
                model = newModel;
                reset();   // different filters keep different state - start clean
            }
            const float nyq = (float) sampleRate * 0.45f;
            fc  = juce::jlimit (20.0f, nyq, cutoff);
            res = juce::jlimit (0.0f, 1.0f, resonance);
            pre = 1.0f + drive * drive * 15.0f;            // drive: up to x16 into a soft clipper
            post = 1.0f / std::sqrt (pre);                 // ...and back down, roughly level-matched
            shp = juce::jlimit (0.0f, 1.0f, shape);

            g = std::tan (juce::MathConstants<float>::pi * fc / (float) sampleRate);
            G = g / (1.0f + g);

            switch (model)
            {
                case fltSvfLow: case fltSvfBand: case fltSvfHigh: case fltSvfNotch:
                    setSvf (svf, g, 2.0f - 2.0f * res * 0.98f);
                    break;

                case fltLadderLow24: case fltLadderLow12: case fltLadderHigh24:
                    ladderK = res * 4.2f;   // ~4 = self-oscillation
                    break;

                case fltMs20Low: case fltMs20High:
                    msK = 0.01f + res * 2.05f;   // ~2 = self-oscillation (the MS-20 scream)
                    msAlpha0 = 1.0f / (1.0f - msK * G + msK * G * G);
                    break;

                case fltCombPlus: case fltCombMinus:
                    combDelay = juce::jlimit (2.0f, (float) combSize - 4.0f, (float) sampleRate / fc);
                    combFeedback = (model == fltCombPlus ? 1.0f : -1.0f) * res * 0.97f;
                    combDamp = 1.0f - shp * 0.9f;   // Shape = darker repeats
                    break;

                case fltFormant:
                {
                    // Shape morphs through the vowels A-E-I-O-U; Cutoff shifts the whole
                    // set up or down (1 kHz = natural); Resonance narrows the peaks.
                    static const float vowels[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                                        { 570, 840, 2410 },  { 300, 870, 2240 } };
                    const float pos = shp * 4.0f;
                    const int a = juce::jmin ((int) pos, 3);
                    const float t = pos - (float) a;
                    const float shift = juce::jlimit (0.25f, 4.0f, fc / 1000.0f);
                    const float k = 1.0f / (2.0f + res * 18.0f);   // Q from 2 to 20
                    for (int f = 0; f < 3; ++f)
                    {
                        const float hz = juce::jlimit (40.0f, nyq, (vowels[a][f] + t * (vowels[a + 1][f] - vowels[a][f])) * shift);
                        setSvf (formant[(size_t) f], std::tan (juce::MathConstants<float>::pi * hz / (float) sampleRate), k);
                    }
                    break;
                }
                default: break;
            }
        }

        bool isActive() const { return model != fltOff; }

        float process (int ch, float x)
        {
            auto& s = state[(size_t) ch];
            if (model == fltOff)
                return x;

            x = std::tanh (x * pre) * post;   // drive

            switch (model)
            {
                case fltSvfLow:   { float lp, bp, hp; runSvf (svf, s.svf, x, lp, bp, hp); return lp; }
                case fltSvfBand:  { float lp, bp, hp; runSvf (svf, s.svf, x, lp, bp, hp); return bp; }
                case fltSvfHigh:  { float lp, bp, hp; runSvf (svf, s.svf, x, lp, bp, hp); return hp; }
                case fltSvfNotch: { float lp, bp, hp; runSvf (svf, s.svf, x, lp, bp, hp); return lp + hp; }

                case fltLadderLow24: case fltLadderLow12: case fltLadderHigh24:
                {
                    // Solve the feedback loop exactly (zero-delay feedback), then
                    // saturate the input like the transistors in the real thing.
                    const float k = ladderK;
                    const bool low = model != fltLadderHigh24;
                    const float in = low ? x * (1.0f + k * 0.5f) : x;   // put back some of the bass resonance removes
                    const float B = 1.0f - G;
                    const float S = G * G * G * B * s.ladder[0] + G * G * B * s.ladder[1] + G * B * s.ladder[2] + B * s.ladder[3];
                    const float G4 = G * G * G * G;
                    const float y4 = (G4 * in + S) / (1.0f + k * G4);
                    float u = std::tanh (in - k * y4);
                    float y[4];
                    for (int i = 0; i < 4; ++i)
                    {
                        const float v = (u - s.ladder[i]) * G;
                        y[i] = v + s.ladder[i];
                        s.ladder[i] = y[i] + v;
                        u = y[i];
                    }
                    if (model == fltLadderLow24) return y[3];
                    if (model == fltLadderLow12) return y[1];
                    const float u0 = std::tanh (in - k * y4);
                    return u0 - 4.0f * y[0] + 6.0f * y[1] - 4.0f * y[2] + y[3];
                }

                case fltMs20Low:
                {
                    // Korg-35 low pass: two one-poles with a high-passed feedback path
                    const float lpf2Beta = (msK - msK * G) / (1.0f + g);
                    const float hpf1Beta = -1.0f / (1.0f + g);
                    const float y1 = onePoleLow (s.ms[0], x);
                    const float S35 = hpf1Beta * s.ms[2] + lpf2Beta * s.ms[1];
                    const float u = std::tanh (msAlpha0 * (y1 + S35));
                    const float y = msK * onePoleLow (s.ms[1], u);
                    onePoleLow (s.ms[2], y);   // updates the feedback high-pass state
                    return y / msK;
                }

                case fltMs20High:
                {
                    // Korg-35 high pass, with an extra pole in front for a 12 dB slope
                    const float hpf2Beta = -G / (1.0f + g);
                    const float lpf1Beta = 1.0f / (1.0f + g);
                    const float x0 = x - onePoleLow (s.ms[3], x);
                    const float y1 = x0 - onePoleLow (s.ms[0], x0);
                    const float S35 = hpf2Beta * s.ms[1] + lpf1Beta * s.ms[2];
                    const float y = std::tanh (msK * msAlpha0 * (y1 + S35));
                    const float h2 = y - onePoleLow (s.ms[1], y);
                    onePoleLow (s.ms[2], h2);
                    return y / msK;
                }

                case fltCombPlus: case fltCombMinus:
                {
                    auto& buf = comb[(size_t) ch];
                    const int mask = combSize - 1;
                    float readPos = (float) combWrite - combDelay;
                    if (readPos < 0.0f) readPos += (float) combSize;
                    const int i0 = (int) readPos;
                    const float t = readPos - (float) i0;
                    const float delayed = buf[(size_t) (i0 & mask)] + t * (buf[(size_t) ((i0 + 1) & mask)] - buf[(size_t) (i0 & mask)]);
                    s.combLp += (delayed - s.combLp) * combDamp;
                    const float y = x + s.combLp * combFeedback;
                    buf[(size_t) combWrite] = y;
                    if (ch == 1 || ! stereoComb) combWrite = (combWrite + 1) & mask;
                    return y * (1.0f - res * 0.5f);
                }

                case fltFormant:
                {
                    float out = 0.0f;
                    static const float gains[3] = { 1.0f, 0.6f, 0.35f };
                    for (int f = 0; f < 3; ++f)
                    {
                        float lp, bp, hp;
                        runSvf (formant[(size_t) f], s.formant[(size_t) f], x, lp, bp, hp);
                        out += bp * gains[f];
                    }
                    return out * 1.6f;
                }
                default: return x;
            }
        }

        // The comb's write position is shared by both channels; call this once
        // per sample frame when only one channel is processed (mono).
        void setStereo (bool stereo) { stereoComb = stereo; }

    private:
        struct SvfCoefs { float k = 2.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f; };
        struct SvfState { float ic1 = 0.0f, ic2 = 0.0f; };
        struct ChannelState
        {
            SvfState svf;
            float ladder[4] = {};
            float ms[4] = {};
            float combLp = 0.0f;
            std::array<SvfState, 3> formant {};
        };

        static void setSvf (SvfCoefs& c, float gg, float k)
        {
            c.k = k;
            c.a1 = 1.0f / (1.0f + gg * (gg + k));
            c.a2 = gg * c.a1;
            c.a3 = gg * c.a2;
        }

        // Andrew Simper's "SVF" (trapezoidal integration): one step gives low, band and high pass
        static void runSvf (const SvfCoefs& c, SvfState& s, float x, float& lp, float& bp, float& hp)
        {
            const float v3 = x - s.ic2;
            const float v1 = c.a1 * s.ic1 + c.a2 * v3;
            const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
            s.ic1 = 2.0f * v1 - s.ic1;
            s.ic2 = 2.0f * v2 - s.ic2;
            lp = v2; bp = v1; hp = x - c.k * v1 - v2;
        }

        // A one-pole low pass (high pass = input minus this), trapezoidal style
        float onePoleLow (float& z, float x) const
        {
            const float v = (x - z) * G;
            const float lp = v + z;
            z = lp + v;
            return lp;
        }

        double sampleRate = 44100.0;
        int model = fltOff;
        float fc = 1000.0f, res = 0.0f, pre = 1.0f, post = 1.0f, shp = 0.0f, g = 0.1f, G = 0.1f;
        SvfCoefs svf;
        std::array<SvfCoefs, 3> formant;
        float ladderK = 0.0f, msK = 0.01f, msAlpha0 = 1.0f;
        float combDelay = 100.0f, combFeedback = 0.0f, combDamp = 1.0f;
        int combSize = 4096, combWrite = 0;
        bool stereoComb = true;
        std::array<std::vector<float>, 2> comb;
        std::array<ChannelState, 2> state;
    };
}
