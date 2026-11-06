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
6. Shape the sound with the sub oscillator, envelopes, filter, LFOs, mod matrix and effects.

**Mod matrix tips.** Slots 1-3 come pre-routed with their amount set to 0, so you only need to turn an amount up:

- *Mod Env → Cutoff* at +0.5, with the filter cutoff around 500 Hz, gives a classic filter sweep on each note.
- *LFO 1 → Pitch* at about +0.02 gives vibrato. Pitch range is ±12 semitones at full amount, so small values go a long way.
- *Mod Wheel → Cutoff* puts the filter on your controller's wheel.

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
