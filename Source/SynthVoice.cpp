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

        // Spreads unison voice u of n evenly from -1 to +1 (0 when there's only one)
        float unisonPosition (int u, int n) { return n <= 1 ? 0.0f : 2.0f * (float) u / (float) (n - 1) - 1.0f; }

        // PolyBLEP: a tiny correction added right at a waveform's jumps. It rounds
        // off the jump just enough to remove most aliasing (the harsh, inharmonic
        // whistling a "perfect" sharp edge produces in digital audio).
        float polyBlep (double t, double dt)
        {
            if (t < dt)       { const double x = t / dt;         return (float) (x + x - x * x - 1.0); }
            if (t > 1.0 - dt) { const double x = (t - 1.0) / dt; return (float) (x * x + x + x + 1.0); }
            return 0.0f;
        }

        float analogShape (int shape, double t, double dt, float pw)
        {
            switch (shape)
            {
                case 0:  return std::sin (juce::MathConstants<float>::twoPi * (float) t);
                case 1:  return 1.0f - 4.0f * std::abs ((float) t - 0.5f);
                case 2:  return (float) (2.0 * t - 1.0) - polyBlep (t, dt);
                default:
                {
                    double t2 = t + 1.0 - (double) pw;
                    t2 -= std::floor (t2);
                    return (t < (double) pw ? 1.0f : -1.0f) + polyBlep (t, dt) - polyBlep (t2, dt);
                }
            }
        }

        // Wave 0..3 morphs sine -> triangle -> saw -> square
        float analogValue (double t, double dt, float wave, float pw)
        {
            wave = juce::jlimit (0.0f, 3.0f, wave);
            const int a = juce::jmin ((int) wave, 2);
            const float mix = wave - (float) a;
            const float va = analogShape (a, t, dt, pw);
            return mix < 0.0001f ? va : va + (analogShape (a + 1, t, dt, pw) - va) * mix;
        }

        float onePoleCoef (double hz, double sr)
        {
            return (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * hz / sr));
        }
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

        juce::ignoreUnused (maxBlockSize);
        for (auto& f : filters) f.prepare (sr);
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
            ls.startPos = s;
    }

    void RefractVoice::restartLayers()
    {
        const auto& P = *shared.params;
        for (int i = 0; i < numLayers; ++i)
        {
            auto& ls = layers[(size_t) i];
            ls.data = shared.bank->get (i);   // grab the current version of this layer's sample
            ls.gain = ls.gainStep = 0.0f;
            const bool ok = ls.data != nullptr && ls.data->audio.getNumSamples() > 128;
            if (ok)
                updateRegion (ls, P.layers[(size_t) i], true);

            ls.numCopies = juce::jlimit (1, maxLayerUnison, (int) std::round (v (P.layers[(size_t) i].unison)));
            for (auto& r : ls.copies)
            {
                r.active = ok;
                r.position = ls.startPos;
                r.direction = 1;
            }
        }
    }

    void RefractVoice::startNote (int midiNote, float vel, juce::SynthesiserSound*, int pitchWheel)
    {
        const auto& P = *shared.params;
        const auto& T = *shared.table;

        const bool legato  = legatoPending;              // Mono/Legato note change on a playing voice
        const bool restart = ! legato || legatoRetrigger;
        legatoPending = false;

        const float previousPitch = legato ? currentPitch : shared.lastNote;

        note = midiNote;
        velocity = vel;
        keyReleased = false;
        noteId = ++shared.noteCounter;
        shared.latestNote = noteId;
        pitchWheelMoved (pitchWheel);

        // --- Glide: slide from the previous note's pitch, taking Glide Time ---
        const int glideMode = (int) P.glideMode->load();
        const float glideTime = T.value (P.glideTime, 0.0f);
        const bool doGlide = previousPitch >= 0.0f && previousPitch != (float) note
                          && (glideMode == glideAlways || (glideMode == glideLegato && (legato || shared.overlapping)));
        if (doGlide && glideTime > 0.0005f)
        {
            currentPitch = previousPitch;
            glideRate = std::abs ((float) note - previousPitch) / (glideTime * (float) sampleRate);
        }
        else
        {
            currentPitch = (float) note;
            glideRate = 0.0f;
        }

        if (! restart)
            return;   // Legato: envelopes, samples and oscillators keep going - only the pitch moves

        noteRandom = random.nextFloat() * 2.0f - 1.0f;
        if (! legato)
        {
            std::fill (offsets.begin(), offsets.end(), 0.0f);   // forget the previous note's modulation
            envValues.fill (0.0f);
        }

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
        restartLayers();

        if (! legato)
        {
            // Oscillators start at a random point (per unison voice) if Random Phase is up -
            // this is what makes unison sound wide instead of phasey.
            for (int o = 0; o < numOscs; ++o)
            {
                auto& s = oscs[(size_t) o];
                const float rp = T.value (P.oscs[(size_t) o].randomPhase, 0.0f);
                for (int u = 0; u < maxUnison; ++u)
                {
                    s.phase[(size_t) u] = (double) (rp * random.nextFloat());
                    s.wrapped[(size_t) u] = false;
                    s.lastOut[(size_t) u] = 0.0f;
                }
                s.gain = s.gainStep = 0.0f;
                s.numVoices = 1;
            }
            subPhase = 0.0;
            subGain = subStep = 0.0f;
            noiseGain = noiseStep = noiseLp = 0.0f;
            for (auto& f : filters) f.reset();
            for (auto& e : envs) e.reset();
        }

        updateControl (controlBlockSize);   // sets envelope params, gains, filter

        if (! legato)
        {
            // Start ramps at their targets so the very first sample isn't faded in twice
            for (auto& ls : layers) { ls.gain += ls.gainStep * controlBlockSize; ls.gainStep = 0.0f; }
            for (auto& s : oscs)    { s.gain  += s.gainStep  * controlBlockSize; s.gainStep  = 0.0f; }
            subGain   += subStep   * controlBlockSize; subStep   = 0.0f;
            noiseGain += noiseStep * controlBlockSize; noiseStep = 0.0f;
        }

        for (auto& e : envs) e.noteOn();   // from wherever they are - no click when retriggering
    }

    void RefractVoice::stopNote (float, bool allowTailOff)
    {
        if (! allowTailOff && legatoPending)
            return;   // the synth is about to hand this voice a new pitch - keep playing

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
            for (auto& r : ls.copies) r.active = false;
            ls.data.reset();   // safe on the audio thread: the processor always holds another reference
        }
        for (auto& s : oscs)
            s.table.reset();
        publishPlayheads();
    }

    void RefractVoice::publishPlayheads()
    {
        for (int i = 0; i < numLayers; ++i)
        {
            const auto& ls = layers[(size_t) i];
            const auto& r = ls.copies[0];
            float pos = -1.0f;
            if (r.active && ls.data != nullptr)
            {
                pos = (float) (r.position / (double) ls.data->audio.getNumSamples());
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

        const double noteHz = 440.0 * std::pow (2.0, ((double) currentPitch - 69.0) / 12.0);
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

    void RefractVoice::updateOscillator (int o, double baseHz, int n)
    {
        const auto& O = shared.params->oscs[(size_t) o];
        auto& s = oscs[(size_t) o];

        s.on   = O.on->load() > 0.5f;
        s.mode = (int) O.mode->load();

        // Unison voice count can change mid-note; new voices start at random phases
        const int voices = juce::jlimit (1, maxUnison, (int) std::round (v (O.unison)));
        for (int u = s.numVoices; u < voices; ++u)
            s.phase[(size_t) u] = (double) (v (O.randomPhase) * random.nextFloat());
        s.numVoices = voices;

        const double hz = baseHz * std::pow (2.0, std::round (v (O.octave)))
                        * (double) semitonesToRatio (std::round (v (O.semi)) + v (O.fine) * 0.01f);

        // Detune: up to +/- 1 semitone at full; squared so small settings are fine-grained
        const float detuneCents = v (O.detune) * v (O.detune) * 100.0f;
        const float spread = v (O.spread);
        const float pan = v (O.pan) + v (shared.params->pan);
        double maxInc = 0.0;
        for (int u = 0; u < voices; ++u)
        {
            const float t = unisonPosition (u, voices);
            s.increment[(size_t) u] = hz * std::pow (2.0, (double) (t * detuneCents) / 1200.0) / sampleRate;
            maxInc = juce::jmax (maxInc, s.increment[(size_t) u]);
            panGains (pan + t * spread, s.panL[(size_t) u], s.panR[(size_t) u]);
        }

        // Equal loudness whatever the unison count
        const float target = s.on ? v (O.level) / std::sqrt ((float) voices) : 0.0f;
        s.gainStep = (target - s.gain) / (float) n;

        s.wave = v (O.wave);
        s.pulseWidth = v (O.pulseWidth);

        if (s.mode == oscWavetable)
        {
            s.table = shared.wavetables->get (juce::jlimit (0, numWavetables - 1, (int) O.table->load()));
            if (s.table != nullptr)
            {
                const float pos = juce::jlimit (0.0f, 1.0f, v (O.position)) * (float) (wtFrames - 1);
                const int f0 = juce::jmin ((int) pos, wtFrames - 1);
                const int f1 = juce::jmin (f0 + 1, wtFrames - 1);
                const int level = Wavetable::levelFor (maxInc);
                s.frameA = s.table->get (f0, level);
                s.frameB = s.table->get (f1, level);
                s.frameMix = pos - (float) f0;
            }
            else
            {
                s.frameA = s.frameB = nullptr;
            }
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

        // --- Glide towards the played note ---
        if (glideRate > 0.0f)
        {
            const float step = glideRate * (float) n;
            const float diff = (float) note - currentPitch;
            if (std::abs (diff) <= step) { currentPitch = (float) note; glideRate = 0.0f; }
            else                          currentPitch += diff > 0.0f ? step : -step;
        }

        const float pitch    = currentPitch + v (P.pitch) + pitchBend;   // in MIDI note numbers
        const double baseHz  = 440.0 * std::pow (2.0, ((double) pitch - 69.0) / 12.0);
        const float voicePan = v (P.pan);
        ampGain = v (P.voiceLevel) * juce::jmap (velocity, 0.25f, 1.0f);   // softer notes are quieter

        // --- Sample layers ---
        for (int i = 0; i < numLayers; ++i)
        {
            auto& ls = layers[(size_t) i];
            const auto& lp = P.layers[(size_t) i];

            float target = 0.0f;
            if (ls.data != nullptr && ls.anyActive())
            {
                updateRegion (ls, lp, false);   // loop points can move while the note plays

                // Unison copies: new ones join where the first copy is
                const int copies = juce::jlimit (1, maxLayerUnison, (int) std::round (v (lp.unison)));
                for (int c = ls.numCopies; c < copies; ++c)
                    ls.copies[(size_t) c] = ls.copies[0];
                ls.numCopies = copies;

                const float detuneCents = v (lp.detune) * v (lp.detune) * 50.0f;   // up to +/- 50 cents
                const float spread = v (lp.spread);
                const float layerPan = v (lp.pan) + voicePan;
                for (int c = 0; c < copies; ++c)
                {
                    auto& r = ls.copies[(size_t) c];
                    const float t = unisonPosition (c, copies);
                    r.ratio = std::pow (2.0, (double) (t * detuneCents) / 1200.0);
                    panGains (layerPan + t * spread, r.panL, r.panR);
                }

                target = lp.on->load() > 0.5f ? v (lp.level) / std::sqrt ((float) copies) : 0.0f;

                // The root note plays the sample at its original pitch; every
                // semitone away from it speeds up or slows down playback.
                const float semis = pitch - std::round (v (lp.root)) + std::round (v (lp.coarse)) + v (lp.fine) * 0.01f;
                ls.increment = ls.data->sampleRate / sampleRate * (double) semitonesToRatio (semis);
            }
            ls.gainStep = (target - ls.gain) / (float) n;
        }

        // --- Oscillators ---
        for (int o = 0; o < numOscs; ++o)
            updateOscillator (o, baseHz, n);

        // --- Sub oscillator ---
        {
            const int octave = (int) P.subOctave->load() - 2;          // choices: -2, -1, 0
            subIncrement = baseHz * std::pow (2.0, octave) / sampleRate;
            subWave = (int) P.subWave->load();
            panGains (voicePan, subPanL, subPanR);
            subStep = (v (P.subLevel) - subGain) / (float) n;
        }

        // --- Noise: Colour below 0 darkens (low-pass), above 0 brightens ---
        {
            const float colour = v (P.noiseColour);
            if (colour < 0.0f) { noiseLpCoef = onePoleCoef (20000.0 * std::pow (2.0, (double) colour * 7.0), sampleRate); noiseHpMix = -1.0f; }
            else               { noiseLpCoef = onePoleCoef (1500.0, sampleRate); noiseHpMix = colour; }
            noiseStep = (v (P.noiseLevel) - noiseGain) / (float) n;
        }

        // --- Filters ---
        // Cutoff = the dial (with modulation) + its own Env 2 amount + key tracking.
        // Env 2 moves the cutoff along the dial's travel, so sweeps sound even.
        const auto& T = *shared.table;
        filterRouting = (int) P.filterRouting->load();
        for (int f = 0; f < 2; ++f)
        {
            const auto& F = P.filters[(size_t) f];
            const auto& range = T[F.cutoff].range;
            const float knob = range.convertTo0to1 (v (F.cutoff)) + v (F.envAmount) * envValues[1];
            const float keyTrack = std::pow (2.0f, (pitch - 60.0f) / 12.0f * v (F.keyTrack));
            const float hz = range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, knob)) * keyTrack;
            filters[(size_t) f].setParameters ((int) F.type->load(), hz, v (F.resonance), v (F.drive), v (F.shape));
            filterMix[(size_t) f] = v (F.mix);
        }
    }

    // One sample from an oscillator (all its unison voices), added into left/right.
    // `master` is osc 1 when this is osc 2 (for FM and hard sync).
    float RefractVoice::renderOsc (OscState& o, float& left, float& right, const OscState* master, float fm, bool sync)
    {
        o.gain += o.gainStep;
        float mono = 0.0f;

        for (int u = 0; u < o.numVoices; ++u)
        {
            const size_t uu = (size_t) u;
            double ph = o.phase[uu];
            const double inc = o.increment[uu];
            size_t mu = 0;
            if (master != nullptr)
                mu = (size_t) juce::jmin (u, master->numVoices - 1);   // pair each unison voice with one in osc 1

            // Hard sync: when osc 1 starts a new cycle, osc 2 is forced to restart too
            if (sync && master != nullptr && master->wrapped[mu])
                ph = master->wrapFraction[mu] * inc;

            // FM (phase modulation): osc 1's output pushes osc 2's phase back and forth
            double readPh = ph;
            if (fm > 0.0f && master != nullptr)
                readPh += (double) (fm * master->lastOut[mu]);
            readPh -= std::floor (readPh);

            float value = 0.0f;
            if (o.mode == oscAnalog)
            {
                value = analogValue (readPh, juce::jlimit (1.0e-6, 0.5, inc), o.wave, o.pulseWidth);
            }
            else if (o.frameA != nullptr)
            {
                const double idx = readPh * (double) wtSize;
                const int i = juce::jmin ((int) idx, wtSize - 1);
                const float t = (float) (idx - (double) i);
                const float a = o.frameA[i] + t * (o.frameA[i + 1] - o.frameA[i]);
                const float b = o.frameB[i] + t * (o.frameB[i + 1] - o.frameB[i]);
                value = a + o.frameMix * (b - a);
            }

            o.lastOut[uu] = value;
            mono += value;
            left  += value * o.panL[uu] * o.gain;
            right += value * o.panR[uu] * o.gain;

            ph += inc;
            o.wrapped[uu] = ph >= 1.0;
            if (o.wrapped[uu])
            {
                ph -= std::floor (ph);
                o.wrapFraction[uu] = ph / juce::jmax (1.0e-9, inc);   // samples since the wrap, for sync
            }
            o.phase[uu] = ph;
        }
        return mono;
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

        const auto& P = *shared.params;
        const int outChannels = output.getNumChannels();
        constexpr float halfPi = juce::MathConstants<float>::halfPi;

        while (numSamples > 0)
        {
            const int n = juce::jmin (numSamples, controlBlockSize);
            computeModulation (n);
            updateControl (n);

            const bool  sync = P.osc2Sync->load() > 0.5f;
            const float fm   = v (P.osc2Fm) * v (P.osc2Fm) * 2.0f;   // up to 2 cycles of phase push
            auto& osc1 = oscs[0];
            auto& osc2 = oscs[1];
            const bool osc2Running = osc2.on || osc2.gain > 0.0f;
            const bool runOsc1 = osc1.on || osc1.gain > 0.0f || (osc2Running && (sync || fm > 0.0f));

            for (int i = 0; i < n; ++i)
            {
                float left = 0.0f, right = 0.0f;

                // --- Sample layers (each with 1-4 unison copies) ---
                for (auto& ls : layers)
                {
                    ls.gain += ls.gainStep;
                    if (ls.data == nullptr)
                        continue;

                    const auto& buf = ls.reversed ? ls.data->reversed : ls.data->audio;
                    const double loopLen = ls.loopEnd - ls.loopStart;
                    const bool looping = ls.loopMode != loopOff && loopLen >= 64.0
                                      && ! (ls.loopMode == loopRelease && keyReleased);

                    for (int c = 0; c < ls.numCopies; ++c)
                    {
                        auto& r = ls.copies[(size_t) c];
                        if (! r.active)
                            continue;
                        const double p = r.position;

                        float l = readSample (buf, 0, p);
                        float rr = readSample (buf, 1, p);   // mono files reuse channel 0

                        // Loop crossfade: as we approach the loop end, fade in the audio
                        // from just before the loop start. When we jump back, the two
                        // match exactly - no click.
                        if (looping && ls.loopMode != loopPingPong && ls.crossfade >= 1.0
                            && p >= ls.loopEnd - ls.crossfade && p < ls.loopEnd)
                        {
                            const float t = (float) ((p - (ls.loopEnd - ls.crossfade)) / ls.crossfade);
                            const float fadeOut = std::cos (t * halfPi), fadeIn = std::sin (t * halfPi);
                            const double p2 = p - loopLen;
                            l  = l  * fadeOut + readSample (buf, 0, p2) * fadeIn;
                            rr = rr * fadeOut + readSample (buf, 1, p2) * fadeIn;
                        }

                        left  += l  * ls.gain * r.panL;
                        right += rr * ls.gain * r.panR;

                        // Move the read position, handling loops
                        r.position += ls.increment * r.ratio * (double) r.direction;
                        if (looping)
                        {
                            if (ls.loopMode == loopPingPong)
                            {
                                if (r.direction > 0 && r.position >= ls.loopEnd && p < ls.loopEnd)
                                {
                                    r.position = juce::jmax (ls.loopStart, 2.0 * ls.loopEnd - r.position);
                                    r.direction = -1;
                                }
                                else if (r.direction < 0 && r.position <= ls.loopStart)
                                {
                                    r.position = juce::jmin (ls.loopEnd, 2.0 * ls.loopStart - r.position);
                                    r.direction = 1;
                                }
                            }
                            else if (r.position >= ls.loopEnd && p < ls.loopEnd)
                            {
                                r.position = ls.loopStart + std::fmod (r.position - ls.loopStart, loopLen);
                            }
                        }

                        if ((r.direction > 0 && r.position >= ls.regionEnd) || r.position < 0.0)
                            r.active = false;   // reached the end marker
                    }
                }

                // --- Oscillators (osc 1 first: osc 2 may sync to it or be FM'd by it) ---
                if (runOsc1)     renderOsc (osc1, left, right, nullptr, 0.0f, false);
                if (osc2Running) renderOsc (osc2, left, right, &osc1, fm, sync);

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
                if (subPhase >= 1.0) subPhase -= std::floor (subPhase);

                // --- Noise ---
                noiseGain += noiseStep;
                if (noiseGain > 0.0001f)
                {
                    const float white = random.nextFloat() * 2.0f - 1.0f;
                    noiseLp += (white - noiseLp) * noiseLpCoef;
                    const float s = noiseHpMix < 0.0f ? noiseLp : white - noiseLp * noiseHpMix;
                    left  += s * noiseGain * 0.5f * subPanL;
                    right += s * noiseGain * 0.5f * subPanR;
                }

                // --- Filters (serial: 1 then 2; parallel: both, averaged), then the amp envelope ---
                {
                    auto run = [this] (int f, int ch, float x)
                    {
                        const float wet = filters[(size_t) f].process (ch, x);
                        return x + filterMix[(size_t) f] * (wet - x);   // Mix: dry <-> filtered
                    };
                    const bool on1 = filters[0].isActive(), on2 = filters[1].isActive();
                    if (filterRouting == routeSerial || ! (on1 && on2))
                    {
                        if (on1) { left = run (0, 0, left); right = run (0, 1, right); }
                        if (on2) { left = run (1, 0, left); right = run (1, 1, right); }
                    }
                    else
                    {
                        left  = 0.5f * (run (0, 0, left)  + run (1, 0, left));
                        right = 0.5f * (run (0, 1, right) + run (1, 1, right));
                    }
                }

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

            // Stop when the amp envelope has finished, or nothing can make sound any more
            bool anySound = osc1.on || osc2.on || subGain > 0.0001f || subStep > 0.0f
                         || noiseGain > 0.0001f || noiseStep > 0.0f;
            for (const auto& ls : layers)
                anySound = anySound || (ls.data != nullptr && ls.anyActive());
            if (! envs[0].isActive() || ! anySound)
            {
                finishNote();
                return;
            }

            startSample += n;
            numSamples  -= n;
        }

        publishPlayheads();
    }

    //==========================================================================
    //  The synth: Poly / Mono / Legato
    //==========================================================================
    int RefractSynth::voiceMode() const
    {
        return shared != nullptr ? (int) shared->params->voiceMode->load() : voicePoly;
    }

    void RefractSynth::noteOn (int midiChannel, int midiNoteNumber, float velocity)
    {
        lastVelocity = velocity;
        const int mode = voiceMode();
        if (shared != nullptr)
        {
            shared->overlapping = shared->keysDown > 0;   // for "Legato Only" glide
            ++shared->keysDown;
        }

        if (mode == voicePoly || getNumVoices() == 0 || getNumSounds() == 0)
        {
            juce::Synthesiser::noteOn (midiChannel, midiNoteNumber, velocity);
        }
        else
        {
            // Mono / Legato: one voice, which changes pitch as keys come and go.
            // Held keys are remembered so releasing one returns to the previous.
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), midiNoteNumber), heldNotes.end());
            if (heldNotes.size() < heldNotes.capacity())
                heldNotes.push_back (midiNoteNumber);

            for (int i = 1; i < getNumVoices(); ++i)   // let any notes left over from Poly fade out
                if (getVoice (i)->isVoiceActive() && getVoice (i)->isKeyDown())
                    getVoice (i)->stopNote (0.0f, true);

            auto* voice = dynamic_cast<RefractVoice*> (getVoice (0));
            auto* sound = getSound (0).get();
            if (voice != nullptr && sound != nullptr)
            {
                // A voice that's still sounding changes pitch rather than restarting.
                // Mono restarts envelopes and samples; Legato only does so if the
                // previous key was already released.
                if (voice->isVoiceActive())
                    voice->prepareLegato (mode == voiceMono || ! voice->isKeyDown());
                startVoice (voice, sound, midiChannel, midiNoteNumber, velocity);
            }
        }

        if (shared != nullptr)
            shared->lastNote = (float) midiNoteNumber;
    }

    void RefractSynth::noteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff)
    {
        if (shared != nullptr)
            shared->keysDown = juce::jmax (0, shared->keysDown - 1);

        const int mode = voiceMode();
        if (mode != voicePoly && getNumVoices() > 0 && getNumSounds() > 0)
        {
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), midiNoteNumber), heldNotes.end());

            auto* voice = dynamic_cast<RefractVoice*> (getVoice (0));
            if (voice != nullptr && voice->isVoiceActive() && voice->getCurrentlyPlayingNote() == midiNoteNumber
                && ! heldNotes.empty())
            {
                // Go back to the most recent key still held
                const int previous = heldNotes.back();
                voice->prepareLegato (mode == voiceMono);
                if (shared != nullptr) shared->overlapping = true;
                startVoice (voice, getSound (0).get(), midiChannel, previous, lastVelocity);
                if (shared != nullptr) shared->lastNote = (float) previous;
                return;
            }
        }

        juce::Synthesiser::noteOff (midiChannel, midiNoteNumber, velocity, allowTailOff);
    }

    void RefractSynth::allNotesOff (int midiChannel, bool allowTailOff)
    {
        heldNotes.clear();
        if (shared != nullptr)
            shared->keysDown = 0;
        juce::Synthesiser::allNotesOff (midiChannel, allowTailOff);
    }
}
