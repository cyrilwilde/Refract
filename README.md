# Refract: a spectral sample synth for Apple Silicon

Refract is an original instrument plugin (VST3, AU and Standalone) that takes the *idea* behind spectral sample synths like Iris: load sounds, paint away parts of their frequency spectrum, layer the results and play them across the keyboard. It is built from scratch on a modern foundation and runs natively on your M2. There's no Rosetta, and the heavy maths happens once per edit, never during playback.

**What it does**

- **3 sample layers + a sub oscillator.** Each layer has level, coarse/fine tuning, pan, start position and loop.
- **Spectral painting.** Each layer shows a spectrogram you paint on to remove (or restore) frequencies over time, with a soft brush or a rectangle tool.
- **Per-voice filter.** Low-, band- or high-pass with resonance.
- **Two envelopes.** One for amplitude, one for modulation.
- **Two LFOs.** Sine, triangle, saw, square and sample & hold.
- **4-slot mod matrix.** Route mod env, LFOs, velocity, mod wheel or key position to pitch, cutoff, resonance, layer levels, sub level, pan or amp.
- **Delay and reverb, plus master volume.**
- **16-voice polyphony and full automation in Ableton.** Your samples and paint strokes are saved with your Live set.

---

## Part 0: The easy way (GitHub builds a .pkg for you)

The file `.github/workflows/build-mac.yml` tells GitHub to compile Refract on one of its own Macs and package it as `Refract.pkg`, so you don't need Xcode or Terminal.

1. Create a free account at github.com. Click **+ → New repository**, name it `Refract`, choose **Public**, and click **Create repository**. Public repos get unlimited free Mac build time; private ones get a small monthly allowance.
2. On the new repo page, click **uploading an existing file**. Drag in everything *inside* the `Refract` folder: `CMakeLists.txt`, `README.md`, `Source` and `.github`. Then click **Commit changes**.
   - `.github` is hidden in Finder. Press **⌘ ⇧ .** (Cmd-Shift-full stop) in the folder to show it.
3. Open the **Actions** tab. A run called "Build Mac installer" starts automatically and takes about 10 minutes.
4. When it shows a green tick, click the run. Under **Artifacts**, download **Refract-installer**, then unzip it to get `Refract.pkg`.
5. Right-click `Refract.pkg` and choose **Open**. You need to do this rather than double-click because it isn't signed with a paid Apple Developer ID. Follow the installer. It puts the VST3 and AU in `/Library/Audio/Plug-Ins`.
6. In Live, open **Settings → Plug-Ins**, turn on **Use VST3 Plug-in System Folders**, and click **Rescan**.

If the run shows a red ✗, click it, open the step that failed, and copy the first `error:` line. That's the thing to fix.

Every time you upload changed files, GitHub builds a new `.pkg` automatically.

## Part 1: Building it on your Mac

You only do this setup once. You don't need it if you used Part 0.

### 1.1 Install the tools

1. **Xcode**: free from the Mac App Store. It's Apple's compiler and is what actually turns C++ into a plugin. Open it once after installing so it can finish setting up, and accept the licence.
2. **CMake**: the tool that reads `CMakeLists.txt` and generates an Xcode project. The easiest way to get it is [Homebrew](https://brew.sh). With Homebrew installed, run this in Terminal:
   ```bash
   brew install cmake
   ```
   (Or download the macOS installer from cmake.org and run `sudo "/Applications/CMake.app/Contents/bin/cmake-gui" --install`.)

JUCE, the C++ framework this plugin is built on, does **not** need installing. The build downloads it automatically the first time.

### 1.2 Build

Unzip the `Refract` folder somewhere sensible (e.g. `~/Developer/Refract`), then in Terminal:

```bash
cd ~/Developer/Refract
cmake -B build -G Xcode            # step 1: generate the Xcode project (downloads JUCE first time)
cmake --build build --config Release   # step 2: compile
```

What each command does:

- **`cmake -B build -G Xcode`** reads `CMakeLists.txt`, fetches JUCE and writes an Xcode project into a new `build/` folder. Nothing is compiled yet.
- **`cmake --build build --config Release`** compiles everything with optimisations on. The first build takes a few minutes because it compiles JUCE too; later builds only recompile what you changed.

When it finishes, the plugins are copied automatically (`COPY_PLUGIN_AFTER_BUILD`) to:

- `~/Library/Audio/Plug-Ins/VST3/Refract.vst3`
- `~/Library/Audio/Plug-Ins/Components/Refract.component` (the AU)

There's also a standalone app at `build/Refract_artefacts/Release/Standalone/Refract.app`. It's handy for testing without Live: open it, pick your audio device in Options, and play with your computer keyboard or a MIDI controller.

**To check it really is native:**
```bash
file ~/Library/Audio/Plug-Ins/VST3/Refract.vst3/Contents/MacOS/Refract
```
You should see both `arm64` and `x86_64`. Your old Iris file only had `i386` and `x86_64`.

**To check the AU passes Apple's validation:**
```bash
auval -v aumu Rfr1 Rfau
```
It should end with `AU VALIDATION SUCCEEDED`.

### 1.3 Load it in Ableton Live

1. Open **Settings → Plug-Ins**.
2. Turn on **Use VST3 Plug-in System Folders** (and **Use Audio Units v2** if you'd rather use the AU).
3. Click **Rescan**.
4. In the browser go to **Plug-Ins → VST3 → Refract Audio → Refract**, and drag it onto a MIDI track.

You only need one format. VST3 is the usual choice in Live.

### 1.4 If the build fails

I wrote this code without access to a Mac or JUCE, so I haven't compiled it, and the first build may stop on a small error. If it does, copy the **first** error message from Terminal and send it to me. The first error is the one that matters, because later ones are usually knock-on effects. Then rebuild with the same command.

To work on the code in Xcode instead of Terminal, open `build/Refract.xcodeproj`, choose the **Refract_VST3** or **Refract_Standalone** scheme at the top, and press ⌘B.

---

## Part 2: Using it

1. Pick a layer tab (**Layer 1 / 2 / 3**) at the top right.
2. **Load a sample.** Drag an audio file (WAV, AIFF, FLAC, MP3, Ogg) onto the sample view, or click **Load**.
3. **The sample view** works like Ableton's Sampler:
   - **Wave / Spectral** switches between the waveform and the paintable spectrogram. A white line moves across for every note that's playing.
   - **Zoom** by scrolling up/down or pinching on the trackpad. Scroll sideways to move along the sample. Double-click the ruler (the strip along the top), or click **Fit**, to see the whole sample again.
   - **Start / End:** drag the **S** and **E** flags in the top half of the ruler. In the Wave view you can also grab the white lines themselves. Anything outside them is dimmed and won't play.
   - **Loop:** pick a **Loop** mode, then drag the loop bar in the bottom half of the ruler. Drag its edges to set Loop Start / End, or drag the middle to move the whole loop.
     - *Forward* loops while the note is held.
     - *Ping-Pong* plays back and forth.
     - *Fwd + Release* loops while you hold the key, then plays on to the End marker when you let go (great for sounds with a tail).
   - **X-Fade** blends the end of the loop into the audio just before the loop start, so loops don't click. The diagonal line shows where it happens. Crossfade needs audio *before* the loop start, so it can't be longer than the gap between Start of sample and Loop Start.
   - **Snap** makes markers jump to the nearest zero crossing, where cuts are click-free.
   - **Reverse** plays the region backwards (the markers keep their places on the forwards waveform).
4. **Tuning.**
   - **Root** is the key at which the sample plays at its original pitch. Set it to the note the sample was recorded at.
   - **Detect** works this out for you: it finds the sample's pitch and sets Root and Fine so the sample plays in tune across the keyboard.
   - **Transpose** shifts by semitones (±4 octaves), and **Fine** by cents.
   - All three can be typed in: double-click the value under the dial. Root accepts note names like `F#2`.
5. **Paint** (in the Spectral view). Drag on the spectrogram to remove frequencies.
   - Time runs left to right and pitch runs bottom (low) to top (high).
   - **Remove / Restore** chooses what the brush does. A right-drag always does the opposite of the current mode.
   - **Brush** paints with a soft round brush. **Rectangle** drags out a box that clears a whole region (e.g. "everything above 5 kHz for the first second").
   - **Size** sets the brush radius. **Strength** sets how far it cuts: 1.0 removes fully, 0.3 gently turns things down.
   - **Reset Paint** undoes all painting on the layer. **Invert** swaps kept and removed, which turns "remove this noise" into "keep *only* this noise".
   - When you let go of the mouse, "Rendering..." appears for a moment while the sample is rebuilt. The waveform view updates to show the edited sound.
6. Shape the sound on the **MAIN** page: sub oscillator, amp envelope, filter, voice pitch/pan/level, delay, reverb, macros and output.

### Modulation (Stage 2)

Almost every dial can be modulated. The exceptions are on/off switches and drop-down menus, plus the macros and the mod amounts themselves.

- **The quick way:** right-click a dial, choose **Modulate with ▸**, and pick a source. It takes the first free row of the matrix with a gentle amount (0.25). Right-click again to remove it.
- **The matrix** (**MATRIX** page): 16 rows of *source → destination × amount*. Destinations are grouped by section in the menu.
  - An amount of ±1 means the full travel of the destination dial. Small amounts go a long way on wide ranges like Pitch, so shift-drag for fine control.
  - Rows 1-3 come pre-routed at amount 0: Env 2 → Cutoff, LFO 1 → Pitch, Mod Wheel → Cutoff.
- **Rings on the dials** (#CF9F9F) show modulation:
  - the faint ring is the range the modulation can reach;
  - the solid arc and dot show where it is right now, following the newest note you play.
- **Sources:**
  - the amp envelope and Envs 2-4;
  - LFOs 1-4;
  - Velocity, Mod Wheel, Aftertouch, Pitch Bend, Key Track (low notes negative, high notes positive) and Note Random (a different random value for each note);
  - Macros 1-4. Map these in Live to drive several things from one knob.
- **LFOs** (**MODULATION** page, tabs 1-4):
  - **Shape:** Sine, Triangle, Saw Up, Saw Down, Square or **Random** (a new random level each cycle).
  - **Rate Mode:**
    - **Hz** is free speed.
    - **Sync** locks to Live's tempo, from 1/32 to 8 bars, including dotted (D) and triplet (T) lengths. While Live is playing it also locks to the beat.
    - **Freq** follows the note you play (Ratio 1 = the note's own pitch).
  - **Depth** scales the LFO's output. It's modulatable too, e.g. Mod Wheel → LFO 1 Depth fades vibrato in.
  - **Jitter** randomly stretches or squeezes each cycle by up to an octave. On Random it makes the steps land unevenly.
  - **Smooth** rounds off sharp edges. On Random it turns steps into slow drifting curves.
  - **Phase** sets where each cycle starts.
  - **Retrigger** on: each note restarts the LFO at its Phase. Off: every note follows one shared, free-running LFO.
  - The little scope shows the shape, and the pale line shows the LFO's live value.
- **Envelopes 2-4** (**MODULATION** page): ADSR envelopes to use as sources. Their own times can be modulated too.

Note: in Freq mode the LFO is still updated 1,500 times a second (once every 32 samples). At audio-rate speeds it gives a gritty, stepped result rather than clean FM. Proper FM comes with the oscillators in Stage 3.

### Oscillators, unison and glide (Stage 3, **OSC** page)

- **Osc 1 and Osc 2.** Switch an oscillator **On** and choose its **Mode**:
  - **Analog:** **Wave** morphs smoothly sine → triangle → saw → square, and **Pulse W** sets the width of the square. The edges are anti-aliased, so high notes stay clean.
  - **Wavetable:** pick a **Table** and sweep **Position** through it.
    - Built-in tables: Basic Shapes, PWM, Harmonic Sweep, Formant, Digital.
    - **From Layer 1/2/3** turns that layer's sample into a wavetable. When the sample's pitch can be detected, each frame is exactly one cycle of it. Paint the spectrum or move the markers, and the table updates.
  - **Octave / Semi / Fine** tune the oscillator. **Level / Pan** place it in the mix.
  - **Unison** stacks up to 8 voices. **Detune** spreads their pitch (up to ±1 semitone; small settings are very fine), **Spread** spreads them across the stereo field, and **Rnd Phase** starts each voice at a random point. Keep it high for a wide supersaw, and turn it down for a punchier, more phase-aligned attack.
  - **Osc 2 only:**
    - **Sync** restarts Osc 2 every time Osc 1 starts a cycle. Sweep Osc 2's Semi or Octave (or modulate them) for classic sync leads.
    - **FM** lets Osc 1 bend Osc 2's phase, for bells, metallic tones and growls. This is real audio-rate FM, unlike the LFO Freq mode.
- **Sub + Noise.** The sub oscillator follows the note an octave or two down. **Tune** offsets it in semitones.
  - **Pitch Env / Decay / Curve** make the sub start higher and drop to the note. **Pitch Env** is how far above the note it starts, **Decay** is how long the drop takes, and **Curve** goes from a straight glide (0) to a snappy drop (1).
  - **Making a kick:** Sub on (sine, 0 Oct), Pitch Env 36-48 st, Decay 40-80 ms, Curve 0.6-0.8, amp envelope with a fast attack and about 300 ms decay and 0 sustain. Play around C1-C2.
  - The sub always starts at the same point of its wave, so every hit sounds the same. **Noise** adds noise, and **Colour** goes from dark (−) through white (0) to bright (+).
- **Voice mode.**
  - **Poly** plays chords.
  - **Mono** plays one note at a time and restarts the envelopes and samples on each new key.
  - **Legato** is one note at a time too, but overlapping keys only change pitch, so you get smooth lines. Releasing a key goes back to the previous held key in both mono modes.
- **Glide.**
  - **Always** slides from the previous note every time.
  - **Legato Only** slides only when keys overlap.
  - **Time** is how long the slide takes, the same whatever the interval. Glide works in Poly too: each new note slides from the last one played.
- **Sample layers have unison too**: **Unison** (up to 4 copies), **Detune** (up to ±50 cents) and **Spread**, on the right of the layer strip.

### Presets

The preset bar sits at the top of the window.

- **< / >** step through your presets, and the menu lists them all.
- **Save** overwrites the current preset. If the sound hasn't been saved yet, it asks for a name.
- **Save As** saves a new preset. **Init** starts from scratch. **Folder** opens the preset folder in Finder.

Presets live in **Music › Refract › Presets** as `.refract` files. Make sub-folders there (e.g. `Bass`, `Pads`) and they show up as `Bass/Name` in the menu.

**Samples are kept safe.** When you save, any sample a layer uses is copied once into **Music › Refract › Samples**, and the preset points at that copy. A preset keeps working even if you move or delete the original file. To share presets with someone, send them both folders.

Presets hold everything: every dial, the mod matrix routing, the samples and the spectral painting. Live's own "Save Preset" (in the device title bar) works too, and saves the same information inside Live's library.

### Filters (Stage 4, **FILTER** page)

There are two filters per voice. **Routing** sets how they connect:
- **Serial:** Filter 1 feeds Filter 2.
- **Parallel:** both filter the same sound and are blended together.

Set a filter's Type to *Off* to bypass it.

**Types**

| Type | Character |
|---|---|
| **SVF** Low / Band / High / Notch | Clean and precise, the all-rounder. |
| **Ladder** LP 24 / LP 12 / HP 24 | Moog-style. Thick and round; the bass thins a little as resonance rises, and it self-oscillates near the top. |
| **MS-20** Low / High | Korg-style. Aggressive, gritty resonance that screams and self-oscillates. Pair with Drive. |
| **Comb +** / **Comb −** | A tuned, very short echo with feedback. Metallic, plucked and flanger-like; Cutoff sets the pitch. **Shape** darkens the repeats. |
| **Formant** | Vowel sounds. **Shape** sweeps A-E-I-O-U, **Cutoff** shifts the "voice" up or down (1 kHz = natural), and **Resonance** sharpens the vowel. |

**Controls**

- **Drive** pushes the signal into soft saturation before the filter.
- **Key Track** makes the cutoff follow the keyboard. At 100 %, the cutoff moves an octave for every octave you play.
- **Env 2** is a built-in Envelope 2 → cutoff amount (−/+). This is the classic filter sweep, without touching the matrix.
- **Mix** blends between the dry signal and the filtered one.

All of these dials can be modulated, like everything else.

### Mixer and FX buses (Stage 5, **MIXER** and **FX** pages)

**MIXER: one channel strip per sound** (Layers 1-3, Osc 1-2, Sub, Noise):

- **Level** and **Pan**. These are the same dials as on the layer and oscillator panels.
- **To** picks where the sound goes: **Filter 1**, **Filter 2**, or **No Filter**.
  - In **Serial** routing (FILTER page), sounds sent to Filter 1 carry on through Filter 2 as well. Sounds sent to Filter 2 go through Filter 2 only.
  - In **Parallel**, each filter only gets its own sounds.
  - Example: put the sub on *No Filter* so the low end stays solid while you sweep the filter on everything else.
- **Send A / Send B** send a copy of that sound to an FX bus. The copy is taken *before* the filters, after the sound's level and the amp envelope.
- **Voice (post)** sends the whole voice *after* the filters, so the effects get the filtered sound.
- **Returns:** how loud each bus comes back into the mix, plus the **Master** volume.

**FX: two buses with three slots each.** Each slot can be any effect, processed top to bottom:

| Effect | P1 | P2 | P3 | P4 |
|---|---|---|---|---|
| **Delay** | Time | Feedback | Tone (Pitch in Granular) | Width / Wow / Spray |
| **Reverb** | Size | Damping | Width | Pre-Delay (up to 250 ms) |
| **Chorus** | Rate | Depth | Feedback | Delay |
| **Phaser** | Rate | Depth | Centre | Feedback |
| **Distortion** | Drive | Shape (soft → hard → fold) | Tone | Output |
| **Bitcrush** | Bits (16 → 2) | Downsample | Tone | Output |
| **Filter** | Cutoff | Resonance | LP → BP → HP | Drive |

**Delay modes:**
- **Digital:** clean repeats.
- **Ping-Pong:** repeats bounce left and right.
- **Tape:** wobbly, warming repeats that saturate instead of exploding at high feedback.
- **Granular:** the repeats are re-played as small grains, which you can pitch ±12 semitones and **Spray** (scatter in time and stereo). With feedback and Pitch up an octave, you get the classic "shimmer".

**Delay time:** turn **Sync** on to pick a note length tied to Live's tempo (1/32 to 8 bars, dotted and triplet), or off to set the time in milliseconds.

**Mix** blends dry and effect. On a bus you usually want 100 %, because the dry sound is already in the main mix.

**Starting setup:** Bus A starts with a synced delay and Bus B with a reverb. Turn up a sound's Send A or Send B to hear them.

### Granular mode and brushes (Stage 6)

**Granular mode.** Switch a layer from **Classic** to **Granular** with the menu in the layer's toolbar. The layer then plays as a cloud of tiny overlapping snippets ("grains") taken from the sample. This is great for pads, textures, frozen moments and glitchy movement.

The controls along the bottom of the layer change to:

- **Position:** where in the sample the grains come from. You can also drag the **G** flag in the ruler. Start and End still mark the part of the sample that can be used.
- **Size:** the length of each grain, from 5 ms (buzzy, granular) to 1 s (smooth, smeared).
- **Density:** how many grains start each second.
- **Spray:** scatters where each grain starts around Position. The pale band on the waveform shows the range.
- **Rnd Pitch:** random pitch per grain (± semitones), for shimmering, chorus-like clouds.
- **Shape:** 0 gives soft, smooth grains; towards 1 gives flatter, punchier grains.
- **Spread:** scatters grains across the stereo field.
- **Reverse:** the chance of each grain playing backwards.
- **Scan:** moves Position through the sample by itself. 1x is normal speed, small values give slow time-stretching, and negative values go backwards.
- **Freeze:** holds Position still.

Tuning (Root / Transpose / Fine), the keyboard, glide and every modulation work the same as in Classic. While notes play, each grain shows as a short tick on the waveform.

**Brushes** (Spectral view):

| Tool | What it does |
|---|---|
| **Soft / Hard / Square Brush** | Freehand painting with different edges. |
| **Spray** | Random speckles, for texture instead of clean cuts. |
| **Smudge** | Blurs earlier edits, softening their edges. |
| **Harmonic** | Paints the note under the cursor *and* all its overtones at once; the faint lines show where. Great for removing (or isolating, with Invert) a pitched sound. |
| **Freq Band** | A horizontal band across the whole sample, e.g. "remove everything around 3 kHz". |
| **Time Slice** | A vertical slice through every frequency, e.g. "silence this moment". |
| **Rectangle / Ellipse / Heart** | Drag out a box; the shape is applied when you let go. |

**Modes:**
- **Remove** cuts frequencies.
- **Restore** brings them back to normal.
- **Boost** raises them, by up to +12 dB at full Strength. Boosted areas look lighter.
- Right-dragging switches between Remove and Restore.

**Undo / Redo** step back and forward through your last 30 painting actions. Reset and Invert count as actions too.

Painting from older saves is converted automatically to the new scale (which now makes room for Boost).

---

## Part 3: How it works

This section walks through the design one piece at a time, explaining what each part does and why it was built that way.

### 3.1 The big picture

A plugin has two halves that run at the same time on different threads:

```
            ┌─────────────────────────── Message thread (UI) ───────────────────────────┐
            │  PluginEditor  ── knobs/attachments ──►  parameters (APVTS)                │
            │  SpectralEditor ── paints the mask ──►  processor.maskEdited()              │
            └───────────────────────────────────────┬───────────────────────────────────┘
                                                    │ starts a job
            ┌──────────────── Background thread pool ┴──────────────────────────────────┐
            │  read file → draw spectrogram → apply mask (FFT) → new SampleData          │
            └───────────────────────────────────────┬───────────────────────────────────┘
                                                    │ publish (atomic swap)
            ┌──────────────── Audio thread ─────────▼───────────────────────────────────┐
            │  processBlock: MIDI → 16 voices (layers+sub → filter → amp env) → delay   │
            │                → reverb → master gain → Ableton                           │
            └───────────────────────────────────────────────────────────────────────────┘
```

**The audio thread** (`processBlock`) is called by Ableton every few milliseconds and must hand back audio *on time, every time*. If it's late you hear a click or dropout. So it must never wait for a lock, read files, allocate memory or do big calculations. Breaking these rules is the most common reason plugins stutter and crash, and it's very likely part of why old plugins struggle on new systems.

**The message thread** runs the UI.

**Background threads** do the slow work (loading files and FFT processing), so neither the UI nor the audio has to wait.

### 3.2 The files, in the order to read them

| File | What it is |
|---|---|
| `CMakeLists.txt` | Build recipe: which formats to build, plugin IDs, universal binary settings. |
| `Source/Parameters.h/.cpp` | Every automatable knob: its ID, range, default and how it's displayed. |
| `Source/SampleData.h` | The loaded-sample type, and the lock-free "bank" the audio thread reads from. |
| `Source/Effects.h` | The FX slot effects, including the 4-mode delay. |
| `Source/Filters.h` | The filter models: SVF, Ladder, MS-20, Comb, Formant. |
| `Source/Wavetable.h/.cpp` | Wavetables: built-in tables, tables made from samples, anti-aliased mip levels. |
| `Source/SpectralProcessor.h/.cpp` | The FFT maths: spectrogram picture, and rebuilding audio through the painted mask. |
| `Source/SynthVoice.h/.cpp` | One playing note: sample playback, sub osc, envelopes, LFOs, filter, mod matrix. |
| `Source/PluginProcessor.h/.cpp` | Ties it all together: voices, effects, background jobs, save/load. |
| `Source/SpectralEditor.h/.cpp` | The paintable spectrogram component. |
| `Source/PluginEditor.h/.cpp` | The rest of the UI: knobs, sections, layer tabs, layout. |
| `Source/RefractLookAndFeel.h` | Colours and knob drawing. Re-skin everything from here. |

### 3.3 `CMakeLists.txt`: the build recipe

- `CMAKE_OSX_ARCHITECTURES "arm64;x86_64"` is **the** line that makes this an Apple Silicon plugin. It builds a *universal binary*: native code for M-series chips and for Intel, in one file.
- `FetchContent` downloads a pinned version of JUCE, so the build is reproducible and there's nothing to install by hand.
- `juce_add_plugin(...)` is where JUCE's helper generates the AU, VST3 and Standalone wrappers around your code. You write the instrument once and JUCE handles each format's rules.
  - `IS_SYNTH` / `NEEDS_MIDI_INPUT` tell hosts this is an instrument, not an effect.
  - `PLUGIN_MANUFACTURER_CODE` / `PLUGIN_CODE` are 4-letter IDs. Hosts use them to recognise the plugin in saved sets. **Never change them after you've used the plugin in projects**, or Live won't find it.

### 3.4 `Parameters`: the knobs

Each parameter has a **string ID** (like `flt_cutoff`), a **range** and a **default**. They're created once in `createParameterLayout()` and stored in an `AudioProcessorValueTreeState` (APVTS). The APVTS gives you three things for free:

1. **Automation.** Ableton sees every parameter.
2. **Saving.** Values are stored with your set.
3. **UI linking.** "Attachments" keep a knob and its parameter in sync in both directions.

Some details worth noticing:

- **Skewed ranges** (`setSkewForCentre`). Cutoff runs 20 Hz to 20 kHz, but hearing is logarithmic, so the knob's middle is placed at 1 kHz. Without this, half the knob's travel would be spent above 10 kHz.
- **`ParamRefs`** collects raw `std::atomic<float>*` pointers to every value. The audio thread reads a knob with `ptr->load()`, a single lock-free instruction. That's how the voice reads 50+ parameters every 32 samples without slowing down.
- **The text functions** (`formatValue`/`parseValue`) are why Ableton shows "350 ms" or "1.20 kHz" rather than "0.3500000".

### 3.5 `SpectralProcessor`: painting with frequencies

This is the core idea, so it's worth taking slowly.

**A spectrogram** shows which frequencies are present at each moment. It's made with the **FFT** (Fast Fourier Transform), which takes a short slice of audio (2048 samples, about 43 ms) and tells you how much of each frequency is in it. Slide the slice along the sample and you get a column per moment. Stack the columns and you have a picture.

**The mask** is a 512 × 256 grid of numbers from 0 to 255, one per cell of that picture. 255 means "keep" and 0 means "remove". Painting just writes numbers into this grid. The rows are **log-spaced** (each octave gets the same height, like a piano keyboard), which matches how you hear and makes the low end paintable.

**Applying the mask** (`applyMask`) is a technique called **STFT resynthesis**:

1. Cut the sample into overlapping slices, each starting 512 samples after the previous one (75% overlap).
2. Multiply each slice by a **Hann window**, a smooth hump that fades the slice in and out. Without it, the hard edges of each slice would show up as fake frequencies.
3. FFT the slice, so you now have its frequencies.
4. Multiply every frequency bin by the mask value at that time and frequency. Painted-out areas get multiplied by 0.
5. Inverse FFT back to audio, window again, and **overlap-add** the slices back together.
6. Divide by how much the windows overlapped at each point (`norm`), so untouched audio comes back at exactly the same level.

I checked this maths numerically: with nothing painted, the output matches the input to within 0.00007, and painting out a 3 kHz tone removes it by about 80 dB while a 200 Hz tone alongside it is untouched.

**Why do this offline instead of in real time?** Iris-style synths could filter spectrally while playing, but doing FFTs for every voice in real time is expensive. Here the work is done **once, when you finish a brush stroke**, and the result is stored as an ordinary sample. Playing 16 notes then costs barely more than playing 16 plain samples. That's the main reason this design runs smoothly. Another advantage: if nothing is painted, the original sample is used untouched (bit-for-bit).

### 3.6 `SampleData` and thread safety

When the background job finishes, the audio thread needs the new sample. That's tricky, because the audio thread might be in the middle of reading the old one.

The solution:

- Samples are held by `std::shared_ptr<const SampleData>`. `const` means a published sample is **never modified**, only replaced, and shared data that never changes is safe to read from any thread.
- `PlaybackBank::exchange()` swaps in the new pointer atomically (all at once), so the audio thread sees either the old sample or the new one, never half of each.
- Each voice grabs its own copy of the pointer when a note starts, so a held note keeps playing the old version even after you edit.
- The old sample isn't freed straight away. It goes into a **graveyard**, and a timer on the message thread frees it once no voice is using it (`use_count() == 1`). This guarantees memory is never freed on the audio thread, because freeing a large buffer there can take long enough to cause a click.

Each layer also has **generation counters**. Every edit gets a number, and if you paint again before the last render finishes, the old job notices its number is out of date and stops (`shouldAbort`). Fast repeated edits therefore never pile up, and an older result can never overwrite a newer one.

### 3.7 `SynthVoice`: one note

JUCE's `Synthesiser` class handles MIDI: it finds a free voice for each note-on and passes note-offs along. `RefractVoice` does the sound.

**Sample playback**

- Pitch comes from how fast the read position moves through the sample. Playing an octave up moves 2 samples per output sample, and the ratio is `2^(semitones/12)`.
- The voice also corrects for sample-rate differences. A 44.1 kHz file in a 48 kHz session is read at 44.1/48 speed so it isn't out of tune.
- When the position lands *between* two samples, `readSample` uses **cubic Hermite interpolation**, a smooth curve through the 4 neighbouring points. Simply rounding to the nearest sample sounds gritty, especially when pitched down.

**Control rate**

- Modulation is recalculated every 32 samples, not every sample (`updateControl`). At 48 kHz that's 1500 times a second, far faster than you can hear changes, and it costs a fraction of the CPU.
- To avoid tiny steps ("zipper noise"), layer and sub levels **ramp** smoothly across each 32-sample block (`gainL += stepL`).

**The mod matrix**

- Each slot does `destination += source × amount`. The sources are all roughly in the range -1 to 1, so one amount knob means the same thing for any route.
- Cutoff modulation is in *octaves* (`2^(mod × 6)`). That's why sweeps sound even across the whole range rather than all happening at the top.

**Envelopes and filter.** These use JUCE's `ADSR` and `StateVariableTPTFilter`. The TPT ("topology-preserving transform") filter stays stable even when its cutoff is changed rapidly, which matters when an LFO is moving it.

**Freeing voices.** When the amp envelope finishes, the voice calls `clearCurrentNote()` so it's free for the next note.

### 3.8 `PluginProcessor`: the conductor

- **`prepareToPlay`** is called before playback starts (and when you change the sample rate or buffer size). Everything that needs to know the sample rate, or that allocates memory, is set up here, so `processBlock` doesn't have to.
- **`processBlock`** runs, in order: `ScopedNoDenormals`, then voices, delay, reverb and master gain. `ScopedNoDenormals` matters because reverb tails fade into extremely tiny numbers ("denormals") that some CPUs process up to 100× slower.
- **Delay** is a classic feedback delay. Each echo is fed back in at `feedback` strength, and `delaySamples` glides when you move the time knob, so you hear a pitch swoop instead of clicks.
- **`getStateInformation` / `setStateInformation`** save and restore your sound when Live saves the set. They store all parameters, each layer's file path and its mask (gzip-compressed, since a mostly-unpainted mask shrinks to almost nothing). The audio itself is *not* stored, only the file path, so keep your samples where they are (or use Live's **Collect All and Save**). If a file goes missing, the layer says so and you can drop it in again.

### 3.9 The UI

- **`Knob`, `Choice` and `Toggle`** wrap a JUCE control plus its label and **attachment**. The attachment is what makes automation in Live move the knob on screen, and makes turning the knob write automation.
- **`Section`** is a titled panel that lays its children out in a row.
- **`SpectralEditor`** converts mouse positions into mask cells (`toMask`), stamps a soft round brush (a raised-cosine falloff, so edits fade at the edges instead of having hard cut-offs), and fills the gaps between mouse events so fast strokes don't leave dotted lines. It only *writes the mask*. When you release the mouse it asks the processor to re-render, and the processor tells the UI (via `ChangeBroadcaster`) when the new picture or render is ready.
- **`RefractLookAndFeel`** holds the colours and the custom knob drawing. Bipolar knobs (pan, fine, mod amount) draw their arc from the centre.

### 3.10 Stage 1 additions: sampler view, loops and tuning

- **Playheads.** Each voice writes "where am I in each layer, 0..1" into a small table of atomic numbers (`PlayheadArray`) at the end of every audio block. The sample view reads that table 30 times a second and draws the lines. Atomics are safe to share between threads without locks, so the audio thread never waits for the UI.
- **Markers are just parameters.** Start, End, Loop Start, Loop End and X-Fade are ordinary automatable parameters, stored as 0..1 of the sample length so they still make sense if you swap in a different sample. Dragging a flag calls `setValueNotifyingHost`, the same as turning a dial. That's why you can automate loop points in Live.
- **How a loop plays** (`RefractVoice::renderNextBlock`):
  - In *Forward* mode, when the read position passes Loop End it jumps back by exactly one loop length.
  - In *Ping-Pong* mode it flips direction instead.
  - *Fwd + Release* is Forward until the note-off arrives (`keyReleased`), after which wrapping stops and the sound plays through to the End marker.
- **Crossfade.** As the read position enters the last stretch before Loop End, the voice reads a second point exactly one loop length earlier (i.e. just before Loop Start). It fades from the first to the second with an equal-power curve (cos/sin). By the time it reaches Loop End it's playing only the second point, which is precisely where the jump lands, so the join is seamless.
- **Reverse** uses a reversed copy of the sample made in the background (`makeSampleData`). Every marker is mirrored (`len - position`) and then exactly the same forward-playing code runs. That's much simpler than writing all the loop logic twice.
- **Zoom.** The view keeps a visible range (`viewStart`..`viewEnd`, 0..1). Every pixel↔sample conversion goes through `xToNorm`/`normToX`, so zooming changes only those two numbers.
  - The waveform uses pre-computed min/max blocks (`WaveformPeaks`) when zoomed out, and the real samples when zoomed in. That way drawing costs about the same at any zoom level.
- **Pitch detection** uses the YIN method (`detectPitch`).
  - It takes a chunk of the sample and compares it with copies of itself delayed by 1, 2, 3... samples. When the delay equals one cycle of the waveform, the copy lines up and the difference dips.
  - The first clear dip gives the period, and `frequency = sampleRate / period`.
  - Converting to a note: `69 + 12 × log2(frequency / 440)`. The whole-number part becomes Root, and the leftover becomes Fine.

### 3.11 Stage 2: how the modulation system works

- **One list of targets** (`ParamTable` in `Parameters.cpp`). When the plugin starts, it walks through every parameter and keeps the continuous ones (float or whole-number). Each entry stores the parameter's ID, its display name, its group (for the menu) and its range. Adding a dial in a later stage automatically makes it modulatable, with no extra code.
- **Modulation happens on the knob position, not the value.** Each voice keeps an `offsets` array, one number per target. The matrix adds `source × amount` into it, and `ParamTable::value()` returns `range.convertFrom0to1(knobPosition + offset)`. Because this uses the dial's own curve, +0.1 on Cutoff moves it the same distance as turning the dial a tenth of its travel. Modulating frequencies and times then sounds even, rather than everything happening at one end.
- **Per voice vs global.** Every note has its own envelopes, LFOs, velocity and so on, so most targets are modulated separately per voice (`RefractVoice::computeModulation`). The delay, reverb and master volume process the whole mix, so they can't follow each note. They follow the **newest note** (the "lead voice"), and when nothing is playing they follow the free-running LFOs, wheel, aftertouch and macros (`computeGlobalModulation`).
- **Order inside each 32-sample block:**
  1. Read the sources (envelope values, LFOs, MIDI controls).
  2. Run the matrix to fill the offsets.
  3. Turn the modulated values into settings (`updateControl`).
  - LFO settings read the *previous* block's offsets. That's how LFOs can modulate each other without an endless loop. The 0.7 ms delay is inaudible.
- **The routing** (which source and which target per row) isn't a host parameter. It lives in a small table of atomics (`ModRouting`) that the UI writes and the audio thread reads. It's saved *by name* (`"lfo1"` → `"flt_cutoff"`), not by number, so future stages can add sources and dials without scrambling old sets. The amounts *are* parameters (`modN_amt`), so you can automate them in Live.
- **The rings.** At the end of each block the processor copies the lead voice's offsets into atomics (`liveOffsets`). The editor's 30 Hz timer reads them for every dial and stores the range and live position as properties on the slider (`modFrom`, `modTo`, `modLive`). `RefractLookAndFeel::drawRotarySlider` then draws them.
- **LFO internals** (`Lfo` in `SynthVoice.h`):
  - A phase runs from 0 to 1. When it wraps, a new random level is picked (for Random) and a new jitter factor is chosen: `2^(jitter × random(-1..1))`, i.e. up to an octave faster or slower for that one cycle.
  - Smooth is a one-pole low-pass on the output, a filter that moves a fraction of the way towards its target each step. Its time constant is `smooth² × ½ cycle`. Squaring makes the low end of the dial gentle.
  - In Sync mode with Live playing, the phase is taken straight from the song position (`ppq ÷ beats per cycle`), so it can never drift off the beat.

### 3.12 Stage 3: how the oscillators work

- **Aliasing and PolyBLEP.** A digital saw or square has an instant jump. Mathematically that jump contains harmonics far above what the sample rate can hold, and they "fold back" as inharmonic whistles, worst on high notes. **PolyBLEP** (`polyBlep` in `SynthVoice.cpp`) adds a tiny pre-computed correction curve in the one or two samples around each jump, which removes most of the fold-back. A triangle has no jump (only a corner), so it barely aliases, and a sine has none at all.
- **The analog morph.** `analogValue` works out the two neighbouring shapes (e.g. saw and square) and crossfades between them. Because Wave is a normal dial, every LFO and envelope can sweep it.
- **Wavetables and mip levels** (`Wavetable.cpp`).
  - A table is 32 frames of 2048 samples each. When a table is built, each frame is FFT'd, and 10 copies are made with fewer and fewer harmonics: all 1024, then 512, 256, and so on down to 2.
  - While playing, `Wavetable::levelFor` picks the copy whose highest harmonic still fits below half the sample rate for that note. It's the same anti-aliasing idea as PolyBLEP, done ahead of time.
  - Position crossfades between the two nearest frames.
- **Tables from samples.** After a layer is loaded or painted, the background job runs the pitch detector on the result. If it finds a pitch, each frame is exactly one period of the sound, resampled to 2048 points, taken from 32 evenly spaced places through the sample. That's why a sung note or a string sample becomes a playable, morphing wavetable.
- **Unison.** Each oscillator keeps 8 phases. Voice *u* of *n* sits at a position from −1 to +1 (`unisonPosition`), which sets both its detune (`2^(position × cents / 1200)`) and its pan. The total is divided by √n so adding voices doesn't make it much louder. Sample layers do the same with up to 4 read positions.
- **Hard sync.** When an Osc 1 voice wraps from the end of its cycle back to the start, it records how far past the wrap it went (`wrapFraction`). The paired Osc 2 voice resets to that same point in its own cycle, so the reset happens at the exact sub-sample moment.
- **FM** is done as phase modulation (the way the classic Yamaha DX synths work). Osc 1's last output, times the FM amount, is added to Osc 2's read position. The FM amount is squared so the low end of the dial gives subtle results.
- **Glide.** The voice keeps `currentPitch` (in MIDI note numbers, fractions allowed) and moves it towards the key you pressed at a constant number of semitones per sample. The rate is set so the whole slide takes exactly Glide Time.
- **Mono and Legato** (`RefractSynth::noteOn`/`noteOff`).
  - JUCE's `Synthesiser` normally starts a fresh voice for every note. In these modes we always reuse voice 0 and keep a list of held keys.
  - Before handing voice 0 its new note, we call `prepareLegato`. The voice then ignores the "stop immediately" that JUCE sends first, and only changes pitch (Legato) or restarts envelopes and samples smoothly (Mono).
  - Releasing the current key hands the voice the most recent key still held.

### 3.13 Stage 4: how the filters and presets work

- **Zero-delay feedback (ZDF).** A resonant filter feeds its output back into its input. The old digital way used last sample's output, which adds a one-sample delay to the loop. That throws the tuning off at high cutoffs and can blow up when modulated. The SVF, Ladder and MS-20 here solve the feedback loop *exactly* each sample (the "topology-preserving transform" method), so they track the cutoff accurately and stay stable however hard you modulate them.
- **SVF** (`runSvf`): Andrew Simper's trapezoidal state-variable filter. One calculation gives low, band and high pass at once, and notch is low + high.
- **Ladder:** four one-pole stages in a row, with the output fed back to the input.
  - The code works out what the last stage's output must be (`y4 = (G⁴·in + S) / (1 + k·G⁴)`), feeds that back, then runs the four stages.
  - `tanh` on the input imitates the saturation of the transistors in the original.
  - LP 12 takes the output after two stages instead of four. HP 24 combines the stage outputs using the binomial pattern 1, −4, 6, −4, 1.
- **MS-20** (Korg-35 design, after Will Pirkle's model): two low-pass stages with a *high-passed* feedback path. The saturator sits inside the loop, which is why the resonance distorts and screams instead of just ringing.
- **Comb:** a delay line whose length is one cycle of the cutoff frequency (`sampleRate / cutoff`), read with linear interpolation so it tunes smoothly. A one-pole low-pass in the feedback (Shape) darkens each repeat.
- **Formant:** three band-pass SVFs set to the first three formant frequencies of a sung vowel (e.g. "A" ≈ 730, 1090, 2440 Hz), crossfaded between neighbouring vowels by Shape.
- **I checked the filter maths numerically** before building it in. The low-pass slopes are 12 / 24 dB per octave as expected, the resonance peaks sit at the cutoff, and nothing goes unstable.
- **Presets** use the same code that saves your Live set (`createStateTree` / `restoreStateTree`), written to disk as readable XML. Loading a preset clears any layer the preset doesn't use, so you never get leftover samples from the previous sound.

### 3.14 Stage 5: how the mixer and buses work

- **Each voice now renders six channels:** main L/R, Bus A L/R and Bus B L/R. The processor gives JUCE's Synthesiser a 6-channel work buffer (`work`), and every voice adds its sound into all three pairs.
- **Inside a voice,** each sound is kept separate (`srcL[]` / `srcR[]`) until the mixer stage. Each sound is then added into one of three sums: Filter 1's input, Filter 2's input, or dry. At the same time, it's multiplied by its send amounts into the two bus sums.
  - Serial: `F2 input = F2's own sounds + F1's output`.
  - Parallel: `main = dry + F1 out + F2 out`.
  - The amp envelope is applied last, so sends follow each note's volume shape.
- **Why are sends taken before the filters?** A filter mixes all its input sounds into one signal, so after it there's no way to say how much of the output came from the sub and how much from Osc 1. *Voice (post)* covers the other case by sending the filtered voice as a whole.
- **The buses** run once for the whole plugin (not per voice) after all voices have played, in `renderChunk`. Each slot (`FxSlot` in `Effects.h`) contains every effect type, but runs only the selected one. Switching type resets the effect, so old echoes don't leak into the new one.
- **The delay** uses a 5-second circular buffer per channel.
  - *Ping-Pong* writes the left echo into the right channel's buffer and vice versa.
  - *Tape* moves the read point with two sine waves (wow at 0.6 Hz, flutter at 7 Hz) and puts `tanh` saturation in the feedback.
  - *Granular* starts an 80 ms grain every 20 ms from around the delay point. Each grain reads at `2^(semitones/12)` speed with a smooth Hann fade in and out, and the grains are fed back into the delay buffer.
- **Big host blocks.** Live can occasionally send a bigger audio block than it said it would in `prepareToPlay`. `processBlock` cuts such blocks into pieces it was prepared for, so nothing ever has to allocate memory while playing.

### 3.15 Stage 6: how granular mode and the brushes work

- **Grains** (`spawnGrain` / `renderGrains` in `SynthVoice.cpp`).
  - Every `sampleRate / Density` samples (with ±15 % randomness, so it sounds natural rather than machine-gun regular), the voice starts a new grain.
  - Each grain picks a start point (Position ± Spray), a speed (the note's normal playback speed × a random pitch offset, negative if it plays backwards), a pan and a length (Size).
  - While a grain plays, it's multiplied by a *window*, a fade in and out, so it never clicks. Shape morphs the window from a Hann bell into a flat-topped "Tukey" window with short fades.
  - Up to 24 grains per layer per note overlap. The total is divided by √(Density × Size) so the loudness stays roughly the same when you change them.
- **Scan** advances an offset every block, in proportion to the note's playback speed. At 1x the cloud moves through the sample exactly as fast as normal playback would. That's what makes it a time-stretch: the pitch comes from the note, and the speed comes from Scan.
- **The grain ticks** in the sample view come from a small ring buffer of atomics (`GrainMarks`). Each new grain writes its start point into it, and the UI draws the last 64.
- **The mask scale changed** from "255 = untouched" to "200 = untouched, 255 = +12 dB" (`maskToGain`). That leaves room for Boost while still fitting in one byte per cell. Old saves carry no `maskVersion`, so their values are rescaled when loaded.
- **The Harmonic brush** converts the cursor height to a frequency `f`, then stamps at `2f`, `3f`, `4f`... up to 20 kHz. Because the frequency axis is logarithmic, the stamps get closer together towards the top, just like the harmonics of a real note.
- **Smudge** replaces each cell with a blend of itself and the average of the 5×5 cells around it, read from a snapshot of the mask so the blur doesn't drag in the direction you paint.
- **The heart** is the classic curve `(x² + y² − 1)³ − x²y³ ≤ 0`. Any cell whose position makes that true gets painted. The outline you see while dragging uses the heart's parametric form (`16 sin³t`, `13 cos t − 5 cos 2t − ...`).
- **Undo** simply keeps copies of the mask (about 260 KB each, up to 30 of them) from before each action.

---

## Part 4: Ideas for what to build next

Each of these is a good self-contained project:

1. **Undo for painting.** Keep a stack of mask copies and push one on every `mouseDown`.
2. **Loop crossfades.** Blend the last few milliseconds of the loop into the start to remove loop clicks.
3. **Tempo-synced LFOs and delay.** Read the BPM with `getPlayHead()->getPosition()` in `processBlock`.
4. **Root-note setting per layer.** Useful when a sample isn't pitched at C.
5. **Preset browser.** Save/load the same XML that `getStateInformation` produces.
6. **Embed samples in the set.** Store the audio in the state (larger saves, but nothing can go missing).
7. **A resizable window.** Add `setResizable(true, true)` and make `resized()` scale.
8. **Glide/portamento**, **unison**, or a **spectral "freeze"** that holds one column of the spectrogram as a pad.

## Customising

- **Rename it.** Change `PRODUCT_NAME`, `COMPANY_NAME` and `BUNDLE_ID` in `CMakeLists.txt` before you use it in real projects. Keep the 4-letter codes stable once you do.
- **Change the colours.** They're all in `RefractLookAndFeel.h`.
- **Change the polyphony.** Set `numVoices` in `Parameters.h`.
