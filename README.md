# NOIZBOY

A chromatically-pitched noise + filter / Karplus-Strong instrument for
Schwung on Ableton Move. Strips DISTROY down to just its noise
generators and two resonant filters (Moog Ladder, Korg35 MS-20-style
LP/HP), reused directly from `distroy_dsp.c` for sonic consistency --
the intended use is running NOISEBOY into DISTROY on the same Move
set, so sharing filter/noise character with it is deliberate.

## What it does

Every time the module is instantiated, it randomizes a "recipe" of
1-3 layers (fixed for that load, not re-randomized per note). Each
layer is independently one of three sound generation methods:

- **Filtered noise** (~60% chance per layer): a noise generator
  (random colour: white/pink/red). During release, the raw source
  itself darkens naturally (a gentle lowpass that engages only as the
  note decays) -- the same kind of timbral evolution a real plucked
  or struck source has, which a constant-timbre noise source
  otherwise lacks.
- **Karplus-Strong** (~25% chance): a noise-excited, damped delay line
  -- the classic plucked-string algorithm -- whose delay length itself
  defines the pitch.
- **Loop** (~15% chance): a third method, deliberately lo-fi -- "a
  poorly looped sample which you can hear repeating." A fixed 8000-
  sample buffer of raw noise is captured once at note-on and read back
  continuously via nearest-neighbor resampling (not interpolated --
  the artifacts are the point) at a rate that transposes with the
  played note, the same way a cheap/old sampler pitch-shifts a fixed
  sample. At middle C the loop repeats every 8000 samples; an octave
  up, every 4000 (read twice as fast); an octave down, every 16000
  (each buffer sample read twice in a row).

Per voice, the layers mix together first (raw, unfiltered), then pass
through a single, shared, voice-level pitch-tracking filter (Moog
Ladder, high resonance -- see "Status" for why this is no longer
randomized against Korg35LP -- tuned to the played note, with
frequency-dependent resonance compensation so it reads more evenly
across the keyboard), then the amplitude envelope, then a final
output filter (velocity-brightened, knob-8-controlled, zero
resonance). See "Signal chain" below for the exact order.

Playing is polyphonic (currently 4 voices -- see "Status", a
diagnostic reduction from the original 8 while a real Move CPU
ceiling gets confirmed), gated by key press -- a note produces sound
only while held (with a short knob-controlled attack/release to avoid
clicks, not a sustained pad envelope).

## Signal chain

1. Sources -- up to 3 noise/Karplus/Loop layers, mixed together (raw,
   no per-layer filtering). Filtered-noise layers darken on their own
   during release, at this stage, before anything else touches them.
2. Voice-level pitch-tracking filter -- high resonance, tracks the
   played note directly (knobs 1-2), no envelope or velocity
   influence, frequency-compensated for more even resonance across
   the keyboard.
3. Amplitude modulation (tremolo) + wavefolder, both knob 3/4
   controlled and linked to the same cycle.
4. Amplitude envelope (attack/release, knobs 5-6).
5. Output Filt -- a tilt filter, knob 8 controlled, the last stage
   before output. Centre = bypassed; left = lowpass sweeping down;
   right = highpass sweeping up. Either extreme silences the signal.

Bitcrush and sample-rate reduction were both tried at various
amounts and ultimately removed entirely, per direct feedback -- see
"Status" below.

## Controls (11 chain_params -- knobs 1-8, plus 3 more via the parameter menu)

| Knob | Parameter | Range |
|---|---|---|
| 1 | Filter Offset | brightens/darkens the voice-level pitch-tracking filter relative to the played note -- centre (64) = filter sits exactly at the played pitch, never stops tracking it |
| 2 | Resonance | resonance of the voice-level pitch-tracking filter; also blends into Karplus-Strong pluck damping |
| 3 | AM Depth | amplitude modulation depth, 0 = off (default) |
| 4 | AM Rate | amplitude modulation rate, 0.1-20 Hz |
| 5 | Attack | envelope attack time, 0.5-200 ms |
| 6 | Release | envelope release time, exponential/log-scale mapping, ~0.02ms (a single sample) to 4000ms. Most of the knob's range is now dedicated to short, "plucky" times, with only the top end reaching long, pad-like sustain |
| 7 | Detune | spreads BOTH pitch and stereo image (0 = unison and mono; up = richer/chorused pitch AND wider stereo). Filtered-noise layers pan to fixed positions (reusing each layer's own pitch-spread randomization); Karplus layers auto-pan back and forth at the AM rate instead |
| 8 | Output Filt | TILT filter, the last stage before output. Centre (64) = fully bypassed. Turn left: lowpass sweeps down (20kHz to 20Hz), silencing from the top. Turn right: highpass sweeps up (20Hz to 20kHz), silencing from the bottom. Makes the sound disappear in either direction |
| 9 | Drive | single shared drive/saturation stage on the final mix -- beyond the 8 physical knobs, reachable via the module's parameter menu |
| 10 | Randomize | re-rolls the layer recipe on demand, without reinstantiating the module -- rising-edge trigger (any 0->nonzero move fires it once); also reachable as a single jog-wheel-click action in the module's own menu screen |
| 11 | Level | master output level -- moved off knob 8 to make room for Output Filt; still fully controllable via the parameter menu |

## Status

**v0.15.0** -- naming fix plus three substantial investigations, each
with a real bug found and fixed, not just guessed at.

**Naming**: fixed the display name shown on the Move (was "NOISEBOY",
should always have been "NOIZBOY" -- the repo, folder, and even this
module's own `abbrev` field ("NOIZB") already used this naming; only
the main display string never matched). Fixed in `module.json`'s
`name` field, the on-screen text in `ui.js`, and a fallback menu title
embedded in the plugin's own JSON.

**1. Karplus latency / "only triggers every other note" when playing
fast.** Two distinct, compounding bugs found and fixed:
- The voice-stealing selection logic never excluded voices that
  already had a deferred steal queued (from the previous session's
  click fix). A voice waiting to play a queued note has rapidly-
  decaying `envLevel` from its forced fast release, which made it
  look like the BEST candidate to the very next note-on -- getting
  picked again and silently overwriting the note already waiting.
  Confirmed directly: notes could be dropped entirely, survivors
  showed 100+ms latency.
- A second, worse bug found while fixing the first: the existing
  handling for "note released before its deferred steal even
  completed" set `gateOpen=0` in the SAME sample the voice started --
  before the envelope had computed even once with `gateOpen=1`. Since
  gateOpen IS the attack target, envLevel never rose above 0 at all --
  completely, silently dropped, not just cut short. Exactly what fast
  pad-tapping under voice-stealing load would trigger constantly.
  Fixed with a brief (~5ms) guaranteed minimum hold so the attack
  genuinely gets a chance to be audible.

Also sped up deferred-steal resolution itself from ~114ms to ~13ms
(the original time constant needed far more time to reach its
threshold than intended), re-verified still click-free via the
existing baseline-relative test. Stress-tested at 25ms note spacing,
10ms hold times: zero drops, ~12.5ms max latency (down from notes
being lost entirely).

**2. Reduced Randomize variety.** Traced to the previous session's own
resonance-evenness fix (removing filter-type randomization, adding a
resonance compensation curve) -- both real, deliberate trade-offs at
the time, but with a real side effect on variety. This one took real
investigation to actually fix, not just implement: an initial attempt
(varying resonance +-20%) measured under 1% actual difference, because
that parameter sits on the flat top of this filter's own response
curve at typical operating points -- and worse, the FIRST verification
of that attempt was itself misleading (used impulse excitation, not
the continuous noise the real engine feeds this filter). Pivoted to
varying cutoff instead, which tested far more reliably in isolation
(~21% RMS range) -- but wiring it into the real engine, the measured
difference STILL dropped to under 1%. Root cause: the always-on tape
saturation stage runs a compressor on the entire final mix, which
normalizes overall loudness by design and specifically defeats any
amplitude-based variation, no matter where upstream it originates.
The fix wasn't the parameter -- it was recognizing that loudness
itself is the wrong thing to vary here. Cutoff shifts SPECTRAL
character (brightness), which survives that compression completely
intact even though overall RMS gets normalized away. Verified with
zero-crossing rate (a frequency-content proxy, not an amplitude one):
35.1% measured difference between the low and high ends of the new
`timbreCharacterMul` range (+-15%, bounded well short of the original
79x Moog/Korg35LP inconsistency this is deliberately not trying to
recreate).

**3. Release envelope "not plucky enough" on noise, only 128 knob
values.** The envelope's own decay curve was already unconditionally
exponential (verified directly, no linear branch anywhere) -- the
actual issue was the KNOB-TO-MILLISECONDS mapping being linear across
a huge range (5-2000ms) with only 128 discrete MIDI steps to cover it.
Most of those steps were spent on the long, pad-like end, leaving very
few, coarse steps for the short "plucky" end where fine control
matters most. Remapped to exponential/log-scale, per explicit spec:
knob=0 now maps to ~0.02ms (one sample at 48kHz, literally "a single
sample" as requested), knob=1 to 4000ms (up from 2000ms, for genuine
room to be "almost hard to hear" at the top). Also had to lower the
envelope's own hard clamp floor (was 0.5ms, now 0.02ms) -- without
that, the new mapping's short end would have been silently clamped
away regardless of how the knob itself was reshaped. Verified: at
knob=0.02 (just 2-3 raw MIDI steps off minimum), the OLD linear
mapping was already at 45ms (nowhere near "plucky"); the new mapping
is still at 0.025ms there -- a concrete demonstration of how much more
of the knob's range is now usable for short, percussive times.

Five new permanent tests added this round (`make test-fastplay`,
`test-timbre`, `test-release`, plus updates to the existing staccato
and darkening tests whose own assertions needed correcting once the
underlying bugs were actually fixed), all part of the default
`make test`. Full 18-suite run, including the 24,000-combination
sweep, passes clean.

**v0.14.0** -- three substantial changes from one focused investigation
session, each verified with a dedicated test, not just implemented and
hoped for.

**1. Resonance evenness ("some notes more resonant than notes around
it").** Two distinct causes found and fixed:
- The dominant one: filter type (Moog Ladder / Korg35LP) was
  randomized fresh per note. Measured directly (impulse response):
  Korg35LP's actual resonant peak was up to ~79x weaker than Moog's at
  the same knob value -- adjacent notes landing on different filter
  types produced wildly inconsistent resonance, exactly the reported
  symptom. Investigated fixing Korg35LP's own calibration first, but a
  coefficient sweep showed genuinely non-monotonic behaviour (higher
  resonance sometimes producing LOWER peak gain) -- evidence the
  compensation formula's whole shape, borrowed from Moog's 4-pole
  derivation, may be wrong for a 2-pole loop, not something to
  re-derive correctly under time pressure. Fixed by always using Moog
  Ladder now, removing the inconsistency at its source.
- The secondary one: even Moog alone varies substantially with cutoff
  frequency for a fixed resonance knob (0.28 measured peak at the
  bottom of the playable range vs. 0.94 at the top). Added a
  frequency-dependent compensation boost for lower notes, capped at a
  MEASURED, genuine physical ceiling -- resonance stops helping (and
  very slightly reverses) past roughly 2x the knob value at the
  lowest notes, a real property of this filter topology at low
  cutoff-to-sample-rate ratios, not a made-up safety margin. Narrows
  the measured spread from ~3.4x to ~2.1x -- real, verified
  improvement, honestly not perfectly flat given that hard ceiling.

**2. LOOP -- a third sound generation method**, per explicit spec: "a
poorly looped sample which you can hear repeating." A fixed 8000-
sample buffer of raw noise, captured once at note-on, read back via
nearest-neighbor resampling (deliberately not interpolated -- the
artifacts are the entire point) at a rate that transposes with the
played note, exactly like a cheap/old sampler pitch-shifting a fixed
sample. Verified directly against the explicit spec: loop period is
exactly 8000 samples at middle C, 4000 at one octave up (buffer read
twice as fast), 16000 at one octave down (each buffer sample read
twice in a row -- confirmed 10 of 20 consecutive samples duplicated,
matching "every other sample duplicated" precisely). Added at 15% in
the recipe randomizer alongside the existing two methods (now ~60%
filtered-noise / ~25% Karplus / ~15% Loop). Full pipeline tested
across 500 seeds with zero silent voices.

**3. Natural release character for noise layers**, per direct
feedback that Karplus "sounds very plucked... because of its nature"
while noise "does not sound plucked on releases." Verified first that
the envelope math itself was already unconditionally exponential, no
linear branch anywhere -- the real explanation: Karplus's own string
damping darkens its TIMBRE as it decays, on top of the shared volume
envelope, which noise layers have no equivalent of. Fixed by
darkening the RAW noise source (a one-pole lowpass, the same leaky-
integrator technique this project already uses for its red/brown
noise colour) progressively during release only. Deliberately applied
to the source, BEFORE the pitch-tracking filter, rather than by
touching that filter's own cutoff -- specifically to avoid
reintroducing the earlier, reverted "pitch tied to envelope" bug.
Verified directly, not just reasoned about: pitch drift during release
measured at only 0.8%, confirming the earlier bug's mechanism was not
reintroduced, while the darkening direction was independently
confirmed too.

Six new permanent tests added this round (`make test-resonance`,
`test-loop`, `test-darken`, plus the earlier `test-steal`/
`test-staccato`/`test-tilt`/etc. from prior sessions), all part of the
default `make test`. Full 13-suite run, including the 24,000-
combination sweep, passes clean.

**v0.13.1 -- DIAGNOSTIC BUILD, not a committed change.** Polyphony
reduced from 8 to 4 voices, per direct request, to test whether
"still glitching above Release of 7" (after v0.13.0's voice-stealing
fix) is a real CPU ceiling on the Move rather than remaining DSP
logic. Reasoning: v0.12.0's stereo panning doubled per-voice
processing cost (vibrato, pitch-tracking filter, and output tilt
filter all now run independently for L and R, where they used to run
once) -- and longer release keeps more voices "active" simultaneously,
compounding that cost. This project has never actually profiled real
CPU usage on Move hardware; it's been a flagged unknown since the
very first build. This is the fastest way to get a real answer: if
the glitching goes away at 4 voices, that's strong evidence for a CPU
ceiling; if it persists even here, that rules CPU out and points back
at DSP logic still to be found.

Purely a one-line constant change (`NOISEBOY_MAX_VOICES`, 8 -> 4) --
nothing else touched. Full test suite re-run clean at this reduced
count, including the 24,000-combination sweep. CONFIRMED via direct
testing on real hardware: reducing to 4 voices eliminated the
glitching entirely -- this really was a CPU ceiling, not remaining
DSP logic. Still running at 4 voices as of v0.14.0 while a genuine
optimization pass (to reclaim more polyphony) remains a separate,
future decision rather than something rushed alongside this round's
three unrelated feature/bugfix requests.

**v0.13.0** -- fixed clicking/glitching with longer release times, per
direct report ("with release longer than 7, it starts to click and
glitch... increasing release amount increases glitching... release of
8" on several random patches).

**Root cause**: longer release times mean voices stay "active" (and
therefore ineligible to be picked as a free voice) for longer, so the
8-voice pool runs out more often, forcing voice-stealing. The old
stealing logic immediately reset a still-sounding voice's ENTIRE state
(envelope, Karplus pluck, filters) in one step -- confirmed directly
via a dedicated test: up to a 0.17 single-sample jump right at the
moment of a steal, against a baseline of ~0.01 for the same signal's
own normal variation.

**Important methodology note, since a naive test almost gave a false
result here**: a simple "max jump over a window" check is NOT valid
for this signal -- this project's default filter resonance (0.82,
near self-oscillation) legitimately produces large sample-to-sample
jumps on its own. A no-stealing-involved baseline measurement showed a
0.226 max jump with nothing wrong at all, actually larger than an
early (correctly click-reduced) measurement taken WITH stealing --
which would have looked like the fix made things worse if judged by
that metric alone. The valid test compares the jump specifically AT a
voice reset against the signal's own baseline jump magnitude, not an
absolute threshold.

**The fix**: voice stealing is now deferred. When a steal is needed,
the old voice is forced into a fast, fixed 15ms release (NOT the
user's own Release knob -- this transition happens quickly regardless
of how long Release is set) and the new note's info is parked. Only
once the old sound has genuinely decayed to near-silence does the
actual state reset happen -- adds a few milliseconds of latency to a
stolen note's onset, not perceptible in practice, but eliminates the
click since there's nothing audible left for the reset to click
against by the time it happens. Verified: jump at the actual
transition is now ~2.9x the signal's own baseline variation (previously
this same measurement point was undefined -- the old code reset
immediately, so there was no "transition" to separately measure at
all), well within normal range, not a spike.

**Related edge case, also fixed**: a very quick staccato note that
happens to land on a steal could previously "stick" and sustain even
after being released, since it hadn't been assigned to a voice yet
when the note-off arrived (nothing for `noiseboy_note_off` to find).
Now tracked explicitly -- if the pending note gets released before its
deferred steal completes, it starts already in release instead of
sustaining. Verified with a dedicated test.

Two new permanent tests added (`make test-steal`, `make test-staccato`,
both part of default `make test`), plus the existing full suite
(including the 24,000-combination sweep) re-run clean.

**v0.12.0** -- Detune (knob 7) now spreads the stereo image, not just
pitch, per explicit request. This required making the whole voice
processing chain stereo-aware for the first time (NOISEBOY was mono
internally through v0.11.x, with the plugin wrapper just duplicating
one signal to both channels) -- a genuinely large change, so handled
carefully: the existing mono `noiseboy_process()` was kept working
unchanged (now a thin wrapper around a new
`noiseboy_process_stereo()`), so none of the existing test suite
needed to change, and every voice-level stateful filter (vibrato,
pitch-tracking filter, output tilt filter) was duplicated into
independent L/R instances -- running the same filter state on both
channels would have collapsed any panning straight back to mono.

Panning behaviour, per spec: filtered-noise layers get a FIXED pan
position, reusing each layer's own already-randomized `detuneCents`
(the same per-layer spread already driving pitch) scaled by the
Detune knob -- so a layer's pitch character and its stereo position
come from the same underlying randomization, not two unrelated ones.
Karplus layers instead auto-pan back and forth at the AM rate (same
shared phase driving AM/wavefold/vibrato), also scaled by Detune. At
Detune=0 everything collapses to exact mono.

Caught and fixed a real bug via testing, not just implementation-by-
inspection: the first version didn't collapse to EXACT mono at
Detune=0 (a small but real L/R difference remained). Traced to the
global tape saturation stage sharing one `TapeSaturation` instance
called sequentially for L then R -- its internal envelope-follower
state mutated between calls, so even identical input produced
slightly different output per channel. Fixed by duplicating that
instance too (matching every other voice-level stage's own pattern).
Re-verified: exact 0.0 difference at Detune=0, confirmed via a new
dedicated test (`make test-stereo`, now part of default `make test`)
that also verifies spread scales substantially with the knob (0.027 at
Detune=1 vs. 0.0 at Detune=0) and that Karplus layers' pan genuinely
oscillates (confirmed swinging both left and right over one second at
a 4Hz AM rate, not just moving once).

Also updated `render_block` in the plugin wrapper to actually call the
new stereo function and feed genuine L/R into db-cell, replacing the
old mono-duplicated signal -- the piece that makes all of this reach
the actual output, not just exist in the DSP layer.

Full test suite re-run clean, including the 24,000-combination sweep
-- the mono backward-compatibility wrapper meant zero existing tests
needed modification for this change.

**v0.11.1** -- fixed audible "zipper" stepping on knob 8 (Output Filt),
per direct report. Root cause: `set_param` writes the knob's target
value instantly on every discrete MIDI CC step as a physical knob
turns, and the tilt filter previously read that target directly with
no smoothing -- each step caused an instant jump in cutoff frequency,
audible as stepping rather than a smooth sweep. Especially noticeable
here given the tilt filter's wide range and silence-at-both-extremes
design, where even small target jumps near the extremes produce large
audible changes.

Fix: knob 8's value now glides toward its target (~15ms time constant,
smoothed once per sample) rather than jumping to it. Verified directly
with a dedicated test that simulates discrete knob steps (like real
MIDI CC messages arriving as a knob turns) and confirms the actual
smoothed value used by the filter changes by no more than ~0.0007 per
sample, vs. up to 0.1 in a single sample without smoothing (`make
test-zipper`, now part of the default `make test`).

This required updating the existing tilt-filter silence test too --
with smoothing now taking ~250ms to fully converge from centre to an
extreme, the test's original 500ms peak-measurement window was
capturing the transition period (filter still partway open) rather
than steady-state behaviour. Fixed by letting the smoothing settle
first, then measuring -- the correct fix given the new, intentional
smoothing behavior, not a loosened threshold papering over it.

**v0.11.0** -- Output Filt (knob 8) rebuilt as a TILT filter, per
direct request. Checked the previous design first rather than
assuming it was broken -- `set_param`, `get_param`, and the
`ui_hierarchy` knob mapping for `output_filter_freq` all traced
correctly, so the most likely explanation is the old design (a
velocity-scaled multiplier, clamped near Nyquist on the bright end)
was just too subtle to register as doing anything.

New behaviour: knob centred (12 o'clock, 64) = completely bypassed.
Turning left engages a lowpass whose cutoff falls (20kHz down to
20Hz, log scale) the further left you go. Turning right engages a
highpass whose cutoff rises (20Hz up to 20kHz) the further right you
go. Only one side is ever active at a time -- a true tilt, not two
filters stacked.

Caught and fixed a real acoustic limitation during testing, not just
implemented blind: isolated-filter testing against white noise showed
Korg35HP's own attenuation plateaus around ~29% RMS remaining no
matter how close its cutoff gets pushed toward Nyquist -- a structural
property of this filter topology at extreme cutoff ratios, not
something tunable away. Added a supplemental quadratic gain fade
(applied to BOTH sides, for symmetry) that guarantees true silence at
each extreme regardless of what the filter alone achieves, while
leaving the filter's own natural sweep character dominant through
most of the knob's range. Verified directly: both extremes now
measure exactly 0.0 output in a dedicated test (`make test-tilt`, now
part of the default `make test`), center position confirmed unchanged
(full signal passes through).

**v0.10.0** -- bitcrush and sample-rate reduction removed entirely,
per explicit request, after several rounds of trying to find a
subtle-enough amount kept drawing "too much bitcrushing and sample
rate reduction". Rather than keep tuning it, both are gone. Signal
chain is now: sources -> pitch-tracking filter -> AM/wavefold ->
envelope -> output filter, with nothing between sources and the
filter.

Notable side effect, not the point of the change but real and
measured: pitch-tracking accuracy improved substantially.
This project's own zero-crossing test ratio (low note vs. a note 4
octaves higher) jumped from ~3.5x to ~7.2x (true value for a 4-octave
span is 16x) once the rate-reducer was removed -- direct confirmation
of a tradeoff flagged back in v0.9.0's restructuring notes ("the
rate-reducer now runs BEFORE the pitch filter and can compete
with/mask its resonant peak"). Removing it didn't just address the
"too much crushing" complaint -- it also fixed that tradeoff as a
side effect.

`bitcrush_process`/`pitchedhold_process` remain defined in
`noiseboy_dsp.c/h` (unused now) per this project's established "keep
superseded options, don't delete" convention, in case a future
revision wants a lighter version of either back. All references to
the removed per-voice `bitDepth`/`rateReducer`/`rateReducerMultiplier`
state were removed from `Voice`, and the test that checked per-voice
bitDepth variation was updated to no longer reference the removed
field (it would not have compiled otherwise).

Full test suite re-run clean, including the 24,000-combination sweep
(still zero silent cases -- if anything, more robustly so now that
there's no bitcrush-related silencing failure mode possible at all).

**v0.9.1** -- investigation into a "dive bomb" / "pitch attached to
envelope" report. Being direct about what was actually verified vs.
hypothesized here, since this one wasn't a clean bug-with-a-fix:

**Verified, not the cause**: exhaustively traced every use of
`envLevel` in the codebase (8 total) -- none of them touch any
frequency/cutoff/rate parameter. Specifically confirmed the rate-
reducer's rate is mathematically constant for a note's entire
duration: both `v->freqHz` and `v->rateReducerMultiplier` are set once
at note-on and never touched again, so "sample rate reduction attached
to envelope" cannot happen as literally described -- there's no code
path for it. Bitcrush's bit depth is likewise fixed at note-on.
Vibrato was also ruled out: its depth comes from the AM Depth knob
(fixed, defaults to 0) and even when active is a symmetric oscillation
around centre, not a one-directional sweep.

**Best hypothesis, addressed**: Karplus-Strong's sustain-feed was
hard-gated (an instant on/off snap in excitation level at the exact
release boundary), which could produce an audible spectral
discontinuity right at the moment release begins -- smoothed this to
a gradual fade instead (both directions: fades in from 0 at note-on,
fades out at release) rather than an instant step.

**Tried and reverted**: reduced the pitch-tracking filter's drive
(0.2->0.05) on the theory that a saturating feedback path at high
resonance could let the resonant peak shift with input amplitude/
transients. This measurably WEAKENED pitch tracking -- this project's
own zero-crossing test ratio dropped from ~3.5x to ~1.5x (further from
the true 16x for a 4-octave span) -- a confirmed regression for an
unconfirmed benefit, so reverted back to 0.2.

**Still open**: asked whether the effect happens on pure-filtered-
noise patches (no Karplus) to help isolate whether this is Karplus-
specific or more general -- no answer yet. If the smoothing above
doesn't resolve it, that answer is the next real lead, since it would
rule the Karplus-specific hypothesis in or out directly.

**v0.9.0** -- major signal chain restructuring, per explicit spec, plus
the root cause of a real reported bug found and fixed:

**The bug**: "sampling rate of bitcrushers... following amp envelope...
with a long release, the pitch decays." The actual mechanism: the OLD
per-layer pitch-tracking filter's cutoff was modulated by the envelope
(a 20% brightening at the envelope's peak, added in v0.4.0) -- as
envLevel decayed during a long release, that filter's cutoff (which
IS the pitch mechanism for filtered-noise layers) decayed right along
with it, audibly dragging the pitch down. This is exactly the failure
mode flagged as a risk when it was first added ("envelopeMul... could
theoretically have the same issue" as the velocity bug fixed in
v0.7.0) -- now confirmed and removed.

**The restructuring** (per explicit 5-step spec, "trying to push back
towards musicality"):
1. Sources -- up to 3 noise layers + Karplus-Strong, raw generation
   only now. The per-layer filter and per-layer PitchedHold pitch
   stage are GONE -- no filtering happens per-layer anymore.
2. Bitcrush + pitch-following sample-rate reduction, applied to the
   combined voice signal. Unchanged in mechanism, just now runs before
   any filtering rather than after.
3. NEW: a single voice-level pitch-tracking filter (high resonance,
   randomized per note between Moog Ladder / Korg35LP, matching
   bitDepth's own per-note variety) replaces all the old per-layer
   filters. Cutoff is purely `freqHz * knob-1-offset` -- NO envelope
   modulation, NO velocity modulation, nothing but the played note and
   two relevant knobs. This is the actual fix for the reported bug.
4. Amplitude envelope.
5. OUTPUT FILT -- moved to genuinely last in the chain now (was
   before the envelope through v0.8.0; explicit spec puts it after).
   Still the velocity-brightened, zero-resonance, knob-8-controlled
   final stage.

Vibrato, AM, and wavefolder (not part of the explicit 5-step list)
were fit in at their most sensible existing positions: vibrato stays
early (pitch-bending the raw combined source, same as before), AM/
wavefold stay immediately before the envelope (they're both amplitude-
family effects).

**Honest note on a real tradeoff observed**: the existing pitch-
tracking test (zero-crossing count, low note vs. high note 4 octaves
apart) showed a notably weaker ratio after this change (14->49, ~3.5x,
vs. the true 16x and vs. ~14-19x in earlier architectures) -- likely
because the rate-reducer now runs BEFORE the pitch filter and can
compete with/mask its resonant peak on some notes, whereas before the
filter ran on a cleaner, unreduced signal. This is a direct, expected
consequence of the explicitly-requested step 2-before-step-3 ordering,
not a bug -- flagging it because it's a real, measured difference
worth listening for, not because anything here contradicts the spec
as given.

Full test suite re-run clean after this restructuring, including the
24,000-combination sweep (still zero silent cases) -- verified no
stale references to any of the removed per-layer filter fields
remained anywhere in the codebase before finishing.

**v0.8.0** -- three changes from real playing feedback, plus a note on
what turned out NOT to be a code problem:

1. **Bitcrush reduced again.** Was 8-15 (already halved once from an
   original 1-15). Halved the worst-case distance from full
   resolution a second time: floor raised to 12, range now 12-15.
   Worth noting: the rate-reducer running alongside bitcrush (down to
   100Hz) may itself be contributing to a perceived "crushed"
   character independent of bit depth -- flagged, not changed, since
   only bitcrushing specifically was reported as excessive.
2. **Output Filt's resonance removed entirely** (was a modest fixed
   0.15, now hardcoded 0.0) -- per direct correction that even that
   modest resonance was still creating a perceivable pitch-like
   artifact. A filter with no pitch-defining role shouldn't have any
   resonant peak at all.
3. **Knob 8 moved from Level to Output Filt**, per explicit request
   to make it "the final knob to control audible sound" -- Output
   Filt is the last processing stage before the envelope/output, so
   controlling it from the last physical knob is a deliberate,
   meaningful pairing. Knob 8's value now multiplies the velocity-
   driven cutoff range up or down (same offset-around-neutral pattern
   knob 1 already used), rather than setting output level. Level
   moved to a menu-only position (still fully controllable, just not
   on a dedicated knob) -- 11 chain_params total now, 8 on physical
   knobs, 3 menu-only (Drive, Randomize, Level).

On investigating the "still too much bitcrushing compared to last
night's build" report: this session also uncovered that this
project's local files had drifted significantly out of sync with what
was actually being tested (module.json still read "0.1.0", and
noiseboy_dsp.c/noiseboy_plugin.c were hundreds of lines short of
current) -- so "last night's build" and tonight's build may not have
been comparable in the way it seemed. Given that, some of the
perceived difference may be explained by that drift rather than by
anything changed in the DSP itself between those two sessions. Fixed
via a full clean replacement of the affected files rather than another
partial merge, and confirmed via exact line-count matches before
proceeding.

**v0.7.0** -- musicality fix plus a lighter bitcrush, per direct
feedback after real playing:

1. **Velocity no longer detunes notes.** The v0.6.0 "velocity
   brightens the filter" change modulated the SAME cutoff that defines
   pitch for filtered-noise layers (the filter's resonant peak IS the
   pitch mechanism), so harder-played notes were also going sharp
   relative to softer ones on the same key -- a real, direct
   correction, not just a preference tweak. Removed velocity from the
   per-layer filters entirely; pitch tracking is pure again. Velocity-
   driven brightness now lives in a new, separate stage instead --
   OUTPUT FILT, a second lowpass (Moog Ladder) applied once to the
   whole mixed voice signal after everything else (layers, vibrato,
   bitcrush, rate-reduce, AM, wavefold) but before the amplitude
   envelope, per explicit spec. 800Hz-16kHz across the velocity range,
   modest fixed resonance for character without becoming a second
   pitch-defining peak. Confirmed via the existing pitch-tracking test:
   the note-36-to-note-84 zero-crossing ratio actually got MORE
   accurate after this fix (14.3x, vs. the old 19.2x which overshot
   the true 16x for a 4-octave span) -- real evidence the old coupling
   was distorting pitch, not just a subjective impression.
2. **Bitcrush backed off 50%**, per direct feedback ("too much
   bitcrushing on randomize"). Was a full 1-15 bit random range
   (worst-case distance from 16-bit full resolution: 15). Halved that
   worst-case distance -- floor raised from 1 to 8, so the range is
   now 8-15. Eliminates the harshest crushing entirely while keeping
   some randomized variety.

Note left in place, flagged rather than silently changed: the
envelope-following cutoff (20% modulation from v0.4.0) uses the exact
same underlying mechanism as the velocity change that just got pulled
-- it's possible it has the same kind of subtle pitch-drift issue
during attack/release. Wasn't reported as a problem, so left as-is,
but worth listening for.

All test suites re-run clean after this round, including the full
24,000-combination sweep (still zero silent cases, if anything safer
now with the raised bitDepth floor).

**v0.6.1** -- real build fix: `distroy_dsp.h` (the shared DSP core)
already has its own `NoiseGate` struct and `noisegate_init`/
`noisegate_process` functions (part of DISTROY's own pedal chain,
apparently), which I didn't know about when adding a same-named gate
in `noiseboy_plugin.c` for the post-dbcell silencing fix -- caused a
genuine compile error (conflicting types) on the real build. Renamed
mine to `NoiseboyOutputGate`/`noiseboy_output_gate_init`/
`noiseboy_output_gate_process` to disambiguate. Also proactively
diffed every struct and function name across `noiseboy_dsp.h`,
`noiseboy_plugin.c`, and `distroy_dsp.h` to check for any OTHER hidden
collisions before they could surface the same way -- none found.

**v0.6.0** -- this round covers a real bug catch plus three feature
requests, and a full rewrite of the Randomize menu using verified
(not guessed) Schwung UI source:

1. **Real bug: a voice could go completely silent for an entire
   note.** At low randomized bit-crush depths (as low as 1-2 bits),
   the quantization step (0.5 for 2-bit) could exceed a Karplus
   voice's actual signal amplitude entirely -- every sample rounded to
   exactly 0, silencing the whole note, not just sounding crunchy.
   First fix attempt (reordering bitcrush vs. the rate-reducer) helped
   but didn't fully resolve it; the real fix is in `bitcrush_process`
   itself, which now guarantees it never rounds a genuinely nonzero
   signal all the way to silence -- nudges to the nearest nonzero
   quantization step instead, preserving the harsh low-bit-depth
   character while eliminating total silence. Caught this via a
   comprehensive sweep (3000 seeds x 8 notes = 24,000 combinations,
   now a permanent test via `make test-sweep`) after a small hand-
   picked sample of seeds initially masked it -- worth remembering
   that per-note randomization needs the same scale of verification as
   everything else, not just a few manually-tried seeds.
2. **Vibrato** ("in the noise and Karplus... gentle, like an acoustic
   instrument") -- implemented as a modulated delay line with linear
   interpolation (the standard, safe way to add pitch modulation
   without touching Karplus's own delay-length-defines-pitch
   internals), applied once per voice after layers mix so it covers
   both layer types. Depth saturates at just 15% of AM Depth's own
   knob travel as requested -- verified the saturation curve hits
   exactly 1.0 at 0.15 and stays there for the rest of the range.
3. **Filter cutoff follows the amplitude envelope**, capped at a 20%
   max brightening at the envelope's own peak, per explicit request
   matching acoustic-instrument behaviour.
4. **Randomize menu rewritten from scratch using verified real
   source**, not documentation. The v0.4.0 draft never actually
   loaded -- turned out to be using relative import paths
   ('../../shared/menu_items.mjs') when real Schwung modules use
   ABSOLUTE filesystem paths (/data/UserData/schwung/shared/...), and
   missing the "raw_ui": true flag in module.json entirely (a real
   module.json field discovered by having the checkout inspected
   directly, not documented anywhere I could find). Rewrote using
   patterns copied directly from three real, working files pulled from
   a local Schwung checkout: tools/ui-test/ui.js, text-test/ui.js, and
   audio_fx/freeverb/ui_chain.js -- host_module_set_param(key,
   stringValue) is confirmed directly from freeverb's real chain UI,
   not guessed. Single jog-wheel click now triggers Randomize.

All four existing test suites plus the new comprehensive sweep re-run
clean (`make test`).

**v0.5.0** -- four items from real playing feedback:

1. **Karplus-Strong now blends the recipe's noise, and rings while
   held.** Excitation is summed from ALL of the recipe's layer colours
   (not just its own single colour) -- normalized average of
   independently-seeded generators, one per colour -- so a Karplus
   layer blends with the rest of the patch rather than sounding like
   an isolated source. Separately: a single pluck decays on its own
   timescale regardless of the envelope's attack time, which meant a
   long attack could make the pluck nearly inaudible by the time the
   slow envelope let it through, and there was no way to get a
   sustained/ringing character. Fixed with a small ongoing noise
   injection while the note is held (stops at release, letting the
   string ring out via its own damping) -- verified with a dedicated
   test: even at maximum attack (200ms), signal stays clearly audible
   deep into the hold (0.235 vs. 0.278 initial peak) rather than
   decaying to near-silence, and still properly releases to full
   silence within a second of note-off.
2. **AM Depth and AM Rate swapped** -- Depth is knob 3, Rate is knob 4
   now, matching the requested order. Updated in both `module.json`'s
   chain_params and the matching `ui_hierarchy` JSON (re-validated the
   hand-built JSON string parses correctly after the edit).
3. **Velocity always brightens the filter** -- up to 2.5x cutoff
   multiplier at maximum velocity, matching how acoustic instruments
   naturally get brighter when played harder. Not knob-controlled;
   always on, per explicit request. Confirmed audible: the existing
   pitch-tracking test's zero-crossing counts jumped noticeably once
   this landed (13->21 low note, 214->355 high note), since the
   velocities used in that test now genuinely brighten the signal.
4. **Randomize single-button-press -- still open.** Feedback was that
   it's still the old multi-step parameter interaction (scroll to it,
   select it, dial from 0 to a nonzero value), which suggests the
   `ui.js` Shadow UI menu added in v0.4.0 either didn't load or got
   bypassed on-device. Needs on-device diagnosis (what the menu screen
   actually shows) before attempting a fix -- see the project's
   conversation history for the open question on this.

All four existing test suites re-run clean after this round's changes,
plus a new dedicated `test_karplus_sustain.c` (also part of the
default `make test`).

**v0.4.1** -- signal chain correction: db-cell's forced-always-present
Noiz slot generates sound regardless of NOISEBOY's own input, so a
noise gate is now placed AFTER db-cell (not before, and not on
NOISEBOY's raw output alone) -- keyed off actual NOISEBOY voice
activity (`noiseboy_any_voice_active`), not signal level, so the
instrument is genuinely silent when nothing is being played rather
than leaving a faint db-cell hiss running. Fast attack (3ms), smoothed
release (150ms) to avoid a click when the last voice stops. Verified
the gate math directly with a standalone simulation before wiring it
in: full level while "active", decays ~84dB down within 1 second of
"inactive".

**v0.4.0** -- five changes from real hardware feedback:
1. Karplus-Strong rebalanced from 50% to 25% per-layer selection odds
   -- the original 50/50 compounded across 1-3 layers to ~71% of
   recipes containing at least one Karplus layer, which is what "on
   most patches" was actually describing. Verified the new ~42% rate
   statistically across 5000 seeds (42.7% actual vs. ~42% computed).
2. Per-voice bitcrusher (random 1-15 bits) and pitch-following
   sample-rate reducer, both randomized fresh on every note-on,
   applied to the voice's mixed layer output. The reducer reuses the
   same `PitchedHold` sample-and-hold mechanism the per-layer pitch
   stage already used, floored at 100Hz and ceiled at Nyquist.
3. AM-linked wavefolder -- fold amount driven by the exact same phase
   as the AM tremolo, so distortion peaks exactly when the AM dip is
   deepest ("comes in and out with the AM").
4. Global always-on tape saturation (envelope-follower compressor +
   fixed drive + tanh) on the final mix, distinct from the existing
   knob-controlled Drive stage -- placed after Drive since its
   built-in compressor tames whatever loudness Drive added.
5. Single-button-press Randomize via a new `ui.js` Shadow UI menu
   (previously only reachable by nudging a chain_param knob from 0).
   This is new ground for the project -- everything before this was
   pure native C/dsp.so with no JavaScript UI layer at all -- built
   from documentation examples, syntax-verified with `node --check`,
   but not confirmed against a real working module's menu.

All DSP changes stress-tested together at maximum AM depth/rate/drive
across 4 simultaneous voices: stayed finite, peaked at a safe 0.77,
and confirmed bitDepth genuinely varies per voice across different
notes (not a fixed/stuck value).

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
