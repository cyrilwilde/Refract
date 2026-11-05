#include "SynthVoice.h"

namespace refract
{
    namespace
    {
        // Equal-power pan: -1 = left, 0 = centre (unity on both sides), +1 = right.
        void panGains (float pan, float& left, float& right)
        {
            const float angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            left  = std::cos (angle) * juce::MathConstants<float>::sqrt2;
            right = std::sin (angle) * juce::MathConstants<float>::sqrt2;
        }

        float semitonesToRatio (float semis) { return std::pow (2.0f, semis / 12.0f); }
    }

    RefractVoice::RefractVoice (const ParamRefs& p, const PlaybackBank& b, const float& wheel)
        : params (p), bank (b), modWheel (wheel) {}

    void RefractVoice::prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        ampEnv.setSampleRate (sr);
        modEnv.setSampleRate (sr);
        lfo1.setSampleRate (sr);
        lfo2.setSampleRate (sr);

        filter.prepare ({ sr, (juce::uint32) maxBlockSize, 2 });
        filterType = -1;
    }

    void RefractVoice::startNote (int midiNote, float vel, juce::SynthesiserSound*, int pitchWheel)
    {
        note = midiNote;
        velocity = vel;
        pitchWheelMoved (pitchWheel);

        for (int i = 0; i < numLayers; ++i)
        {
            auto& ls = layers[(size_t) i];
            ls.data = bank.get (i);   // grab the current version of this layer's sample
            ls.active = ls.data != nullptr && ls.data->audio.getNumSamples() > 1;
            ls.gainL = ls.gainR = ls.stepL = ls.stepR = 0.0f;
            if (ls.active)
            {
                const auto len = (double) ls.data->audio.getNumSamples();
                ls.position  = juce::jlimit (0.0, len - 1.0, (double) params.layers[(size_t) i].start->load() * len);
                ls.loopStart = ls.position;
            }
        }

        subPhase = 0.0;
        subGain = subStep = 0.0f;

        lfo1.reset();
        lfo2.reset();
        filter.reset();
        modEnvValue = 0.0f;

        ampEnv.reset();
        modEnv.reset();
        updateControl (controlBlockSize);   // sets envelope params, gains, filter
        // Start ramps from the target so the very first sample isn't faded in twice
        for (auto& ls : layers)
        {
            ls.gainL += ls.stepL * controlBlockSize; ls.stepL = 0.0f;
            ls.gainR += ls.stepR * controlBlockSize; ls.stepR = 0.0f;
        }
        subGain += subStep * controlBlockSize; subStep = 0.0f;

        ampEnv.noteOn();
        modEnv.noteOn();
    }

    void RefractVoice::stopNote (float, bool allowTailOff)
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();   // let the release stage play out
            modEnv.noteOff();
        }
        else
        {
            finishNote();       // voice was stolen - stop immediately
        }
    }

    void RefractVoice::pitchWheelMoved (int newValue)
    {
        pitchBend = (float) (newValue - 8192) / 8192.0f * 2.0f;   // +/- 2 semitones
    }

    void RefractVoice::finishNote()
    {
        clearCurrentNote();
        ampEnv.reset();
        modEnv.reset();
        for (auto& ls : layers)
        {
            ls.active = false;
            ls.data.reset();   // safe on the audio thread: the processor always holds another reference
        }
    }

    void RefractVoice::updateControl (int n)
    {
        // --- Envelope settings (cheap; lets you tweak them while notes play) ---
        ampEnv.setParameters ({ params.ampEnv.attack->load(), params.ampEnv.decay->load(),
                                params.ampEnv.sustain->load(), params.ampEnv.release->load() });
        modEnv.setParameters ({ params.modEnv.attack->load(), params.modEnv.decay->load(),
                                params.modEnv.sustain->load(), params.modEnv.release->load() });

        // --- Modulation sources, all roughly in the range -1..1 ---
        float src[numModSources] = {};
        src[srcModEnv]   = modEnvValue;
        src[srcLfo1]     = lfo1.advance (n, params.lfos[0].rate->load(), (int) params.lfos[0].shape->load());
        src[srcLfo2]     = lfo2.advance (n, params.lfos[1].rate->load(), (int) params.lfos[1].shape->load());
        src[srcVelocity] = velocity;
        src[srcModWheel] = modWheel;
        src[srcKeyTrack] = (float) (note - 60) / 60.0f;

        // --- Mod matrix: add up source * amount for each destination ---
        float dst[numModDests] = {};
        for (const auto& slot : params.mods)
        {
            const int s = (int) slot.source->load();
            const int d = (int) slot.dest->load();
            if (s > srcNone && s < numModSources && d > dstNone && d < numModDests)
                dst[d] += src[s] * slot.amount->load();
        }

        const float pitchMod = dst[dstPitch] * 12.0f;                      // up to +/- 1 octave
        const float panMod   = dst[dstPan];
        ampGain = juce::jlimit (0.0f, 2.0f, 1.0f + dst[dstAmp])
                * juce::jmap (velocity, 0.25f, 1.0f);                       // softer notes are quieter

        // --- Sample layers ---
        for (int i = 0; i < numLayers; ++i)
        {
            auto& ls = layers[(size_t) i];
            const auto& lp = params.layers[(size_t) i];

            float targetL = 0.0f, targetR = 0.0f;
            if (ls.active)
            {
                const float level = lp.on->load() > 0.5f
                                  ? juce::jlimit (0.0f, 1.0f, lp.level->load() + dst[dstLayer1 + i]) : 0.0f;
                panGains (lp.pan->load() + panMod, targetL, targetR);
                targetL *= level;
                targetR *= level;

                // Notes are relative to middle C (MIDI 60). Coarse/fine shift that.
                const float semis = (float) (note - 60) + lp.coarse->load() + lp.fine->load() * 0.01f
                                  + pitchBend + pitchMod;
                ls.increment = ls.data->sampleRate / sampleRate * (double) semitonesToRatio (semis);
                ls.loop = lp.loop->load() > 0.5f;
            }
            ls.stepL = (targetL - ls.gainL) / (float) n;
            ls.stepR = (targetR - ls.gainR) / (float) n;
        }

        // --- Sub oscillator ---
        {
            const float level  = juce::jlimit (0.0f, 1.0f, params.subLevel->load() + dst[dstSub]);
            const int   octave = (int) params.subOctave->load() - 2;          // choices: -2, -1, 0
            const double freq  = juce::MidiMessage::getMidiNoteInHertz (note) * std::pow (2.0, octave)
                               * (double) semitonesToRatio (pitchBend + pitchMod);
            subIncrement = freq / sampleRate;
            subWave = (int) params.subWave->load();
            panGains (panMod, subPanL, subPanR);
            subStep = (level - subGain) / (float) n;
        }

        // --- Filter ---
        const int type = (int) params.filterType->load();
        if (type != filterType)
        {
            filterType = type;
            filter.setType (type == 0 ? juce::dsp::StateVariableTPTFilterType::lowpass
                          : type == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                      : juce::dsp::StateVariableTPTFilterType::highpass);
        }
        const float cutoff = params.cutoff->load() * std::pow (2.0f, dst[dstCutoff] * 6.0f);   // +/- 6 octaves
        filter.setCutoffFrequency (juce::jlimit (20.0f, (float) sampleRate * 0.45f, cutoff));

        const float res = juce::jlimit (0.0f, 1.0f, params.resonance->load() + dst[dstResonance]);
        filter.setResonance (0.5f * std::pow (30.0f, res));   // Q from 0.5 (soft) to 15 (screaming)
    }

    // 4-point cubic (Hermite) interpolation: reading "between" samples smoothly is
    // what lets us repitch a sample without the grainy sound of simple rounding.
    float RefractVoice::readSample (const SampleData& data, int channel, double position) const
    {
        const int   len = data.audio.getNumSamples();
        const float* s  = data.audio.getReadPointer (juce::jmin (channel, data.audio.getNumChannels() - 1));
        const int   i1  = (int) position;
        const float t   = (float) (position - (double) i1);

        auto at = [s, len] (int i) { return s[juce::jlimit (0, len - 1, i)]; };
        const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    void RefractVoice::renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples)
    {
        if (! isVoiceActive())
            return;

        const int outChannels = output.getNumChannels();

        while (numSamples > 0)
        {
            const int n = juce::jmin (numSamples, controlBlockSize);
            updateControl (n);

            for (int i = 0; i < n; ++i)
            {
                float left = 0.0f, right = 0.0f;

                // Sample layers
                for (auto& ls : layers)
                {
                    ls.gainL += ls.stepL;
                    ls.gainR += ls.stepR;
                    if (! ls.active)
                        continue;

                    const auto& d = *ls.data;
                    left  += readSample (d, 0, ls.position) * ls.gainL;
                    right += readSample (d, 1, ls.position) * ls.gainR;   // mono files reuse channel 0

                    ls.position += ls.increment;
                    const double len = (double) d.audio.getNumSamples();
                    if (ls.position >= len - 1.0)
                    {
                        const double loopLen = len - 1.0 - ls.loopStart;
                        if (ls.loop && loopLen > 16.0)
                            ls.position = ls.loopStart + std::fmod (ls.position - ls.loopStart, loopLen);
                        else
                            ls.active = false;   // one-shot finished
                    }
                }

                // Sub oscillator
                subGain += subStep;
                if (subGain > 0.0001f)
                {
                    const float ph = (float) subPhase;
                    float v;
                    if (subWave == 0)      v = std::sin (juce::MathConstants<float>::twoPi * ph);
                    else if (subWave == 1) v = 1.0f - 4.0f * std::abs (ph - 0.5f);
                    else                   v = (ph < 0.5f ? 0.6f : -0.6f);   // square, a bit quieter
                    left  += v * subGain * subPanL;
                    right += v * subGain * subPanR;
                }
                subPhase += subIncrement;
                if (subPhase >= 1.0) subPhase -= 1.0;

                // Filter, then the amp envelope
                left  = filter.processSample (0, left);
                right = filter.processSample (1, right);

                modEnvValue = modEnv.getNextSample();
                const float g = ampEnv.getNextSample() * ampGain * 0.5f;   // 0.5 = headroom for chords

                if (outChannels > 1)
                {
                    output.addSample (0, startSample + i, left * g);
                    output.addSample (1, startSample + i, right * g);
                }
                else if (outChannels == 1)
                {
                    output.addSample (0, startSample + i, (left + right) * 0.5f * g);
                }
            }

            const bool anyLayerPlaying = std::any_of (layers.begin(), layers.end(), [] (const LayerState& l) { return l.active; });
            if (! ampEnv.isActive() || (! anyLayerPlaying && subGain <= 0.0001f && subStep <= 0.0f))
            {
                finishNote();
                return;
            }

            startSample += n;
            numSamples  -= n;
        }
    }
}
