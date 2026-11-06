#include "Parameters.h"
#include "Wavetable.h"
#include "Filters.h"
#include "Effects.h"

namespace refract
{
    namespace
    {
        using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

        // ParameterID's second argument is a "version hint". AU needs it; bump it
        // only for parameters you add in a later version of the plugin.
        juce::ParameterID pid (const juce::String& id) { return { id, 1 }; }

        // A range whose middle position sits at `centre` - makes time and frequency
        // knobs feel natural (most of the travel covers the useful small values).
        juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
        {
            juce::NormalisableRange<float> r (lo, hi);
            r.setSkewForCentre (centre);
            return r;
        }

        // How a value is shown as text (in the plugin and in Ableton's device view),
        // and how typed text is turned back into a value.
        juce::String formatValue (float v, float span, const juce::String& unit)
        {
            if (unit == "s")   return v < 1.0f ? juce::String (v * 1000.0f, 0) + " ms" : juce::String (v, 2) + " s";
            if (unit == "Hz")  return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, v < 10.0f ? 2 : 0) + " Hz";
            if (unit == "%")   return juce::String (v * 100.0f, 1) + " %";
            if (unit == "deg") return juce::String (v * 360.0f, 0) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
            if (unit == "x")   return juce::String (v, v < 1.0f ? 3 : 2) + " x";
            const int decimals = span >= 100.0f ? 0 : (span >= 10.0f ? 1 : 2);
            return juce::String (v, decimals) + (unit.isEmpty() ? juce::String() : " " + unit);
        }

        float parseValue (const juce::String& text, const juce::String& unit)
        {
            const float v = text.getFloatValue();
            if (unit == "s"   && text.containsIgnoreCase ("ms")) return v * 0.001f;
            if (unit == "Hz"  && text.containsIgnoreCase ("k"))  return v * 1000.0f;
            if (unit == "%")                                      return v * 0.01f;
            if (unit == "deg")                                    return v / 360.0f;
            return v;
        }

        // "C3", "F#2", "Bb4" or a plain MIDI number -> MIDI note number (C3 = 60)
        int parseNoteName (const juce::String& text)
        {
            const auto t = text.trim().toUpperCase();
            if (t.isEmpty()) return 60;
            if (t.containsOnly ("0123456789")) return juce::jlimit (0, 127, t.getIntValue());

            const int semis[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
            const auto letter = t[0];
            if (letter < 'A' || letter > 'G') return 60;
            int note = semis[letter - 'A'];
            int i = 1;
            if (t[i] == '#') { ++note; ++i; }
            else if (t[i] == 'B' && t.length() > 2) { --note; ++i; }
            const int octave = t.substring (i).getIntValue();   // handles "-1"
            return juce::jlimit (0, 127, (octave + 2) * 12 + note);
        }

        void addFloat (Layout& l, const juce::String& id, const juce::String& name,
                       juce::NormalisableRange<float> range, float def, const juce::String& unit = {})
        {
            const float span = range.end - range.start;
            auto attributes = juce::AudioParameterFloatAttributes()
                                  .withStringFromValueFunction ([span, unit] (float v, int) { return formatValue (v, span, unit); })
                                  .withValueFromStringFunction ([unit] (const juce::String& t) { return parseValue (t, unit); });
            l.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, range, def, attributes));
        }

        void addChoice (Layout& l, const juce::String& id, const juce::String& name,
                        const juce::StringArray& choices, int def)
        {
            l.add (std::make_unique<juce::AudioParameterChoice> (pid (id), name, choices, def));
        }

        void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def)
        {
            l.add (std::make_unique<juce::AudioParameterBool> (pid (id), name, def));
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        Layout l;

        // --- Sample layers -------------------------------------------------
        for (int i = 0; i < numLayers; ++i)
        {
            const auto n = "Layer " + juce::String (i + 1) + " ";
            addBool  (l, ids::layer (i, "on"), n + "On", true);
            addFloat (l, ids::layer (i, "level"),  n + "Level",  { 0.0f, 1.0f }, 0.8f);
            addFloat (l, ids::layer (i, "pan"),    n + "Pan",    { -1.0f, 1.0f }, 0.0f);

            // Tuning: the root note is the key at which the sample plays at its
            // original pitch. Shown as a note name ("C3" = MIDI 60, Ableton style).
            l.add (std::make_unique<juce::AudioParameterInt> (pid (ids::layer (i, "root")), n + "Root Note", 0, 127, 60,
                       juce::AudioParameterIntAttributes()
                           .withStringFromValueFunction ([] (int v, int) { return juce::MidiMessage::getMidiNoteName (v, true, true, 3); })
                           .withValueFromStringFunction ([] (const juce::String& t) { return parseNoteName (t); })));
            l.add (std::make_unique<juce::AudioParameterInt> (pid (ids::layer (i, "coarse")), n + "Transpose", -48, 48, 0,
                       juce::AudioParameterIntAttributes().withLabel ("st")));
            addFloat (l, ids::layer (i, "fine"),   n + "Fine",   { -100.0f, 100.0f, 0.1f }, 0.0f, "ct");

            // Playback region and loop, as fractions of the sample length
            addFloat  (l, ids::layer (i, "start"),     n + "Sample Start", { 0.0f, 1.0f }, 0.0f, "%");
            addFloat  (l, ids::layer (i, "end"),       n + "Sample End",   { 0.0f, 1.0f }, 1.0f, "%");
            addChoice (l, ids::layer (i, "loopmode"),  n + "Loop Mode",    loopModeNames(), loopOff);
            addFloat  (l, ids::layer (i, "loopstart"), n + "Loop Start",   { 0.0f, 1.0f }, 0.0f, "%");
            addFloat  (l, ids::layer (i, "loopend"),   n + "Loop End",     { 0.0f, 1.0f }, 1.0f, "%");
            addFloat  (l, ids::layer (i, "xfade"),     n + "Loop Crossfade", { 0.0f, 0.5f }, 0.0f, "%");
            addBool   (l, ids::layer (i, "reverse"),   n + "Reverse", false);

            // Unison: up to 4 slightly detuned copies of the sample, spread in stereo
            l.add (std::make_unique<juce::AudioParameterInt> (pid (ids::layer (i, "unison")), n + "Unison", 1, maxLayerUnison, 1));
            addFloat  (l, ids::layer (i, "detune"),    n + "Unison Detune", { 0.0f, 1.0f }, 0.2f);
            addFloat  (l, ids::layer (i, "spread"),    n + "Unison Spread", { 0.0f, 1.0f }, 0.5f);
        }

        // --- Oscillators ---------------------------------------------------
        for (int o = 0; o < numOscs; ++o)
        {
            const auto n = "Osc " + juce::String (o + 1) + " ";
            addBool   (l, ids::osc (o, "on"),    n + "On", false);
            addChoice (l, ids::osc (o, "mode"),  n + "Mode", oscModeNames(), oscAnalog);
            // Analog: Wave morphs sine (0) -> triangle (1) -> saw (2) -> square (3)
            addFloat  (l, ids::osc (o, "wave"),  n + "Wave", { 0.0f, 3.0f }, 2.0f);
            addFloat  (l, ids::osc (o, "pw"),    n + "Pulse Width", { 0.05f, 0.95f }, 0.5f, "%");
            // Wavetable: which table, and where in it
            addChoice (l, ids::osc (o, "table"), n + "Wavetable", wavetableNames(), 0);
            addFloat  (l, ids::osc (o, "pos"),   n + "Position", { 0.0f, 1.0f }, 0.0f, "%");
            l.add (std::make_unique<juce::AudioParameterInt> (pid (ids::osc (o, "oct")),  n + "Octave",   -3, 3, 0));
            l.add (std::make_unique<juce::AudioParameterInt> (pid (ids::osc (o, "semi")), n + "Semitone", -12, 12, 0,
                       juce::AudioParameterIntAttributes().withLabel ("st")));
            addFloat  (l, ids::osc (o, "fine"),  n + "Fine", { -100.0f, 100.0f, 0.1f }, 0.0f, "ct");
            addFloat  (l, ids::osc (o, "level"), n + "Level", { 0.0f, 1.0f }, 0.7f);
            addFloat  (l, ids::osc (o, "pan"),   n + "Pan", { -1.0f, 1.0f }, 0.0f);
            l.add (std::make_unique<juce::AudioParameterInt> (pid (ids::osc (o, "unison")), n + "Unison", 1, maxUnison, 1));
            addFloat  (l, ids::osc (o, "detune"), n + "Unison Detune", { 0.0f, 1.0f }, 0.25f);
            addFloat  (l, ids::osc (o, "spread"), n + "Unison Spread", { 0.0f, 1.0f }, 0.7f);
            addFloat  (l, ids::osc (o, "rphase"), n + "Random Phase", { 0.0f, 1.0f }, 1.0f);
        }
        addBool  (l, "osc2_sync", "Osc 2 Hard Sync", false);
        addFloat (l, "osc2_fm",   "Osc 2 FM Amount", { 0.0f, 1.0f }, 0.0f);

        // --- Noise ------------------------------------------------------------
        addFloat (l, "noise_level",  "Noise Level",  { 0.0f, 1.0f }, 0.0f);
        addFloat (l, "noise_colour", "Noise Colour", { -1.0f, 1.0f }, 0.0f);

        // --- Voice mode and glide ------------------------------------------
        addChoice (l, "voice_mode", "Voice Mode", voiceModeNames(), voicePoly);
        addChoice (l, "glide_mode", "Glide",      glideModeNames(), glideOff);
        addFloat  (l, "glide_time", "Glide Time", skewed (0.001f, 5.0f, 0.3f), 0.1f, "s");

        // --- Sub oscillator -----------------------------------------------
        addFloat  (l, "sub_level",  "Sub Level", { 0.0f, 1.0f }, 0.0f);
        addChoice (l, "sub_octave", "Sub Octave", juce::StringArray { "-2 Oct", "-1 Oct", "0 Oct" }, 1);
        addChoice (l, "sub_wave",   "Sub Wave",   juce::StringArray { "Sine", "Triangle", "Square" }, 0);

        // --- Voice (whole-note pitch, pan and level - handy mod targets) ---
        addFloat (l, "voice_pitch", "Pitch", { -24.0f, 24.0f, 0.01f }, 0.0f, "st");
        addFloat (l, "voice_pan",   "Pan",   { -1.0f, 1.0f }, 0.0f);
        addFloat (l, "voice_level", "Voice Level", { 0.0f, 1.0f }, 1.0f);

        // --- Envelopes (1 = amp) -----------------------------------------------
        for (int e = 0; e < numEnvs; ++e)
        {
            const auto n = e == 0 ? juce::String ("Amp Env") : "Env " + juce::String (e + 1);
            const bool amp = e == 0;
            addFloat (l, ids::env (e, "a"), n + " Attack",  skewed (0.001f, 10.0f, 0.5f), amp ? 0.005f : 0.01f, "s");
            addFloat (l, ids::env (e, "d"), n + " Decay",   skewed (0.001f, 10.0f, 0.5f), amp ? 0.3f : 0.5f, "s");
            addFloat (l, ids::env (e, "s"), n + " Sustain", { 0.0f, 1.0f }, amp ? 1.0f : 0.0f);
            addFloat (l, ids::env (e, "r"), n + " Release", skewed (0.001f, 15.0f, 0.8f), amp ? 0.4f : 0.5f, "s");
        }

        // --- LFOs -----------------------------------------------------------
        for (int i = 0; i < numLfos; ++i)
        {
            const auto n = "LFO " + juce::String (i + 1) + " ";
            addChoice (l, ids::lfo (i, "shape"),  n + "Shape",     lfoShapeNames(), lfoSine);
            addChoice (l, ids::lfo (i, "mode"),   n + "Rate Mode", lfoRateModeNames(), rateHz);
            addFloat  (l, ids::lfo (i, "rate"),   n + "Rate",      skewed (0.01f, 40.0f, 2.0f), i == 0 ? 5.0f : 1.0f, "Hz");
            addChoice (l, ids::lfo (i, "sync"),   n + "Sync Rate", syncNames(), 7);   // 1/4
            addFloat  (l, ids::lfo (i, "ratio"),  n + "Freq Ratio", skewed (0.125f, 16.0f, 1.0f), 1.0f, "x");
            addFloat  (l, ids::lfo (i, "depth"),  n + "Depth",     { 0.0f, 1.0f }, 1.0f);
            addFloat  (l, ids::lfo (i, "phase"),  n + "Phase",     { 0.0f, 1.0f }, 0.0f, "deg");
            addFloat  (l, ids::lfo (i, "jitter"), n + "Jitter",    { 0.0f, 1.0f }, 0.0f);
            addFloat  (l, ids::lfo (i, "smooth"), n + "Smooth",    { 0.0f, 1.0f }, 0.0f);
            addBool   (l, ids::lfo (i, "retrig"), n + "Retrigger", true);
        }

        // --- Filters (two per voice, serial or parallel) ------------------------
        for (int f = 0; f < 2; ++f)
        {
            const auto n = "Filter " + juce::String (f + 1) + " ";
            addChoice (l, ids::filter (f, "type"),   n + "Type", filterModelNames(), f == 0 ? fltSvfLow : fltOff);
            addFloat  (l, ids::filter (f, "cutoff"), n + "Cutoff", skewed (20.0f, 20000.0f, 1000.0f), f == 0 ? 20000.0f : 1000.0f, "Hz");
            addFloat  (l, ids::filter (f, "res"),    n + "Resonance", { 0.0f, 1.0f }, 0.1f);
            addFloat  (l, ids::filter (f, "drive"),  n + "Drive", { 0.0f, 1.0f }, 0.0f);
            addFloat  (l, ids::filter (f, "key"),    n + "Key Track", { 0.0f, 1.0f }, 0.0f, "%");
            addFloat  (l, ids::filter (f, "env"),    n + "Env 2 Amount", { -1.0f, 1.0f }, 0.0f);
            addFloat  (l, ids::filter (f, "shape"),  n + "Shape", { 0.0f, 1.0f }, 0.0f);
            addFloat  (l, ids::filter (f, "mix"),    n + "Mix", { 0.0f, 1.0f }, 1.0f, "%");
        }
        addChoice (l, "flt_routing", "Filter Routing", filterRoutingNames(), routeSerial);

        // --- Mixer: where each sound goes ---------------------------------------
        for (int s = 0; s < numMixSources; ++s)
        {
            const auto n = mixSourceNames()[s] + " ";
            addChoice (l, ids::mix (s, "route"), n + "Filter Route", mixRouteNames(), routeFilter1);
            addFloat  (l, ids::mix (s, "sendA"), n + "Send A", { 0.0f, 1.0f }, 0.0f, "%");
            addFloat  (l, ids::mix (s, "sendB"), n + "Send B", { 0.0f, 1.0f }, 0.0f, "%");
        }
        addFloat (l, "mix_sub_pan",     "Sub Pan",   { -1.0f, 1.0f }, 0.0f);
        addFloat (l, "mix_noise_pan",   "Noise Pan", { -1.0f, 1.0f }, 0.0f);
        addFloat (l, "mix_voice_sendA", "Voice Send A (post-filter)", { 0.0f, 1.0f }, 0.0f, "%");
        addFloat (l, "mix_voice_sendB", "Voice Send B (post-filter)", { 0.0f, 1.0f }, 0.0f, "%");

        // --- FX buses: 2 buses x 3 slots ------------------------------------------
        for (int b = 0; b < numBuses; ++b)
        {
            const auto bn = juce::String (b == 0 ? "Bus A " : "Bus B ");
            addFloat (l, ids::bus (b) + "_return", bn + "Return", { 0.0f, 1.0f }, 0.8f, "%");
            for (int s = 0; s < numFxSlots; ++s)
            {
                const auto n = bn + "FX " + juce::String (s + 1) + " ";
                // Sensible starting chain: Bus A = delay, Bus B = reverb
                const int defType = s == 0 ? (b == 0 ? fxDelay : fxReverb) : fxOff;
                addChoice (l, ids::fx (b, s, "type"),  n + "Type", fxTypeNames(), defType);
                addFloat  (l, ids::fx (b, s, "p1"),    n + "P1", { 0.0f, 1.0f }, 0.5f, "%");
                addFloat  (l, ids::fx (b, s, "p2"),    n + "P2", { 0.0f, 1.0f }, 0.4f, "%");
                addFloat  (l, ids::fx (b, s, "p3"),    n + "P3", { 0.0f, 1.0f }, 0.7f, "%");
                addFloat  (l, ids::fx (b, s, "p4"),    n + "P4", { 0.0f, 1.0f }, 0.3f, "%");
                addFloat  (l, ids::fx (b, s, "mix"),   n + "Mix", { 0.0f, 1.0f }, 1.0f, "%");
                addChoice (l, ids::fx (b, s, "dmode"), n + "Delay Mode", delayModeNames(), delayDigital);
                addBool   (l, ids::fx (b, s, "sync"),  n + "Delay Sync", true);
                addChoice (l, ids::fx (b, s, "div"),   n + "Delay Division", syncNames(), 8);   // 1/4 D
            }
        }
        addFloat (l, "master",   "Master",         { -36.0f, 6.0f, 0.1f }, -6.0f, "dB");

        // --- Macros: 4 free knobs to map in Live and use as mod sources ---
        for (int m = 0; m < numMacros; ++m)
            addFloat (l, ids::macro (m), "Macro " + juce::String (m + 1), { 0.0f, 1.0f }, 0.0f);

        // --- Mod matrix amounts (routing itself is saved separately) -----------
        for (int s = 0; s < numModSlots; ++s)
            addFloat (l, ids::modAmount (s), "Mod " + juce::String (s + 1) + " Amount", { -1.0f, 1.0f }, 0.0f);

        return l;
    }

    //==========================================================================
    namespace
    {
        // Which section a parameter belongs to, for the grouped destination menu
        juce::String groupFor (const juce::String& id)
        {
            for (int i = 0; i < numLayers; ++i)
                if (id.startsWith ("l" + juce::String (i + 1) + "_")) return "Layer " + juce::String (i + 1);
            if (id.startsWith ("sub_"))   return "Sub";
            if (id.startsWith ("osc1_"))  return "Osc 1";
            if (id.startsWith ("osc2_"))  return "Osc 2";
            if (id.startsWith ("noise_")) return "Noise";
            if (id.startsWith ("glide_")) return "Voice";
            if (id.startsWith ("voice_")) return "Voice";
            if (id.startsWith ("amp_"))   return "Amp Env";
            for (int e = 1; e < numEnvs; ++e)
                if (id.startsWith ("env" + juce::String (e + 1) + "_")) return "Env " + juce::String (e + 1);
            for (int i = 0; i < numLfos; ++i)
                if (id.startsWith ("lfo" + juce::String (i + 1) + "_")) return "LFO " + juce::String (i + 1);
            if (id.startsWith ("flt1_"))  return "Filter 1";
            if (id.startsWith ("flt2_"))  return "Filter 2";
            if (id.startsWith ("mix_"))   return "Mixer";
            if (id.startsWith ("busA_"))  return "Bus A";
            if (id.startsWith ("busB_"))  return "Bus B";
            return "Output";
        }

        bool isGlobalParam (const juce::String& id)
        {
            return id.startsWith ("bus") || id == "master";
        }
    }

    void ParamTable::build (juce::AudioProcessorValueTreeState& state, juce::AudioProcessor& processor)
    {
        targets.clear();
        for (auto* p : processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (ranged == nullptr) continue;

            // Only continuous knobs - switches and menus aren't modulated
            const bool continuous = dynamic_cast<juce::AudioParameterFloat*> (p) != nullptr
                                 || dynamic_cast<juce::AudioParameterInt*> (p) != nullptr;
            const auto id = ranged->getParameterID();
            if (! continuous || id.startsWith ("macro") || id.endsWith ("_amt"))
                continue;

            Target t;
            t.id = id;
            t.name = ranged->getName (64);
            t.group = groupFor (id);
            t.raw = state.getRawParameterValue (id);
            t.range = ranged->getNormalisableRange();
            t.param = ranged;
            t.global = isGlobalParam (id);
            targets.push_back (std::move (t));
        }
    }

    int ParamTable::indexOf (const juce::String& id) const
    {
        for (size_t i = 0; i < targets.size(); ++i)
            if (targets[i].id == id)
                return (int) i;
        return -1;
    }

    ParamRefs::ParamRefs (juce::AudioProcessorValueTreeState& s, const ParamTable& table)
    {
        auto raw = [&s] (const juce::String& id)
        {
            auto* p = s.getRawParameterValue (id);
            jassert (p != nullptr);   // a typo in an ID would land here in a Debug build
            return p;
        };
        auto idx = [&table] (const juce::String& id)
        {
            const int i = table.indexOf (id);
            jassert (i >= 0);
            return i;
        };

        for (int i = 0; i < numLayers; ++i)
        {
            auto& L = layers[(size_t) i];
            L.on        = raw (ids::layer (i, "on"));
            L.loopMode  = raw (ids::layer (i, "loopmode"));
            L.reverse   = raw (ids::layer (i, "reverse"));
            L.level     = idx (ids::layer (i, "level"));
            L.pan       = idx (ids::layer (i, "pan"));
            L.root      = idx (ids::layer (i, "root"));
            L.coarse    = idx (ids::layer (i, "coarse"));
            L.fine      = idx (ids::layer (i, "fine"));
            L.start     = idx (ids::layer (i, "start"));
            L.end       = idx (ids::layer (i, "end"));
            L.loopStart = idx (ids::layer (i, "loopstart"));
            L.loopEnd   = idx (ids::layer (i, "loopend"));
            L.crossfade = idx (ids::layer (i, "xfade"));
            L.unison    = idx (ids::layer (i, "unison"));
            L.detune    = idx (ids::layer (i, "detune"));
            L.spread    = idx (ids::layer (i, "spread"));
        }

        for (int o = 0; o < numOscs; ++o)
        {
            auto& O = oscs[(size_t) o];
            O.on          = raw (ids::osc (o, "on"));
            O.mode        = raw (ids::osc (o, "mode"));
            O.table       = raw (ids::osc (o, "table"));
            O.wave        = idx (ids::osc (o, "wave"));
            O.pulseWidth  = idx (ids::osc (o, "pw"));
            O.position    = idx (ids::osc (o, "pos"));
            O.octave      = idx (ids::osc (o, "oct"));
            O.semi        = idx (ids::osc (o, "semi"));
            O.fine        = idx (ids::osc (o, "fine"));
            O.level       = idx (ids::osc (o, "level"));
            O.pan         = idx (ids::osc (o, "pan"));
            O.unison      = idx (ids::osc (o, "unison"));
            O.detune      = idx (ids::osc (o, "detune"));
            O.spread      = idx (ids::osc (o, "spread"));
            O.randomPhase = idx (ids::osc (o, "rphase"));
        }
        osc2Sync    = raw ("osc2_sync");
        osc2Fm      = idx ("osc2_fm");
        noiseLevel  = idx ("noise_level");
        noiseColour = idx ("noise_colour");
        voiceMode   = raw ("voice_mode");
        glideMode   = raw ("glide_mode");
        glideTime   = idx ("glide_time");

        subLevel  = idx ("sub_level");
        subOctave = raw ("sub_octave");
        subWave   = raw ("sub_wave");

        for (int e = 0; e < numEnvs; ++e)
            envs[(size_t) e] = { idx (ids::env (e, "a")), idx (ids::env (e, "d")),
                                 idx (ids::env (e, "s")), idx (ids::env (e, "r")) };

        for (int i = 0; i < numLfos; ++i)
        {
            auto& L = lfos[(size_t) i];
            L.shape  = raw (ids::lfo (i, "shape"));
            L.mode   = raw (ids::lfo (i, "mode"));
            L.sync   = raw (ids::lfo (i, "sync"));
            L.retrig = raw (ids::lfo (i, "retrig"));
            L.rate   = idx (ids::lfo (i, "rate"));
            L.ratio  = idx (ids::lfo (i, "ratio"));
            L.depth  = idx (ids::lfo (i, "depth"));
            L.phase  = idx (ids::lfo (i, "phase"));
            L.jitter = idx (ids::lfo (i, "jitter"));
            L.smooth = idx (ids::lfo (i, "smooth"));
        }

        for (int f = 0; f < 2; ++f)
        {
            auto& F = filters[(size_t) f];
            F.type      = raw (ids::filter (f, "type"));
            F.cutoff    = idx (ids::filter (f, "cutoff"));
            F.resonance = idx (ids::filter (f, "res"));
            F.drive     = idx (ids::filter (f, "drive"));
            F.keyTrack  = idx (ids::filter (f, "key"));
            F.envAmount = idx (ids::filter (f, "env"));
            F.shape     = idx (ids::filter (f, "shape"));
            F.mix       = idx (ids::filter (f, "mix"));
        }
        filterRouting = raw ("flt_routing");

        pitch      = idx ("voice_pitch");
        pan        = idx ("voice_pan");
        voiceLevel = idx ("voice_level");

        for (int m = 0; m < numModSlots; ++m) modAmounts[(size_t) m] = raw (ids::modAmount (m));
        for (int m = 0; m < numMacros; ++m)   macros[(size_t) m]     = raw (ids::macro (m));

        for (int s = 0; s < numMixSources; ++s)
            mix[(size_t) s] = { raw (ids::mix (s, "route")), idx (ids::mix (s, "sendA")), idx (ids::mix (s, "sendB")) };
        subPan     = idx ("mix_sub_pan");
        noisePan   = idx ("mix_noise_pan");
        voiceSendA = idx ("mix_voice_sendA");
        voiceSendB = idx ("mix_voice_sendB");

        for (int b = 0; b < numBuses; ++b)
        {
            busReturn[(size_t) b] = idx (ids::bus (b) + "_return");
            for (int s = 0; s < numFxSlots; ++s)
            {
                auto& F = fx[(size_t) b][(size_t) s];
                F.type      = raw (ids::fx (b, s, "type"));
                F.delayMode = raw (ids::fx (b, s, "dmode"));
                F.sync      = raw (ids::fx (b, s, "sync"));
                F.division  = raw (ids::fx (b, s, "div"));
                F.p1  = idx (ids::fx (b, s, "p1"));
                F.p2  = idx (ids::fx (b, s, "p2"));
                F.p3  = idx (ids::fx (b, s, "p3"));
                F.p4  = idx (ids::fx (b, s, "p4"));
                F.mix = idx (ids::fx (b, s, "mix"));
            }
        }
        master = idx ("master");
    }
}
