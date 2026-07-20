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

## Controls (10 chain_params -- knobs 1-8, plus 2 more via the parameter menu)

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
| 9 | Drive | single shared drive/saturation stage on the final mix -- beyond the 8 physical knobs, reachable via the module's parameter menu |
| 10 | Randomize | re-rolls the layer recipe on demand, without reinstantiating the module -- rising-edge trigger (any 0->nonzero move fires it once) |

## Status

**v0.3.0** -- db-cell is now permanently baked into the output, per
explicit request ("add db-cell on the output, always... this will
test whether I like my own work"). Full C port of db-cell's chaos
modulator and processing chain (`dbcell_dsp.c/h`) -- same 26-type
restricted pedal pool, same forced-Noiz-slot-at-33%-cap, same
battery-never-at-100% floor, same combined legacy+hybrid chaos
modulator, same brickwall limiter placement, same 1%-3% wet/dry blend,
all ported line-by-line from db-cell's actual `PluginProcessor.h/.cpp`
rather than reimplemented from memory. `distroy_dsp.c/h` (the shared
DSP core underneath both db-cell and NOISEBOY) needed no porting --
already plain C -- but this DID require consolidating NOISEBOY's own
noise/filter primitives (which had been copied in verbatim when
NOISEBOY was first built) down to one shared copy, since having both
`distroy_dsp.c` and NOISEBOY's own duplicate definitions in the same
build would have been a duplicate-symbol compile error. Verified the
consolidation didn't break anything by compiling and linking all three
DSP files together before writing a single line of new logic.

NOISEBOY's mono voice output is duplicated to both channels as
db-cell's "dry" input; db-cell's independent per-channel chain state
naturally diverges the two over time even though they start identical
each sample, so the combined output gets some real stereo width
NOISEBOY's own mono synthesis doesn't have on its own. The one thing
NOT ported is db-cell's UI (wet-trim slider, tube/bulb indicators,
Combine Modes toggle) -- there's no equivalent surface for those here,
and "always on" per this request means a toggle wouldn't have anything
to do anyway.

New: pressing Randomize now re-rolls BOTH layers together (NOISEBOY's
own recipe and db-cell's), each with an independent random seed so
they're not correlated.

Verified with a dedicated test (`make test-dbcell`, also runs as part
of the default `make test`): finite output across 8 different
randomization seeds fed a steady tone, peaks stay sensible (~0.30,
matching the 0.3-amplitude test input -- confirms the 1-3% wet blend
isn't overwhelming the dry signal), and a mid-stream re-randomize
stays finite too.

**v0.2.1** -- fixed Randomize/Drive not being reachable at all: I'd
put both as a 9th/10th `chain_params` entry, but Schwung's docs
describe `chain_params` as specifically mapping to the 8 physical
knobs ("knobs 1-8 in the Shadow UI") -- there's no knob 9 or 10 for
those two to bind to, so they were likely just invisible. The actual
mechanism for a broader navigable parameter list is a separate thing,
`ui_hierarchy`, which wasn't implemented before this. Added it now: a
"root" level listing all 10 params, with only the first 8 bound to
physical knobs via the hierarchy's own `"knobs"` array -- Drive and
Randomize should now be reachable through the module's parameter menu
even without a dedicated knob. Verified the hand-built JSON string is
actually valid JSON (extracted and parsed it, not just eyeballed) and
that the file still compiles and passes the full test suite. Same
caveat as the rest of the plugin wrapper: built from documentation,
not confirmed against a real working example -- next real build is
the test.

**v0.2.0** -- four fixes based on real Move hardware feedback (the
first actual listening test this project has had):

1. **All noise now goes through a filter.** Root cause of "some
   layers sound dry": the filter pool included Korg35 HP (highpass),
   which is derived as `input - its own resonant lowpass core` -- at
   the resonance needed for a clear pitched character, this actually
   creates a *notch* at the tracked frequency rather than a peak,
   working against audibility rather than for it, especially
   noticeable on higher notes where a highpass tuned there passes most
   of the spectrum through nearly unchanged. Removed from the random
   selection pool (Moog Ladder / Korg35 LP only now, 50/50); the code
   path and enum value are left in place per this project family's
   "keep superseded options, don't delete" convention.
2. **Pitch is now much stronger.** Two changes: (a) default filter
   resonance raised from 0.3 to 0.82 -- 0.3 is nowhere near strong
   enough for a resonant filter to produce an audible peak at its
   cutoff from noise input, which is the entire mechanism "pitched
   noise via filter" depends on; (b) added a new sample-and-hold
   pitch stage (`PitchedHold`) applied to filtered-noise layers
   *before* the filter -- holds each noise sample for a duration
   derived from the note's frequency, giving a buzzy, quantized
   character with a much more obvious relationship to the played
   pitch, reinforcing rather than replacing the filter's resonant
   peak. This is the "playback at a lower rate" mechanism directly
   requested, implemented as a pre-filter stage rather than a
   separate layer type.
3. **New "Randomize" control** (10th chain_param, beyond the 8
   physical knobs but reachable via the module's parameter menu) --
   re-rolls the layer recipe on demand without reinstantiating the
   module. Extracted the recipe-generation logic out of
   `noiseboy_engine_init` into its own `noiseboy_randomize_recipe()`
   so both the initial load and this on-demand trigger share the same
   code. Implemented as a rising-edge trigger (0 -> nonzero) so it
   fires once per press rather than continuously while a bound control
   is mid-movement, and only affects notes played after the call --
   in-flight voices keep whatever recipe they started with, since
   retroactively swapping a sounding voice's DSP state would click.
4. **New single shared drive/saturation stage** on the final mix
   ("Drive") -- simple pre-gain into `tanh`, deliberately not
   renormalized afterward since the saturation's natural loudness/
   density increase at higher settings IS the requested "more volume"
   effect, not a side effect to correct for. Verified safe at maximum
   drive + maximum velocity: stays finite and peaks at 0.80
   (tanh-bounded, confirmed via a standalone stress test, not just
   reasoned about).

All four changes re-verified against the existing test suite (still
passing) plus the new stress test above. Not yet re-tested on real
hardware -- that's the next real verification step.

## v0.1.0 verification notes (original build)

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

Note this is a different, additional thing from the db-cell
processing described above, which is already always running as part
of NOISEBOY itself now -- db-cell is a subtler, always-on, randomized
subset (26 of 31 pedal types, capped at 1%-3% wet, no manual control)
baked into the voice engine's own output stage. Chaining into DISTROY
on top of that stacks its full 31-type chain and complete manual
control over whatever db-cell already did, rather than being the only
way to get DISTROYBOY-family character into NOISEBOY's sound.
