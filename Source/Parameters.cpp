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
            if (unit == "s")  return v < 1.0f ? juce::String (v * 1000.0f, 0) + " ms" : juce::String (v, 2) + " s";
            if (unit == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, v < 10.0f ? 2 : 0) + " Hz";
            const int decimals = span >= 100.0f ? 0 : (span >= 10.0f ? 1 : 2);
            return juce::String (v, decimals) + (unit.isEmpty() ? juce::String() : " " + unit);
        }

        float parseValue (const juce::String& text, const juce::String& unit)
        {
            const float v = text.getFloatValue();
            if (unit == "s"  && text.containsIgnoreCase ("ms")) return v * 0.001f;
            if (unit == "Hz" && text.containsIgnoreCase ("k"))  return v * 1000.0f;
            return v;
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

        void addEnv (Layout& l, const juce::String& prefix, const juce::String& name,
                     float a, float d, float s, float r)
        {
            addFloat (l, prefix + "_a", name + " Attack",  skewed (0.001f, 10.0f, 0.5f), a, "s");
            addFloat (l, prefix + "_d", name + " Decay",   skewed (0.001f, 10.0f, 0.5f), d, "s");
            addFloat (l, prefix + "_s", name + " Sustain", { 0.0f, 1.0f }, s);
            addFloat (l, prefix + "_r", name + " Release", skewed (0.001f, 15.0f, 0.8f), r, "s");
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        Layout l;

        // --- Sample layers -------------------------------------------------
        for (int i = 0; i < numLayers; ++i)
        {
            const auto n = "Layer " + juce::String (i + 1) + " ";
            l.add (std::make_unique<juce::AudioParameterBool> (pid (ids::layer (i, "on")), n + "On", true));
            addFloat (l, ids::layer (i, "level"),  n + "Level",  { 0.0f, 1.0f }, 0.8f);
            l.add (std::make_unique<juce::AudioParameterInt> (pid (ids::layer (i, "coarse")), n + "Coarse", -24, 24, 0));
            addFloat (l, ids::layer (i, "fine"),   n + "Fine",   { -100.0f, 100.0f, 1.0f }, 0.0f, "ct");
            addFloat (l, ids::layer (i, "pan"),    n + "Pan",    { -1.0f, 1.0f }, 0.0f);
            addFloat (l, ids::layer (i, "start"),  n + "Start",  { 0.0f, 1.0f }, 0.0f);
            l.add (std::make_unique<juce::AudioParameterBool> (pid (ids::layer (i, "loop")), n + "Loop", false));
        }

        // --- Sub oscillator -----------------------------------------------
        addFloat  (l, "sub_level",  "Sub Level", { 0.0f, 1.0f }, 0.0f);
        addChoice (l, "sub_octave", "Sub Octave", subOctaveNames(), 1);
        addChoice (l, "sub_wave",   "Sub Wave",   subWaveNames(),   0);

        // --- Envelopes -----------------------------------------------------
        addEnv (l, "amp",  "Amp",     0.005f, 0.3f, 1.0f, 0.4f);
        addEnv (l, "env2", "Mod Env", 0.01f,  0.5f, 0.0f, 0.5f);

        // --- LFOs -----------------------------------------------------------
        for (int i = 0; i < 2; ++i)
        {
            const auto p = "lfo" + juce::String (i + 1);
            const auto n = "LFO " + juce::String (i + 1);
            addFloat  (l, p + "_rate",  n + " Rate", skewed (0.01f, 20.0f, 2.0f), i == 0 ? 4.0f : 0.5f, "Hz");
            addChoice (l, p + "_shape", n + " Shape", lfoShapeNames(), 0);
        }

        // --- Filter --------------------------------------------------------
        addChoice (l, "flt_type",   "Filter Type", filterTypeNames(), 0);
        addFloat  (l, "flt_cutoff", "Cutoff", skewed (20.0f, 20000.0f, 1000.0f), 20000.0f, "Hz");
        addFloat  (l, "flt_res",    "Resonance", { 0.0f, 1.0f }, 0.1f);

        // --- Mod matrix ----------------------------------------------------
        // Slots come pre-routed to common destinations with amount 0, so you
        // only have to turn the amount knob to hear something.
        const int defaultSrc[numModSlots] = { srcModEnv, srcLfo1, srcModWheel, srcNone };
        const int defaultDst[numModSlots] = { dstCutoff, dstPitch, dstCutoff,  dstNone };
        for (int s = 0; s < numModSlots; ++s)
        {
            const auto n = "Mod " + juce::String (s + 1) + " ";
            addChoice (l, ids::mod (s, "src"), n + "Source", modSourceNames(), defaultSrc[s]);
            addChoice (l, ids::mod (s, "dst"), n + "Dest",   modDestNames(),   defaultDst[s]);
            addFloat  (l, ids::mod (s, "amt"), n + "Amount", { -1.0f, 1.0f }, 0.0f);
        }

        // --- Effects -------------------------------------------------------
        addFloat (l, "dly_time", "Delay Time",     skewed (10.0f, 2000.0f, 350.0f), 375.0f, "ms");
        addFloat (l, "dly_fb",   "Delay Feedback", { 0.0f, 0.95f }, 0.35f);
        addFloat (l, "dly_mix",  "Delay Mix",      { 0.0f, 1.0f },  0.0f);
        addFloat (l, "rev_size", "Reverb Size",    { 0.0f, 1.0f },  0.6f);
        addFloat (l, "rev_damp", "Reverb Damping", { 0.0f, 1.0f },  0.4f);
        addFloat (l, "rev_mix",  "Reverb Mix",     { 0.0f, 1.0f },  0.15f);
        addFloat (l, "master",   "Master",         { -36.0f, 6.0f, 0.1f }, -6.0f, "dB");

        return l;
    }

    ParamRefs::ParamRefs (juce::AudioProcessorValueTreeState& s)
    {
        auto get = [&s] (const juce::String& id)
        {
            auto* p = s.getRawParameterValue (id);
            jassert (p != nullptr);   // a typo in an ID would land here in a Debug build
            return p;
        };

        for (int i = 0; i < numLayers; ++i)
            layers[(size_t) i] = { get (ids::layer (i, "on")),   get (ids::layer (i, "level")),
                                   get (ids::layer (i, "coarse")), get (ids::layer (i, "fine")),
                                   get (ids::layer (i, "pan")),  get (ids::layer (i, "start")),
                                   get (ids::layer (i, "loop")) };

        subLevel  = get ("sub_level");
        subOctave = get ("sub_octave");
        subWave   = get ("sub_wave");

        ampEnv = { get ("amp_a"),  get ("amp_d"),  get ("amp_s"),  get ("amp_r") };
        modEnv = { get ("env2_a"), get ("env2_d"), get ("env2_s"), get ("env2_r") };

        lfos[0] = { get ("lfo1_rate"), get ("lfo1_shape") };
        lfos[1] = { get ("lfo2_rate"), get ("lfo2_shape") };

        filterType = get ("flt_type");
        cutoff     = get ("flt_cutoff");
        resonance  = get ("flt_res");

        for (int m = 0; m < numModSlots; ++m)
            mods[(size_t) m] = { get (ids::mod (m, "src")), get (ids::mod (m, "dst")), get (ids::mod (m, "amt")) };

        delayTime     = get ("dly_time");
        delayFeedback = get ("dly_fb");
        delayMix      = get ("dly_mix");
        reverbSize    = get ("rev_size");
        reverbDamping = get ("rev_damp");
        reverbMix     = get ("rev_mix");
        masterGain    = get ("master");
    }
}
