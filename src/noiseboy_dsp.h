#ifndef NOISEBOY_DSP_H
#define NOISEBOY_DSP_H

/* -----------------------------------------------------------------------
 * NOISEBOY -- a chromatically-pitched noise+filter instrument for
 * Schwung/Ableton Move. Strips DISTROY down to just its noise
 * generators and two resonant filters (Moog Ladder, Korg35 MS-20-style
 * LP/HP), reused verbatim from distroy_dsp.c for sonic consistency with
 * DISTROY -- the stated use case is running NOISEBOY into DISTROY on
 * the same Move set, so sharing filter/noise character is intentional,
 * not just convenient.
 *
 * Each voice is 1-3 randomly-chosen layers, decided once per module
 * instantiation (fixed for that load, not re-randomized per note --
 * though see noiseboy_randomize_recipe() for re-rolling on demand
 * without a full reload). Each layer is independently either:
 *   - filtered noise: a noise generator (random colour) through a
 *     resonant lowpass filter (randomly Moog Ladder or Korg35 LP --
 *     the highpass option was tried and dropped, see FilterKind's
 *     comment) tuned to the played note's frequency, PLUS a sample-
 *     and-hold pitch stage layered in before the filter (see
 *     PitchedHold) for a stronger, more clearly pitched result than
 *     filter resonance alone, or
 *   - Karplus-Strong: a noise-excited, damped delay line whose length
 *     itself sets the pitch (the classic plucked-string algorithm),
 *     picked randomly per the explicit request to add it as a second
 *     pitch-defining method alongside filter tracking.
 * Per explicit clarification: filtered-noise layers ALWAYS track the
 * played pitch via their filter's cutoff; Karplus-Strong layers get
 * their pitch from delay length instead (running them through a
 * SEPARATE pitch-tracked filter on top would just fight the same job
 * twice).
 * ---------------------------------------------------------------------*/

#include <stddef.h>
#include "distroy_dsp.h" /* SimpleNoise, NoiseGen, NoiseColour, MoogLadder, Korg35LP, Korg35HP now come from here -- consolidated to one shared source when DBCELL processing was added, rather than keeping NOISEBOY's own verbatim-copied duplicates alongside a second copy pulled in for DBCELL. Same structs/functions as before, just declared once instead of twice. */

#define NOISEBOY_MAX_VOICES 8
#define NOISEBOY_MAX_LAYERS 3
#define NOISEBOY_KS_MAX_SAMPLES 2400 /* enough delay-line length for the lowest supported note (~A0, 27.5Hz) at up to 48kHz-ish sample rates, with headroom */

/* ---- New for NOISEBOY ---- */

/* Karplus-Strong plucked-string algorithm: a delay line of length
 * sample_rate/freq_hz, seeded with a burst of noise at note-on, fed
 * back through a simple one-pole damping filter (lower damping =
 * brighter/longer sustain, higher = darker/shorter -- the classic
 * "string material" control). Pitch comes from delay length itself,
 * not from any separate resonant filter.
 *
 * Per direct feedback: (1) the excitation should be built from the
 * SUM of whatever noise colours the rest of the recipe's layers are
 * using, not just this layer's own single colour, so a Karplus layer
 * blends with the rest of the patch's noise palette rather than
 * sounding like an isolated source; (2) a single pluck decays away on
 * its own timescale regardless of the envelope's attack time, so a
 * long attack could make the pluck nearly inaudible by the time the
 * envelope let it through, and there was no way to get a sustained,
 * ringing character rather than a single decaying pluck. Both
 * addressed by karplus_process taking an ongoing small noise feed
 * (same multi-colour-sum source) applied continuously while the note
 * is held -- keeps the string genuinely alive/ringing for as long as
 * it's held ("short sustained notes that ring out"), and since the
 * string stays energized rather than just decaying from one initial
 * burst, it also fixes the long-attack-makes-it-inaudible problem as
 * a side effect. */
typedef struct {
    double buffer[NOISEBOY_KS_MAX_SAMPLES];
    int length;       /* current delay length in samples, set per note */
    int writePos;
    double damping;   /* 0-1, one-pole feedback damping coefficient */
    double lastOut;   /* one-pole filter state */
} KarplusString;

void karplus_init(KarplusString *k);
/* Reseeds the delay line and sets its length for freq_hz at the given
 * sample rate -- call once on note-on. The excitation burst is built
 * by summing `numSources` independently-seeded noise generators (one
 * per colour in sourceColours), normalized by numSources -- see the
 * struct comment above for why this replaced a single dedicated
 * NoiseGen. rngState seeds the temporary generators created inside
 * this call. */
void karplus_pluck(KarplusString *k, double freq_hz, double sample_rate, unsigned int *rngState, const NoiseColour *sourceColours, int numSources, double dampingAmount);
/* sustainFeedSample/sustainAmount: an ongoing small noise injection
 * (sustainAmount > 0 while the note is held, 0 after release) that
 * keeps the string ringing rather than only decaying from the initial
 * pluck -- see the struct comment above. Pass sustainAmount=0 for the
 * original single-decaying-pluck behaviour if ever needed. */
double karplus_process(KarplusString *k, double sustainFeedSample, double sustainAmount);

/* Sample-and-hold "pitched noise" stage -- an additional, distinct
 * pitch mechanism layered in on top of resonant filter tracking, per
 * direct feedback that filter tracking alone wasn't reading as clearly
 * pitched. Holds each fresh noise sample for a duration derived from
 * the played note's frequency (like a bitcrushed/sample-rate-reduced
 * source, but the reduction rate itself tracks the note), giving a
 * buzzy, more obviously pitched character that reinforces the
 * filter's resonant peak rather than replacing it. */
typedef struct {
    double heldValue;
    double phase;
} PitchedHold;

void pitchedhold_init(PitchedHold *h);
double pitchedhold_process(PitchedHold *h, double newSample, double freqHz, double sampleRate, double holdMultiplier);

/* Simple bit-depth quantizer, per explicit request for a per-voice
 * bitcrusher (random 1-15 bits, applied to the voice's mixed layer
 * output). Stateless -- just a quantization formula, no persistent
 * state needed between calls. */
double bitcrush_process(double x, int bits);

/* Simple reflective wavefolder, per explicit request to add one to
 * the AM stage that "comes in and out with the AM" -- amount 0 = no
 * folding (passthrough), higher amounts fold the signal back on
 * itself repeatedly when it exceeds +-1, producing rich, FM-like
 * harmonics rather than hard clipping. */
double wavefold_process(double x, double amount);

/* Vibrato via a small modulated delay line with linear interpolation
 * -- the classic, safe way to add pitch modulation to an arbitrary
 * signal without touching the source generator's own internals
 * (particularly important for the Karplus-Strong layers, where
 * directly modulating the delay-line length that defines pitch would
 * risk clicks/glitches from reading uninitialized buffer regions).
 * Applied once per voice, after layers are mixed, per explicit
 * request for vibrato "in the noise and Karplus" -- since both are
 * already combined into one signal by then, one instance covers both
 * layer types. */
#define NOISEBOY_VIBRATO_BUFFER_SIZE 512
typedef struct {
    double buffer[NOISEBOY_VIBRATO_BUFFER_SIZE];
    int writePos;
} VibratoDelay;

void vibrato_init(VibratoDelay *v);
/* phase01: 0-1, the SAME phase driving AM/wavefold (see NoiseboyEngine
 * voice processing) so vibrato pulses in sync with the tremolo rather
 * than drifting independently. depthSamples: max delay-time deviation
 * in samples -- kept intentionally small by the caller (see
 * noiseboy_process's own comment) for a gentle, acoustic-instrument-
 * like vibrato rather than a dramatic pitch wobble. */
double vibrato_process(VibratoDelay *v, double x, double phase01, double depthSamples);

typedef enum { LAYER_FILTERED_NOISE = 0, LAYER_KARPLUS_STRONG } LayerType;
/* FILTER_KORG_HP is intentionally no longer selected by the
 * randomizer (see randomizeCell-equivalent logic in the .c file) --
 * per direct feedback/diagnosis, a highpass filter tuned to the played
 * pitch removes energy AT that pitch rather than emphasizing it
 * (Korg35HP is derived as input-minus-its-own-resonant-lowpass-core,
 * so at high resonance it creates a NOTCH at the tracked frequency,
 * not a peak), working against "sounds pitched" rather than for it.
 * The enum value and its code path are left in place rather than
 * deleted, matching this project family's convention of keeping
 * superseded options around for a possible future revert. */
typedef enum { FILTER_MOOG = 0, FILTER_KORG_LP, FILTER_KORG_HP } FilterKind;

typedef struct {
    LayerType type;

    /* Filtered-noise layer state -- raw source generation only now.
     * The per-layer filter (Moog/Korg35LP/Korg35HP switch,
     * resonance-per-layer, the pre-filter PitchedHold stage) has been
     * REMOVED per explicit restructuring request: source generation
     * (noise/Karplus) -> bitcrush/rate-reduce -> a single VOICE-LEVEL
     * pitch-tracking filter -> envelope -> output filter, not a filter
     * per layer before mixing. See Voice's own pitchFilter* fields for
     * where that single filter now lives. */
    NoiseGen noiseGen;
    NoiseColour colour;

    /* Karplus-Strong layer state. */
    KarplusString karplus;

    /* Small per-layer detune (in cents), still used for Karplus-type
     * layers' own pitch (each Karplus layer's delay length is tuned
     * slightly differently for chorused richness when multiple layers
     * stack) -- no longer relevant to filtered-noise layers now that
     * they have no per-layer filter of their own to detune. */
    double detuneCents;

    /* Smoothed sustain-feed amount for Karplus layers -- avoids an
     * instantaneous on/off snap in the string's excitation level
     * exactly at the release boundary (was a hard gate: 0.02 while
     * held, 0.0 the instant released). A discontinuous excitation
     * change happening exactly when release begins is one plausible
     * contributor to a reported "pitch tied to envelope" perception --
     * smoothing it removes that specific discontinuity regardless of
     * whether it's the full explanation. */
    double sustainAmountSmoothed;
} Layer;

typedef struct {
    int active;
    int midiNote;
    double velocity01;
    double freqHz;

    Layer layers[NOISEBOY_MAX_LAYERS];
    int numLayers;

    /* Simple gate envelope -- fast attack while held, fast release on
     * note-off, rather than a full ADSR: "only turns on with keypress"
     * calls for a gate, not a sustained pad. Attack/Release TIME are
     * still knob-controllable (see NoiseboyParams) so it isn't a
     * hard instant on/off -- just smoothed enough to avoid clicks. */
    double envLevel;
    int gateOpen;

    /* Per-voice amplitude modulation phase -- each voice runs its own
     * AM phase (not perfectly in sync with other voices), which reads
     * more like an ensemble of slightly-differently-wobbling noise
     * sources than one synchronized tremolo. */
    double amPhase;

    /* Vibrato, applied once per voice after layers mix -- see
     * VibratoDelay's own comment. Duplicated L/R (see this project's
     * stereo panning feature, added with the Detune knob) -- separate
     * instances per channel are required for a genuine stereo image;
     * running the same filter state on both channels would collapse
     * any panning right back to mono. */
    VibratoDelay vibratoL;
    VibratoDelay vibratoR;

    /* Voice-level pitch-tracking filter -- per explicit restructuring
     * request, this replaces the old per-layer filters entirely.
     * Signal chain is now: sources (noise+Karplus, up to 3 layers) ->
     * bitcrush/rate-reduce -> THIS FILTER -> AM/wavefold -> amplitude
     * envelope -> output filter. High resonance, tracks v->freqHz
     * directly via knob 1 (offset) and knob 2 (resonance).
     * Deliberately carries NO envelope or velocity modulation on its
     * cutoff -- envelope-modulated cutoff on a pitch-tracking filter
     * was a real, reported bug (pitch audibly decaying over a long
     * release, since the old per-layer filter's cutoff followed
     * envLevel). Filter type (Moog/Korg35LP) randomized fresh per
     * note, matching this project's established per-note variety
     * pattern -- Korg35HP still deliberately excluded from
     * the choice, same reasoning as before (a highpass tuned to the
     * played pitch works against sounding pitched, not for it).
     * Duplicated L/R, same reasoning as vibrato above. */
    FilterKind pitchFilterKind;
    MoogLadder pitchFilterMoogL;
    MoogLadder pitchFilterMoogR;
    Korg35LP pitchFilterKorgLpL;
    Korg35LP pitchFilterKorgLpR;

    /* OUTPUT FILT -- the final stage, per explicit spec. Rebuilt as a
     * TILT filter, per direct request: knob centred (12 o'clock) =
     * no filtering at all. Turning left engages a lowpass whose
     * cutoff falls as you keep turning left, eventually silencing the
     * signal from the top down. Turning right engages a highpass
     * whose cutoff rises as you keep turning right, eventually
     * silencing the signal from the bottom up -- "make it disappear
     * either direction". Only one side is ever active at a time (never
     * both at once) -- a true tilt, not two independent filters
     * stacked. No velocity or envelope influence, no resonance --
     * purely knob-controlled sweep in either direction, deliberately
     * simple and predictable. Duplicated L/R, same reasoning as
     * vibrato above. */
    MoogLadder outputLowpassL;
    MoogLadder outputLowpassR;
    Korg35HP outputHighpassL;
    Korg35HP outputHighpassR;
} Voice;

/* Layer RECIPE, decided once at module instantiation (not per-note) --
 * per explicit spec ("every time you instantiate, its a randomized
 * noise block"). Every voice is initialized from this same shared
 * recipe on note-on (same layer count/types/colours), giving each note
 * fresh independent DSP state (noise-generator/Karplus-string
 * instances) but a consistent "instrument identity" across the whole
 * session, rather than re-randomizing per note. Filter choice is NOT
 * part of the recipe anymore -- the filter moved to voice level and is
 * randomized fresh per note instead (see Voice's own pitchFilterKind),
 * matching this project's established per-note variety pattern
 * rather than being fixed for the whole session. */
typedef struct {
    LayerType type;
    NoiseColour colour;      /* filtered-noise layers only */
    double detuneCents;
    double dampingAmount01;  /* Karplus-Strong layers only */
} LayerRecipe;

/* Knob-controlled parameters, shared across all voices (not per-voice
 * state) -- set via set_param in the Schwung plugin wrapper, mapped to
 * knobs 1-8 (plus three additional chain_params beyond the 8 physical
 * knobs -- drive, master level, and randomize-trigger -- still
 * reachable via the module's parameter menu). */
typedef struct {
    double filterCutoffOffset01;  /* knob 1: brightens/darkens the pitch-tracked filter cutoff, -1..1 mapped from 0..1 */
    double filterResonance01;     /* knob 2 */
    double amRateHz;              /* knob 3: 0.1-20 Hz */
    double amDepth01;             /* knob 4: 0 = no AM, 1 = full tremolo */
    double attackMs;              /* knob 5: 0.5-200 ms */
    double releaseMs;             /* knob 6: 5-2000 ms */
    double detuneSpread01;        /* knob 7: scales each layer's per-layer detuneCents, 0 = unison, 1 = full spread */
    /* knob 8: deliberately the LAST knob in the signal path, per
     * explicit request -- controls OUTPUT FILT (the post-mix,
     * velocity-brightened, zero-resonance lowpass in noiseboy_process),
     * making it "the final knob to control audible sound" rather than
     * a level control. 0.5 = neutral (no change to the velocity-driven
     * base range); away from 0.5 multiplies that range up or down,
     * same pattern as filterCutoffOffset01. Master level moved off
     * knob 8 to a menu-only position (see masterLevel01 below) to make
     * room for this. */
    double outputFilterFreq01;
    double drive01;               /* chain_param: single shared drive/saturation stage on the final mix -- see noiseboy_process */
    /* Moved off knob 8 (was there through v0.7.0) to make room for
     * outputFilterFreq01 -- still fully controllable, just via the
     * parameter menu rather than a dedicated physical knob now. */
    double masterLevel01;
} NoiseboyParams;

/* Global, always-on tape-style saturation stage on the final mixed
 * output -- per explicit request ("output should have a tape
 * saturation on always that compresses, drives, and saturates the
 * final sound... on the global output, so all voices get this").
 * Simple envelope-follower compressor feeding a driven tanh
 * saturation stage -- not a literal tape-machine model (no wow/
 * flutter, no head-bump EQ), just the "compresses, drives, saturates"
 * character requested, kept cheap given NOISEBOY's CPU-light mandate
 * (same philosophy as db-cell's own drive stage). */
typedef struct {
    double envelope;
} TapeSaturation;

void tapesat_init(TapeSaturation *t);
double tapesat_process(TapeSaturation *t, double x, double sampleRate);

typedef struct {
    double sampleRate;
    unsigned int rngState;
    Voice voices[NOISEBOY_MAX_VOICES];
    NoiseboyParams params;

    /* The randomized recipe decided once at instantiation -- see
     * LayerRecipe's comment. */
    LayerRecipe recipe[NOISEBOY_MAX_LAYERS];
    int numRecipeLayers;

    /* Tracks the previous value of the "randomize" trigger param so
     * set_param can detect a rising edge (0 -> nonzero) and re-roll
     * the recipe exactly once per press, rather than re-randomizing
     * continuously while a bound knob is mid-turn. */
    int lastRandomizeTriggerRaw;

    /* Global output stage -- always on, not knob-controlled, applied
     * once to the final mix after all voices are summed (and after
     * DBCELL processing, in the plugin wrapper). Duplicated L/R --
     * a single shared instance called sequentially for L then R was
     * tried first (simpler, one instance) but this project's own
     * stereo test caught a real bug in that: the envelope follower's
     * internal state mutates on each call, so processing L first
     * changes what state the R call sees, producing a small but real
     * L/R difference even at Detune=0 where the input is otherwise
     * identical -- exactly the "should collapse to true mono" case
     * the stereo feature explicitly promises. Independent instances
     * fix this properly, matching every other voice-level stage. */
    TapeSaturation tapeSatL;
    TapeSaturation tapeSatR;

    /* Smoothed knob-8 (Output Filt) value -- params.outputFilterFreq01
     * is set instantly by set_param on each discrete MIDI CC step as
     * the knob turns, which without smoothing produces audible
     * "zipper" stepping in the filter's cutoff (especially noticeable
     * here given the tilt filter's wide, silence-at-both-extremes
     * sweep range). This glides toward the target each sample instead
     * of jumping to it -- see its use in noiseboy_process. */
    double outputFilterFreqSmoothed01;
} NoiseboyEngine;

void noiseboy_engine_init(NoiseboyEngine *e, double sampleRate, unsigned int seed);
/* Re-rolls the recipe (layer count/types/colours/filters/detune) using
 * the engine's own ongoing RNG state, WITHOUT reinitializing sample
 * rate, params, or any currently-sounding voices -- per explicit
 * request for a way to get a new randomized set without reloading the
 * whole module. Takes effect for notes played AFTER this call; voices
 * already sounding keep whatever recipe they started with (retroactively
 * changing an in-flight voice's DSP state would click/glitch). */
void noiseboy_randomize_recipe(NoiseboyEngine *e);
void noiseboy_note_on(NoiseboyEngine *e, int midiNote, double velocity01);
void noiseboy_note_off(NoiseboyEngine *e, int midiNote);
void noiseboy_all_notes_off(NoiseboyEngine *e);
/* Whether any voice is currently active (sounding or in release) --
 * per explicit request, used to key a noise gate placed AFTER db-cell
 * in the plugin wrapper's output chain, since db-cell's own forced-
 * always-present Noiz slot generates sound regardless of NOISEBOY's
 * own input and would otherwise leak through even with zero voices
 * playing. */
int noiseboy_any_voice_active(const NoiseboyEngine *e);
/* Mono convenience wrapper around noiseboy_process_stereo (returns
 * (L+R)/2) -- kept for the existing test suite and any caller that
 * doesn't need real stereo output. The plugin wrapper uses the stereo
 * version directly; this one exists so nothing that predates the
 * stereo panning feature needed to change. */
double noiseboy_process(NoiseboyEngine *e);
/* Real stereo processing -- per explicit request, Detune (knob 7) now
 * spreads the stereo image: filtered-noise layers pan to a fixed
 * per-layer position (reusing each layer's existing randomized
 * detuneCents, scaled by the Detune knob), Karplus layers auto-pan
 * back and forth at the AM rate (knob 4) instead, also scaled by
 * Detune. At Detune=0 (knob fully left) this collapses back to mono,
 * identical to noiseboy_process's own output on both channels. */
void noiseboy_process_stereo(NoiseboyEngine *e, double *outL, double *outR);

#endif /* NOISEBOY_DSP_H */
