#include "Parameters.h"

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
        }

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

        // --- Filter --------------------------------------------------------
        addChoice (l, "flt_type",   "Filter Type", juce::StringArray { "Low Pass", "Band Pass", "High Pass" }, 0);
        addFloat  (l, "flt_cutoff", "Cutoff", skewed (20.0f, 20000.0f, 1000.0f), 20000.0f, "Hz");
        addFloat  (l, "flt_res",    "Resonance", { 0.0f, 1.0f }, 0.1f);

        // --- Effects -------------------------------------------------------
        addFloat (l, "dly_time", "Delay Time",     skewed (10.0f, 2000.0f, 350.0f), 375.0f, "ms");
        addFloat (l, "dly_fb",   "Delay Feedback", { 0.0f, 0.95f }, 0.35f);
        addFloat (l, "dly_mix",  "Delay Mix",      { 0.0f, 1.0f },  0.0f);
        addFloat (l, "rev_size", "Reverb Size",    { 0.0f, 1.0f },  0.6f);
        addFloat (l, "rev_damp", "Reverb Damping", { 0.0f, 1.0f },  0.4f);
        addFloat (l, "rev_mix",  "Reverb Mix",     { 0.0f, 1.0f },  0.15f);
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
            if (id.startsWith ("voice_")) return "Voice";
            if (id.startsWith ("amp_"))   return "Amp Env";
            for (int e = 1; e < numEnvs; ++e)
                if (id.startsWith ("env" + juce::String (e + 1) + "_")) return "Env " + juce::String (e + 1);
            for (int i = 0; i < numLfos; ++i)
                if (id.startsWith ("lfo" + juce::String (i + 1) + "_")) return "LFO " + juce::String (i + 1);
            if (id.startsWith ("flt_"))   return "Filter";
            if (id.startsWith ("dly_"))   return "Delay";
            if (id.startsWith ("rev_"))   return "Reverb";
            return "Output";
        }

        bool isGlobalParam (const juce::String& id)
        {
            return id.startsWith ("dly_") || id.startsWith ("rev_") || id == "master";
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
        }

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

        filterType = raw ("flt_type");
        cutoff     = idx ("flt_cutoff");
        resonance  = idx ("flt_res");

        pitch      = idx ("voice_pitch");
        pan        = idx ("voice_pan");
        voiceLevel = idx ("voice_level");

        for (int m = 0; m < numModSlots; ++m) modAmounts[(size_t) m] = raw (ids::modAmount (m));
        for (int m = 0; m < numMacros; ++m)   macros[(size_t) m]     = raw (ids::macro (m));

        delayTime     = idx ("dly_time");
        delayFeedback = idx ("dly_fb");
        delayMix      = idx ("dly_mix");
        reverbSize    = idx ("rev_size");
        reverbDamping = idx ("rev_damp");
        reverbMix     = idx ("rev_mix");
        master        = idx ("master");
    }
}
