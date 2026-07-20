# NOISEBOY

A chromatically-pitched noise + filter / Karplus-Strong instrument for
Schwung on Ableton Move. Strips DISTROY down to just its noise
generators and two resonant filters (Moog Ladder, Korg35 MS-20-style
LP/HP), reused directly from `distroy_dsp.c` for sonic consistency --
the intended use is running NOISEBOY into DISTROY on the same Move
set, so sharing filter/noise character with it is deliberate.

## What it does

Every time the module is instantiated, it randomizes a "recipe" of
1-3 layers (fixed for that load, not re-randomized per note). Each
layer is independently either:

- **Filtered noise**: a noise generator (random colour: white/pink/
  red) through a resonant filter (randomly Moog Ladder or Korg35
  LP/HP) whose cutoff always tracks the played note's pitch.
- **Karplus-Strong**: a noise-excited, damped delay line -- the
  classic plucked-string algorithm -- whose delay length itself
  defines the pitch, picked randomly per layer as a second,
  independent pitch-defining method alongside filter tracking.

Playing is polyphonic (up to 8 voices), gated by key press -- a note
produces sound only while held (with a short knob-controlled attack/
release to avoid clicks, not a sustained pad envelope).

## Controls (knobs 1-8)

| Knob | Parameter | Range |
|---|---|---|
| 1 | Filter Offset | brightens/darkens filtered-noise layers relative to the tracked pitch -- centre (64) = filter sits exactly at the played note, never stops tracking it |
| 2 | Resonance | filter resonance (filtered-noise layers); also blends into Karplus-Strong pluck damping |
| 3 | AM Rate | amplitude modulation rate, 0.1-20 Hz |
| 4 | AM Depth | amplitude modulation depth, 0 = off (default) |
| 5 | Attack | envelope attack time, 0.5-200 ms |
| 6 | Release | envelope release time, 5-2000 ms |
| 7 | Detune | spread between layers' per-layer detune (0 = unison, up = richer/chorused) |
| 8 | Level | master output level |

## Status / what's verified vs. not

**Verified by an actual test suite** (`make test`), which I ran and
confirmed passing, not just reasoned about:
- Finite output (no NaN/Inf) across 12 different randomization seeds
  and 6 notes spanning the practical range (A0 to past C8), through a
  full note-on/hold/note-off/release cycle each
- Every tested note actually produces audible signal (not silently
  broken for some recipe/note combination)
- A voice fully releases to silence and deactivates after note-off
- 4-voice polyphony works (all finite)
- Voice stealing under 9-notes-on-8-voices doesn't crash or produce
  non-finite output
- Directional pitch-tracking sanity (a low note produces fewer zero-
  crossings than a high note over a fixed window) -- a weak but
  meaningful proxy check, not a precise pitch measurement, since
  filtered noise and Karplus-Strong are both non-pure-tone sources
- The layer-count randomization (1-3) is genuinely uniform, checked
  across 2000 seeds (665/667/668 split) after an initial 12-seed test
  run happened to show none landing on 1 layer -- verified that was a
  statistical fluke, not a bias, before moving on
- All module.json knob defaults are numerically consistent with the
  DSP engine's own `noiseboy_engine_init` defaults -- checked by hand,
  each of the 8 conversions

**NOT verified -- genuinely unknown until you build it:**
- `src/noiseboy_plugin.c`, the actual Schwung plugin wrapper, has
  **not** been compiled against a real `plugin_api_v1.h`. I have no
  network access in this environment to clone Schwung and confirm the
  v2 API's exact struct layout, field names, or host_api_v1_t
  callback signatures match what's used here -- everything in that
  file is taken directly from `docs/MODULES.md`'s own code example
  (found in an earlier session's transcript search, not fabricated),
  but that's a documentation example, not a compiled build. If the
  real header differs in any field name, the compiler will point at
  exactly where -- that's the first thing to check if `build.sh`
  fails.
- The `component_type: sound_generator` category, the `sound_generators/`
  install path, and the chain_params schema are all cross-checked
  against real published modules and the docs, but this specific
  module has never been loaded into a real Schwung host.
- Not playtested on real Move hardware at all. CPU headroom on the
  Move's Cortex-A72 with up to 8 voices x up to 3 layers each (24
  simultaneous noise+filter/Karplus chains at once, worst case) is
  unknown -- if it's too heavy, voice count or layer count are the
  first levers to pull, similar to db-cell's "start with 8, reduce if
  needed" approach.
- Karplus-Strong tuning (damping range, pluck excitation amplitude)
  and the filter cutoff-offset knob's exact multiplier range (currently
  0.25x-4x around the tracked pitch) are reasoned choices, not ear-
  tuned -- unlike db-cell, I have no way to listen to this, so expect
  these numbers to need adjustment once you actually hear it.

## Build

Requires a real Schwung checkout as a sibling directory:

```
your-workspace/
  schwung/         <- git clone https://github.com/charlesvestal/schwung.git
  noiseboy/        <- this project
```

```sh
make test              # native DSP sanity check, no Schwung/Docker needed
./scripts/build.sh      # ARM64 cross-compile via Docker, produces build/dsp.so
./scripts/install.sh    # scp's module.json + dsp.so to move.local over SSH
```

## Chaining into DISTROY

Load NOISEBOY as the sound generator in a Schwung slot, then load
DISTROY as that same slot's Audio FX 1 or 2 (each slot's chain is
MIDI FX -> Sound Generator -> Audio FX 1 -> Audio FX 2) -- NOISEBOY's
noise/filter output feeds directly into DISTROY from there.
