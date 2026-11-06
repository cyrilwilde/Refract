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

    RefractVoice::RefractVoice (int voiceIndex, VoiceShared& s)
        : index (voiceIndex), shared (s)
    {
        offsets.assign ((size_t) shared.table->size(), 0.0f);   // allocated once, never on the audio thread
    }

    void RefractVoice::prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        for (auto& e : envs) e.setSampleRate (sr);
        for (auto& l : lfos) l.setSampleRate (sr);

        filter.prepare ({ sr, (juce::uint32) maxBlockSize, 2 });
        filterType = -1;
    }

    // Converts the Start/End/Loop knobs (0..1 of the sample) into sample positions.
    // Ableton-style rules: the loop always sits inside Start..End, and the
    // crossfade borrows audio from *before* the loop start, so it can't be
    // longer than the audio available there.
    void RefractVoice::updateRegion (LayerState& ls, const ParamRefs::Layer& lp, bool setStart)
    {
        const double len = (double) ls.data->audio.getNumSamples();

        double s = v (lp.start) * len, e = v (lp.end) * len;
        if (e < s) std::swap (s, e);
        if (e - s < 64.0) e = juce::jmin (len, s + 64.0);

        double a = v (lp.loopStart) * len, b = v (lp.loopEnd) * len;
        if (b < a) std::swap (a, b);
        a = juce::jlimit (s, e, a);
        b = juce::jlimit (s, e, b);

        if (setStart)
            ls.reversed = lp.reverse->load() > 0.5f;   // fixed for the whole note

        if (ls.reversed)
        {
            // Reading the reversed copy: mirror every point, so "start" becomes len - end, etc.
            const double ns = len - e, ne = len - s, na = len - b, nb = len - a;
            s = ns; e = ne; a = na; b = nb;
        }

        ls.regionEnd = juce::jmin (e, len - 1.0);
        ls.loopStart = a;
        ls.loopEnd   = juce::jmin (b, len - 1.0);
        ls.crossfade = juce::jlimit (0.0, a, (double) v (lp.crossfade) * (ls.loopEnd - ls.loopStart));
        ls.loopMode  = (int) lp.loopMode->load();

        if (setStart)
        {
            ls.position  = s;
            ls.direction = 1;
        }
    }

    void RefractVoice::startNote (int midiNote, float vel, juce::SynthesiserSound*, int pitchWheel)
    {
        const auto& P = *shared.params;
        const auto& T = *shared.table;

        note = midiNote;
        velocity = vel;
        keyReleased = false;
        noteRandom = random.nextFloat() * 2.0f - 1.0f;
        noteId = ++shared.noteCounter;
        shared.latestNote = noteId;
        pitchWheelMoved (pitchWheel);

        std::fill (offsets.begin(), offsets.end(), 0.0f);   // forget the previous note's modulation
        envValues.fill (0.0f);

        // LFOs: Retrigger starts each note at the Phase knob; otherwise the LFO
        // joins the free-running global LFO, so all notes move together.
        for (int k = 0; k < numLfos; ++k)
        {
            const auto& L = P.lfos[(size_t) k];
            const float phaseKnob = T.value (L.phase, 0.0f);
            const bool retrig = L.retrig->load() > 0.5f;
            lfos[(size_t) k].start (retrig ? phaseKnob : (float) shared.globalLfoPhase[(size_t) k] + phaseKnob);
        }

        computeModulation (controlBlockSize);   // so the very first sample is already modulated

        for (int i = 0; i < numLayers; ++i)
        {
            auto& ls = layers[(size_t) i];
            ls.data = shared.bank->get (i);   // grab the current version of this layer's sample
            ls.active = ls.data != nullptr && ls.data->audio.getNumSamples() > 128;
            ls.gainL = ls.gainR = ls.stepL = ls.stepR = 0.0f;
            if (ls.active)
                updateRegion (ls, P.layers[(size_t) i], true);
        }

        subPhase = 0.0;
        subGain = subStep = 0.0f;
        filter.reset();

        for (auto& e : envs) e.reset();
        updateControl (controlBlockSize);   // sets envelope params, gains, filter
        // Start ramps from the target so the very first sample isn't faded in twice
        for (auto& ls : layers)
        {
            ls.gainL += ls.stepL * controlBlockSize; ls.stepL = 0.0f;
            ls.gainR += ls.stepR * controlBlockSize; ls.stepR = 0.0f;
        }
        subGain += subStep * controlBlockSize; subStep = 0.0f;

        for (auto& e : envs) e.noteOn();
    }

    void RefractVoice::stopNote (float, bool allowTailOff)
    {
        keyReleased = true;
        if (allowTailOff)
            for (auto& e : envs) e.noteOff();   // let the release stages play out
        else
            finishNote();                        // voice was stolen - stop immediately
    }

    void RefractVoice::pitchWheelMoved (int newValue)
    {
        pitchBend = (float) (newValue - 8192) / 8192.0f * 2.0f;   // +/- 2 semitones
    }

    void RefractVoice::finishNote()
    {
        clearCurrentNote();
        for (auto& e : envs) e.reset();
        for (auto& ls : layers)
        {
            ls.active = false;
            ls.data.reset();   // safe on the audio thread: the processor always holds another reference
        }
        publishPlayheads();
    }

    void RefractVoice::publishPlayheads()
    {
        for (int i = 0; i < numLayers; ++i)
        {
            const auto& ls = layers[(size_t) i];
            float pos = -1.0f;
            if (ls.active && ls.data != nullptr)
            {
                pos = (float) (ls.position / (double) ls.data->audio.getNumSamples());
                if (ls.reversed) pos = 1.0f - pos;   // show it on the forwards waveform
            }
            (*shared.playheads)[(size_t) i][(size_t) index].store (pos, std::memory_order_relaxed);
        }
    }

    // Step 1 of every control block: work out every modulation source, then run
    // the mod matrix to get an offset for every modulatable parameter.
    void RefractVoice::computeModulation (int n)
    {
        const auto& P = *shared.params;
        const auto& T = *shared.table;
        const auto& R = *shared.routing;

        // --- Sources -------------------------------------------------------------
        for (int e = 0; e < numEnvs; ++e)
            sources[(size_t) (srcEnv1 + e)] = envValues[(size_t) e];

        const double noteHz = juce::MidiMessage::getMidiNoteInHertz (note);
        for (int k = 0; k < numLfos; ++k)
        {
            // LFO settings use last block's modulation, so LFOs can modulate each other
            const auto& L = P.lfos[(size_t) k];
            const int mode = (int) L.mode->load();
            const double hz = mode == rateSync ? shared.syncHz ((int) L.sync->load())
                            : mode == rateFreq ? noteHz * (double) v (L.ratio)
                                               : (double) v (L.rate);
            sources[(size_t) (srcLfo1 + k)] = lfos[(size_t) k].advance (n, hz, (int) L.shape->load(), v (L.jitter), v (L.smooth))
                                            * v (L.depth);
        }

        sources[srcNone]       = 0.0f;
        sources[srcVelocity]   = velocity;
        sources[srcModWheel]   = shared.modWheel;
        sources[srcAftertouch] = juce::jmax (polyAftertouch, shared.aftertouch);
        sources[srcPitchBend]  = pitchBend / 2.0f;
        sources[srcKeyTrack]   = (float) (note - 60) / 60.0f;
        sources[srcNoteRandom] = noteRandom;
        for (int m = 0; m < numMacros; ++m)
            sources[(size_t) (srcMacro1 + m)] = P.macros[(size_t) m]->load();

        // --- Matrix: offset[target] += source * amount --------------------------
        std::fill (offsets.begin(), offsets.end(), 0.0f);
        const int numTargets = T.size();
        for (int slot = 0; slot < numModSlots; ++slot)
        {
            const int s = R.source[(size_t) slot].load (std::memory_order_relaxed);
            const int t = R.target[(size_t) slot].load (std::memory_order_relaxed);
            if (s > srcNone && s < numModSources && t >= 0 && t < numTargets)
                offsets[(size_t) t] += sources[(size_t) s] * P.modAmounts[(size_t) slot]->load();
        }

        // The newest note drives the global effects and the dial rings
        if (noteId == shared.latestNote)
        {
            shared.leadActive = true;
            shared.leadSources = sources;
            std::copy (offsets.begin(), offsets.end(), shared.leadOffsets.begin());
        }
    }

    // Step 2: turn the (modulated) knob values into settings for this voice.
    void RefractVoice::updateControl (int n)
    {
        const auto& P = *shared.params;

        for (int e = 0; e < numEnvs; ++e)
        {
            const auto& E = P.envs[(size_t) e];
            envs[(size_t) e].setParameters ({ v (E.attack), v (E.decay), v (E.sustain), v (E.release) });
        }

        const float pitchMod = v (P.pitch) + pitchBend;
        const float voicePan = v (P.pan);
        ampGain = v (P.voiceLevel) * juce::jmap (velocity, 0.25f, 1.0f);   // softer notes are quieter

        // --- Sample layers ---
        for (int i = 0; i < numLayers; ++i)
        {
            auto& ls = layers[(size_t) i];
            const auto& lp = P.layers[(size_t) i];

            float targetL = 0.0f, targetR = 0.0f;
            if (ls.active)
            {
                updateRegion (ls, lp, false);   // loop points can move while the note plays

                const float level = lp.on->load() > 0.5f ? v (lp.level) : 0.0f;
                panGains (v (lp.pan) + voicePan, targetL, targetR);
                targetL *= level;
                targetR *= level;

                // The root note plays the sample at its original pitch; every
                // semitone away from it speeds up or slows down playback.
                const float semis = (float) note - std::round (v (lp.root)) + std::round (v (lp.coarse))
                                  + v (lp.fine) * 0.01f + pitchMod;
                ls.increment = ls.data->sampleRate / sampleRate * (double) semitonesToRatio (semis);
            }
            ls.stepL = (targetL - ls.gainL) / (float) n;
            ls.stepR = (targetR - ls.gainR) / (float) n;
        }

        // --- Sub oscillator ---
        {
            const float level  = v (P.subLevel);
            const int   octave = (int) P.subOctave->load() - 2;          // choices: -2, -1, 0
            const double freq  = juce::MidiMessage::getMidiNoteInHertz (note) * std::pow (2.0, octave)
                               * (double) semitonesToRatio (pitchMod);
            subIncrement = freq / sampleRate;
            subWave = (int) P.subWave->load();
            panGains (voicePan, subPanL, subPanR);
            subStep = (level - subGain) / (float) n;
        }

        // --- Filter ---
        const int type = (int) P.filterType->load();
        if (type != filterType)
        {
            filterType = type;
            filter.setType (type == 0 ? juce::dsp::StateVariableTPTFilterType::lowpass
                          : type == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                      : juce::dsp::StateVariableTPTFilterType::highpass);
        }
        filter.setCutoffFrequency (juce::jlimit (20.0f, (float) sampleRate * 0.45f, v (P.cutoff)));
        filter.setResonance (0.5f * std::pow (30.0f, v (P.resonance)));   // Q from 0.5 (soft) to 15 (screaming)
    }

    // 4-point cubic (Hermite) interpolation: reading "between" samples smoothly is
    // what lets us repitch a sample without the grainy sound of simple rounding.
    float RefractVoice::readSample (const juce::AudioBuffer<float>& buffer, int channel, double position)
    {
        const int   len = buffer.getNumSamples();
        const float* s  = buffer.getReadPointer (juce::jmin (channel, buffer.getNumChannels() - 1));
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
        constexpr float halfPi = juce::MathConstants<float>::halfPi;

        while (numSamples > 0)
        {
            const int n = juce::jmin (numSamples, controlBlockSize);
            computeModulation (n);
            updateControl (n);

            for (int i = 0; i < n; ++i)
            {
                float left = 0.0f, right = 0.0f;

                // --- Sample layers ---
                for (auto& ls : layers)
                {
                    ls.gainL += ls.stepL;
                    ls.gainR += ls.stepR;
                    if (! ls.active)
                        continue;

                    const auto& buf = ls.reversed ? ls.data->reversed : ls.data->audio;
                    const double loopLen = ls.loopEnd - ls.loopStart;
                    const bool looping = ls.loopMode != loopOff && loopLen >= 64.0
                                      && ! (ls.loopMode == loopRelease && keyReleased);
                    const double p = ls.position;

                    float l = readSample (buf, 0, p);
                    float r = readSample (buf, 1, p);   // mono files reuse channel 0

                    // Loop crossfade: as we approach the loop end, fade in the audio
                    // from just before the loop start. When we jump back, the two
                    // match exactly - no click.
                    if (looping && ls.loopMode != loopPingPong && ls.crossfade >= 1.0
                        && p >= ls.loopEnd - ls.crossfade && p < ls.loopEnd)
                    {
                        const float t = (float) ((p - (ls.loopEnd - ls.crossfade)) / ls.crossfade);
                        const float fadeOut = std::cos (t * halfPi), fadeIn = std::sin (t * halfPi);
                        const double p2 = p - loopLen;
                        l = l * fadeOut + readSample (buf, 0, p2) * fadeIn;
                        r = r * fadeOut + readSample (buf, 1, p2) * fadeIn;
                    }

                    left  += l * ls.gainL;
                    right += r * ls.gainR;

                    // Move the read position, handling loops
                    ls.position += ls.increment * (double) ls.direction;
                    if (looping)
                    {
                        if (ls.loopMode == loopPingPong)
                        {
                            if (ls.direction > 0 && ls.position >= ls.loopEnd && p < ls.loopEnd)
                            {
                                ls.position = juce::jmax (ls.loopStart, 2.0 * ls.loopEnd - ls.position);
                                ls.direction = -1;
                            }
                            else if (ls.direction < 0 && ls.position <= ls.loopStart)
                            {
                                ls.position = juce::jmin (ls.loopEnd, 2.0 * ls.loopStart - ls.position);
                                ls.direction = 1;
                            }
                        }
                        else if (ls.position >= ls.loopEnd && p < ls.loopEnd)
                        {
                            ls.position = ls.loopStart + std::fmod (ls.position - ls.loopStart, loopLen);
                        }
                    }

                    if ((ls.direction > 0 && ls.position >= ls.regionEnd) || ls.position < 0.0)
                        ls.active = false;   // reached the end marker
                }

                // --- Sub oscillator ---
                subGain += subStep;
                if (subGain > 0.0001f)
                {
                    const float ph = (float) subPhase;
                    float s;
                    if (subWave == 0)      s = std::sin (juce::MathConstants<float>::twoPi * ph);
                    else if (subWave == 1) s = 1.0f - 4.0f * std::abs (ph - 0.5f);
                    else                   s = (ph < 0.5f ? 0.6f : -0.6f);   // square, a bit quieter
                    left  += s * subGain * subPanL;
                    right += s * subGain * subPanR;
                }
                subPhase += subIncrement;
                if (subPhase >= 1.0) subPhase -= 1.0;

                // --- Filter, then the amp envelope ---
                left  = filter.processSample (0, left);
                right = filter.processSample (1, right);

                for (int e = 0; e < numEnvs; ++e)
                    envValues[(size_t) e] = envs[(size_t) e].getNextSample();
                const float g = envValues[0] * ampGain * 0.5f;   // 0.5 = headroom for chords

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
            if (! envs[0].isActive() || (! anyLayerPlaying && subGain <= 0.0001f && subStep <= 0.0f))
            {
                finishNote();
                return;
            }

            startSample += n;
            numSamples  -= n;
        }

        publishPlayheads();
    }
}
