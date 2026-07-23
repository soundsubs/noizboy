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

#define NOISEBOY_MAX_VOICES 4
/* 4, not 3 -- per explicit request, LOOP is restored as a 4th fixed
 * source (2 noise + 1 Karplus + 1 Loop), not squeezed into the
 * existing 3. See LayerType's own comment and LoopSource's own
 * comment for the full LOOP redesign/revert. */
#define NOISEBOY_MAX_LAYERS 4
#define NOISEBOY_KS_MAX_SAMPLES 2400 /* enough delay-line length for the lowest supported note (~A0, 27.5Hz) at up to 48kHz-ish sample rates, with headroom */
/* Per explicit redesign -- see LoopSource's own comment for the full
 * history. Fixed buffer length now, matching this feature's own
 * original design before any length knob ever existed -- "it doesn't
 * need a knob to decide length, it should only be tracking note
 * number." 8000 samples (~167ms at 48kHz) -- at middle C (playbackRate
 * 1.0) that's the loop's own period; other notes transpose this by
 * playback speed the same way a sample player would. */
#define NOISEBOY_LOOP_FIXED_SAMPLES 8000
#define NOISEBOY_ENGINE_INIT_MAGIC 0x4E42214Fu /* arbitrary, just needs to be a value fresh/garbage stack memory won't plausibly already contain -- see NoiseboyEngine's own initMagic field comment */

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

/* TAPE WOBBLE -- Mellotron-style noise modulation, per explicit
 * request: "Learning from my synthesizer programming trying to make a
 * sound like a Mellotron, lets modulate the filter frequency,
 * resonance, and output level by noise, but only about 1%-5% so that
 * it's not overwhelming and disturbs pitch too much. This should make
 * it sound more like tape playback."
 *
 * ONE shared, slow, smoothly-wandering noise source per voice drives
 * all three modulation targets together (filter cutoff, resonance,
 * output level), rather than three independent noise sources -- real
 * tape speed fluctuation (wow/flutter) is a single underlying cause
 * with several correlated effects (pitch dips slightly, and the
 * filter/level riding along with it reads as coherent "tape wobble"
 * rather than three unrelated random textures happening to overlap).
 *
 * "Noise" here means a slowly-wandering random value, NOT audio-rate
 * noise -- audio-rate modulation of a filter's cutoff would just add
 * broadband texture/harshness, not the slow, organic instability of
 * real tape wow/flutter. Implemented as heavily lowpassed white noise
 * (a one-pole filter with a very low, sub-audio cutoff), producing a
 * smoothly wandering value roughly in [-1, 1] that changes over
 * fractions of a second to a few seconds, not sample-to-sample. */
typedef struct {
    double state;
    unsigned int rngState;
} TapeWobble;

void tape_wobble_init(TapeWobble *w, unsigned int seed);
/* Call once per sample; returns the current wandering value (roughly
 * -1 to 1, actual amplitude normalized to compensate for the heavy
 * variance reduction the internal smoothing filter would otherwise
 * cause -- see this function's own implementation comment for a real
 * bug that normalization fixed). rateHz controls how fast it wanders
 * (the one-pole lowpass's own cutoff) -- lower is slower/smoother. */
double tape_wobble_process(TapeWobble *w, double rateHz, double sampleRate);

/* LAYER_LOOP restored per explicit request -- see LoopSource's own
 * comment below for the full redesign/revert history: was removed
 * from the type rotation when the mixer became fixed at 3 sources
 * (2 noise + 1 Karplus), then briefly became a totally different,
 * post-filter, non-pitch-transposing effect, then reverted back to
 * this original per-layer, pitch-transposing, pre-filter design (with
 * a knob-controlled master length), then had that length knob removed
 * again ("it doesn't need a knob to decide length, it should only be
 * tracking note number... how a real tape sampler would work") in
 * favor of a fixed technical buffer length and a knob now controlling
 * DEPTH of the loop's own sustain/decay shape instead. */
typedef enum { LAYER_FILTERED_NOISE = 0, LAYER_KARPLUS_STRONG, LAYER_LOOP } LayerType;
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

/* LOOP -- a per-layer, PRE-filter sound-generation "engine" (like
 * Filtered Noise and Karplus), REDESIGNED again per direct feedback:
 * "it doesn't need a knob to decide length, it should only be
 * tracking note number. This is how a real tape sampler would work."
 * Length is no longer knob-controlled at all -- back to a FIXED
 * technical buffer size (NOISEBOY_LOOP_FIXED_SAMPLES, matching this
 * feature's own original design before any length knob existed), with
 * ONLY the played note's pitch controlling playback speed through it
 * (playbackRate = freqHz / referenceFreq, exactly like a sample player
 * pitching a fixed recording by playback speed -- a real tape sampler
 * has one fixed piece of tape; how fast you play it back is what
 * changes, not the tape's own length). Captured INSTANTLY (a tight
 * fill loop, like karplus_pluck's own initial burst), not recorded in
 * real time -- the full, already-pitch-transposed content is available
 * from the very first sample of the note, no capture-phase delay.
 *
 * Knob 4 ("Loop Length" before) is now LOOP DEPTH, per direct request
 * -- "The knob should indicate only DEPTH of LOOP, as AM used to do!"
 * -- an intensity control in the same spirit as this project's old AM
 * Depth knob, not a length control at all anymore.
 *
 * Envelope shape, completely redesigned: each loop pass stays at FULL,
 * SUSTAINED level for 97% of its length, then drops toward silence
 * over the final 3% before jumping back to full at the next pass --
 * inverted from the previous design's continuous decay-throughout
 * shape. DEPTH controls how FAR that final-3% dip actually goes: at
 * depth=0, no dip at all (flat, drone-like, sustained the whole way
 * through with no audible loop seam); at depth=1, the dip reaches true
 * silence by the end of each pass (the full, dramatic "chop" and
 * restart); intermediate depths land the dip partway between "barely
 * audible" and "full silence". This is the loop's own equivalent of
 * AM Depth's 0=off/1=full-swing behavior, just shaping the loop's own
 * repeating envelope instead of an external tremolo oscillator.
 *
 * Nearest-neighbor read (not interpolated) -- matches this project's
 * established "the artifacts are the point" philosophy for this kind
 * of lo-fi, sample-player-style pitch shifting.
 *
 * Mono (one buffer, not stereo L/R) -- like Filtered Noise and
 * Karplus, this is a raw SOURCE that gets mixed and panned like any
 * other layer. Heap-allocated (a several-thousand-sample buffer
 * embedded directly in Voice, times NOISEBOY_MAX_VOICES, would
 * overflow a stack-declared NoiseboyEngine -- see this project's own
 * history on that), though now much smaller than either previous
 * design's buffer given the fixed length is short. */
typedef struct {
    double *buffer;
    double readPos;             /* fractional read position, nearest-neighbor read */
    double playbackRate;        /* freqHz / referenceFreq, set once at note-on */
} LoopSource;

/* Allocates buffer -- NOISEBOY_LOOP_FIXED_SAMPLES doubles. Called
 * once per voice at engine init time (NOT at every note-on, which
 * would mean allocating on every note -- wasteful and not real-time-
 * safe for a hot path). Returns 0 on allocation failure. */
int loop_source_alloc(LoopSource *lp);
/* Frees what loop_source_alloc allocated. */
void loop_source_free(LoopSource *lp);
/* Captures the buffer INSTANTLY (raw noise, filled in a tight loop --
 * see this struct's own comment for why), and sets the pitch-
 * transposing playback rate. Called once at note-on, analogous to
 * karplus_pluck's one-time excitation. Buffer length is always
 * NOISEBOY_LOOP_FIXED_SAMPLES now -- no knob or other input decides
 * it anymore. */
void loop_capture(LoopSource *lp, unsigned int *rngState, double freqHz, double referenceFreqHz);
/* Read-only: computes the SAME envelope gain loop_process is about to
 * apply, WITHOUT advancing readPos or touching any state -- lets a
 * caller apply this loop's envelope shape somewhere ELSE too (see
 * noiseboy_process_stereo's own comment for why: LOOP's own mix level
 * is independently randomized like any other source, so if it happens
 * to land low/near-silent, DEPTH's own dip has nothing audible left to
 * shape -- "the LOOP won't do anything," per direct report). Calling
 * this does not replace calling loop_process -- loop_process still
 * needs to run once per sample to actually advance the loop and
 * produce LOOP's own source audio; this is an ADDITIONAL read of the
 * same envelope value for a second purpose. */
double loop_envelope_gain(const LoopSource *lp, double depth01);
/* Reads back one sample per call, nearest-neighbor, with the
 * sustained-then-decay envelope applied (97% flat, final 3% dips
 * toward silence, dip amount set by depth01) -- see this struct's own
 * comment for the full shape. depth01: 0 = no dip at all, 1 = dip
 * reaches true silence. */
double loop_process(LoopSource *lp, double depth01);

typedef struct {
    LayerType type;

    /* Filtered-noise layer state -- raw source generation only, plus
     * (below) a small release-only darkening stage. The per-layer
     * FILTER (Moog/Korg35LP/Korg35HP switch, resonance-per-layer, the
     * pre-filter PitchedHold stage) was REMOVED per explicit
     * restructuring request: source generation (noise/Karplus) ->
     * bitcrush/rate-reduce -> a single VOICE-LEVEL pitch-tracking
     * filter -> envelope -> output filter, not a filter per layer
     * before mixing. See Voice's own pitchFilter* fields for where
     * that single filter now lives. */
    NoiseGen noiseGen;
    NoiseColour colour;

    /* Release-only darkening, per direct feedback: "Karplus sounds
     * very plucked because of its nature [the string's own natural
     * damping darkens it as it decays]... noise does not sound
     * plucked on releases" -- filtered-noise layers have no equivalent
     * mechanism, since their timbre never evolves, only their shared
     * envelope's volume. This is a simple one-pole lowpass (same
     * leaky-integrator technique NOISE_RED already uses elsewhere in
     * this project) applied to the RAW noise source, progressively
     * engaging only during release. Deliberately placed BEFORE the
     * voice-level pitch-tracking filter (in signal flow, not in this
     * struct), not instead of it or coupled to its cutoff -- darkening
     * the SOURCE feeding a filter is not the same as modulating the
     * filter's own cutoff, and doesn't move the pitch-defining
     * resonant peak at all (unlike the earlier, reverted attempt to
     * tie the pitch filter's own cutoff to envelope, which caused a
     * real, reported pitch-decay bug). */
    double releaseDarkenState;

    /* Karplus-Strong layer state. */
    KarplusString karplus;

    /* Loop layer state -- see LoopSource's own comment. */
    LoopSource loop;

    /* Small per-layer detune (in cents), still used for Karplus-type
     * layers' own pitch (each Karplus layer's delay length is tuned
     * slightly differently for chorused richness when multiple layers
     * stack) -- no longer relevant to filtered-noise layers now that
     * they have no per-layer filter of their own to detune. */
    double detuneCents;

    /* This source's mix level (0-1), per the fixed-source-count mixer
     * restructuring (now 4 sources: 2 filtered-noise, 1 Karplus, 1
     * Loop) -- see LayerRecipe's own comment. Copied from the recipe
     * at voice_start (not randomized per-note -- fixed for the whole
     * session until the user globally re-randomizes). Applied when
     * layers are mixed together in noiseboy_process_stereo. */
    double mixLevel01;

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

    /* Deferred voice stealing -- avoids the audible click from voice
     * stealing. When a still-sounding voice is repurposed for a new
     * note (which happens more often with longer release times, since
     * voices stay "active" -- and therefore ineligible to be picked as
     * a free voice -- for longer), immediately resetting ALL of that
     * voice's state (Karplus re-plucked, filters re-initialized to
     * zero, envelope reset to 0) is a real, measured discontinuity
     * (confirmed via a dedicated test: up to a 0.17 single-sample
     * jump, when anything under ~0.01 is inaudible) -- a genuine,
     * reported click/pop, worse with longer release simply because
     * longer release means more frequent stealing.
     *
     * A first attempt just faded the NEW note's output in from 0
     * instead of resetting instantly -- this only masks half the
     * problem, since the OLD voice's contribution to the mix still
     * vanishes the instant its state is overwritten, regardless of how
     * gently the new one fades in. The actual fix: when a steal is
     * needed, don't reset the voice immediately at all. Instead, force
     * it into a fast release (overriding the user's own Release knob
     * just for this transition) and PARK the new note's info here.
     * Once the old sound has genuinely decayed to near-silence, THEN
     * perform the real reset -- the discontinuity still happens, but
     * only once the audible level is already near-zero, where it's
     * inaudible. Adds a few milliseconds of latency to a stolen note's
     * onset, not perceptible in practice. */
    int pendingSteal;
    int pendingMidiNote;
    double pendingVelocity01;
    /* Set if the pending note gets released (note-off) before its
     * deferred steal even completes -- a quick staccato note that
     * happens to land on a steal. Without this, noiseboy_note_off has
     * no voice to find yet (this one hasn't been assigned via
     * voice_start), so the note-off would be silently lost and the
     * pending note would sustain in full once it eventually starts,
     * even though the player had already released it. Checked once
     * voice_start actually runs for the pending note -- if set, it
     * immediately goes into release instead of sustain.
     *
     * REAL BUG FIXED HERE, found while investigating a related report
     * ("fast playing... only triggers every other note"): the ORIGINAL
     * fix for this immediately set gateOpen=0 in the very same sample
     * voice_start ran, before the envelope had computed even once with
     * gateOpen=1. Since the attack target IS gateOpen's value, envLevel
     * never rose above 0 at all -- the note was completely, silently
     * dropped, not just released early. This is exactly what a fast,
     * short pad tap under voice-stealing load would trigger every
     * time. minHoldSamplesRemaining fixes this: keeps the voice
     * genuinely held for a brief, fixed window so its attack envelope
     * gets a real chance to rise (and be audible) before release is
     * actually applied, rather than skipping the attack entirely. */
    int pendingNoteReleased;
    int minHoldSamplesRemaining;

    /* Per-voice modulation phase -- each voice runs its own phase (not
     * perfectly in sync with other voices), which reads more like an
     * ensemble of slightly-differently-wobbling sources than one
     * synchronized effect. Formerly drove AM/wavefold directly, then
     * (briefly, during LOOP's post-filter redesign) synced to that
     * version's own live playback rate -- neither applies anymore
     * (AM/wavefold stayed removed even after LOOP reverted back to a
     * pre-filter source; the post-filter LOOP's live-rate concept
     * doesn't exist in the reverted design either). Now just a fixed,
     * modest internal rate driving Karplus's own auto-pan (see
     * noiseboy_process_stereo's own panning comment) -- no live
     * knob controls it. */
    double amPhase;

    /* Bitcrush + pitch-following sample-rate reduction, REINTRODUCED
     * per explicit request, applied post-mixer (after layers sum into
     * voiceSum), pre-filter (before the pitch-tracking filter) --
     * i.e. exactly the same position this occupied before v0.10.0
     * removed it entirely.
     *
     * WORTH FLAGGING DIRECTLY, not just implementing quietly: this is
     * the SAME position that v0.10.0's removal specifically measured
     * as harmful to pitch-tracking accuracy -- with the rate-reducer
     * running before the filter, it can compete with/mask the
     * filter's own resonant peak. That removal's own before/after
     * measurement (this project's zero-crossing test) showed pitch-
     * tracking accuracy jump from ~3.5x to ~7.2x (true value 16x for a
     * 4-octave span) once removed. Reintroducing it here trades some
     * of that accuracy back for the requested crunch/character --
     * that's an explicit, informed choice being made here per direct
     * request, not an oversight repeating past history blindly.
     * bitDepth/rateReducerMultiplier randomized fresh per note (not
     * recipe-level), matching this feature's own prior design before
     * removal. See noiseboy_process_stereo's own comment for exactly
     * where in the chain this now sits. */
    int bitDepth;
    double rateReducerMultiplier;
    PitchedHold rateReducerL;
    PitchedHold rateReducerR;

    /* Tape wobble -- see TapeWobble's own header comment. Per-voice
     * (not shared across voices) so simultaneous notes wobble
     * independently rather than in perfect lockstep, same reasoning as
     * amPhase's own randomized starting point. */
    TapeWobble wobble;

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
 * rather than being fixed for the whole session.
 *
 * RESTRUCTURED per explicit request: previously, the recipe randomized
 * BOTH how many layers (1-3) AND what type each one was (filtered-
 * noise/Karplus/Loop). Now always exactly NOISEBOY_MAX_LAYERS (3)
 * sources, fixed type by index -- layers 0 and 1 are always filtered-
 * noise, layer 2 is always Karplus-Strong. Only each source's MIX
 * LEVEL (mixLevel01, 0-1) is randomized now, not its type or whether
 * it's present at all. This is a deliberate simplification, not an
 * oversight -- LAYER_LOOP (the third sound-generation method added
 * last session) has no place in this fixed 2-noise-1-karplus
 * structure as explicitly specified; its own code (LoopSource,
 * loop_capture/loop_process) is left fully intact, just unused here,
 * per this project's established "keep superseded options, don't
 * delete" convention. */
typedef struct {
    LayerType type;
    NoiseColour colour;      /* filtered-noise layers only */
    double detuneCents;
    double dampingAmount01;  /* Karplus-Strong layers only */
    double mixLevel01;        /* 0-1, this source's blend level in the fixed 3-source mix */
} LayerRecipe;

/* Knob-controlled parameters, shared across all voices (not per-voice
 * state) -- set via set_param in the Schwung plugin wrapper, mapped to
 * knobs 1-8 (plus three additional chain_params beyond the 8 physical
 * knobs -- drive, master level, and randomize-trigger -- still
 * reachable via the module's parameter menu). */
typedef struct {
    double filterCutoffOffset01;  /* knob 1: brightens/darkens the pitch-tracked filter cutoff, -1..1 mapped from 0..1 */
    double filterResonance01;     /* knob 2 */
    /* Knobs 3 and 4, redesigned several times -- see LoopSource's own
     * comment for the full LOOP history. Knob 3 is DRIVE (moved from
     * menu-only once LOOP no longer needed a dedicated blend knob).
     * Knob 4 was briefly "LOOP Length" (a knob-controlled buffer
     * length), now LOOP DEPTH instead, per direct request: "it doesn't
     * need a knob to decide length, it should only be tracking note
     * number... The knob should indicate only DEPTH of LOOP, as AM
     * used to do!" Length is fixed (NOISEBOY_LOOP_FIXED_SAMPLES) now;
     * this knob controls how far the loop's own sustained-then-decay
     * envelope dips each pass -- 0 = no dip (flat/drone-like), 1 =
     * dips to true silence -- the loop's own equivalent of how AM
     * Depth used to work, just shaping the loop's repeating envelope
     * instead of an external tremolo oscillator. */
    double loopDepth01;            /* knob 4: 0-1, 0 = no dip, 1 = full silence at the end of each pass */
    double attackMs;              /* knob 5: 0.5-200 ms */
    double releaseMs;             /* knob 6: 5-2000 ms */
    double detuneSpread01;        /* knob 7: scales each layer's per-layer detuneCents, 0 = unison, 1 = full spread */
    /* knob 8: deliberately the LAST knob in the signal path, per
     * explicit request -- controls TILT (see TiltFilter's own comment
     * in noiseboy_plugin.c for the full design). REDESIGNED per
     * explicit request: no longer a lowpass/highpass sweep that can
     * silence the signal entirely -- now a genuine tilt EQ emulating
     * analog tape playback bandwidth (a fixed, always-present gentle
     * rolloff below ~100Hz and a steeper one above ~10kHz, with this
     * knob shifting the balance toward bass or treble emphasis within
     * that window). Applied on the FINAL mix, AFTER db-cell now, not
     * per-voice before it -- see render_block's own comment for why.
     * 0.5 = neutral tilt (the base tape-bandwidth window, no bass/
     * treble emphasis either way); 0 = full bass emphasis (highs
     * rolled off further); 1 = full treble emphasis (bass rolled off
     * further). Master level moved off knob 8 to a menu-only position
     * (see masterLevel01 below) to make room for this. */
    double tiltAmount01;
    /* Now knob 3 (see this struct's own comment on loopDepth01 for the
     * full context) -- moved from menu-only to a dedicated physical
     * knob, per explicit request ("That knob could now be DRIVE"),
     * since LOOP no longer needs knob 3 for a wet/dry blend. Also now
     * controls bitcrush/rate-reduce intensity -- see
     * noiseboy_process_stereo's own comment. */
    double drive01;
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

/* TILT -- redesigned per explicit request, replacing the old lowpass/
 * highpass sweep-to-silence "Output Filt": "Instead of a lowpass/
 * hipass scheme, let's make it more TILT positively or negatively
 * from 50%, closely following what analog tape does. It should roll
 * off below 100hz, and steeply decline starting at 10000hz... much
 * like the TONE knob from DISTROYBOY, emulating analog tape
 * playback... the TILT shifts this either higher or lower." Lives
 * here (not in noiseboy_plugin.c) specifically so it's testable
 * standalone via this project's own C test suite, matching where
 * every other piece of real DSP logic in this project lives --
 * noiseboy_plugin.c just calls into this.
 *
 * Applied on the FINAL stereo mix, AFTER db-cell now (per explicit
 * request to move db-cell before TILT -- see render_block's own
 * comment in noiseboy_plugin.c), not per-voice before it like the old
 * Output Filt was.
 *
 * Design: a FIXED, always-present "tape bandwidth" window -- a gentle
 * highpass around 100Hz (2-pole Korg35HP, ~12dB/octave -- matches
 * "roll off", the gentler word choice for the low end) and a steeper
 * lowpass around 10kHz (4-pole Moog Ladder, ~24dB/octave -- matches
 * "steeply decline", the sharper word choice for the high end). This
 * window is present at EVERY tilt setting, unlike the old design which
 * could sweep all the way to silence -- TILT never fully mutes the
 * signal now, matching real tape playback (which always has SOME
 * bandwidth, just a differently-balanced one).
 *
 * TILT itself (knob 8, 0-1) shifts the BALANCE within that window
 * rather than moving both edges together: at tilt=0.5 (centre), both
 * edges sit at their base position (100Hz/10kHz) -- neutral, no bass
 * or treble emphasis. Below 0.5, the highpass edge stays fixed at
 * 100Hz while the LOWPASS edge sweeps DOWN (from 10kHz toward ~800Hz)
 * -- cutting more treble, emphasizing bass. Above 0.5, the lowpass
 * edge stays fixed at 10kHz while the HIGHPASS edge sweeps UP (from
 * 100Hz toward ~1.5kHz) -- cutting more bass, emphasizing treble. Only
 * one edge ever moves at a time (like the old design's "only one side
 * active" tilt behaviour), giving a genuine tilt-EQ feel using this
 * project's existing filter primitives rather than deriving a new
 * shelving-filter design from scratch.
 *
 * Zero resonance on both filters -- pure tone shaping, no resonant
 * peak, matching this project's established "Output Filt has no
 * resonance" design choice from before. Independent L/R instances for
 * genuine stereo. Smoothed knob value (matches this project's
 * established zipper-noise-avoidance pattern for other knobs) so
 * turning the physical knob doesn't produce audible stepping. */
typedef struct {
    Korg35HP highpassL;
    Korg35HP highpassR;
    MoogLadder lowpassL;
    MoogLadder lowpassR;
    double smoothedTilt01;
} TiltFilter;

void tilt_filter_init(TiltFilter *t, double sampleRate);
void tilt_filter_process(TiltFilter *t, double *l, double *r, double tiltTarget01, double sampleRate);

/* Noise gate, RE-ADDED per direct report: after v0.16.0 removed the
 * dedicated gate (on the theory that TILT's own always-present
 * bandwidth limiting would keep db-cell's forced-always-present Noiz
 * slot quiet enough when idle -- measured at the time as roughly -55
 * to -58dB relative to a played note), it turned out to still be
 * audible in practice. That measurement wasn't wrong, but "quiet"
 * isn't the same as "silent", and a gate is what actually guarantees
 * silence.
 *
 * Positioned AFTER TILT this time (TILT was applied where the OLD
 * gate used to sit, before this fix) -- so db-cell's output still
 * flows through TILT's tone-shaping on its way out, exactly as
 * originally requested ("it must flow through tone shaping on its way
 * out"), with the gate now the final stage after that, guaranteeing
 * true silence rather than TILT trying to do double duty as both an
 * EQ and a gate. Keyed off actual NOISEBOY voice activity
 * (noiseboy_any_voice_active), not signal level -- a level-based gate
 * could still let db-cell's own noise through during a genuinely
 * quiet-but-still-playing moment, or fail to fully silence a loud
 * db-cell moment right as the last voice releases. Fast attack (opens
 * quickly when a note starts) and a slower, smoothed release (avoids
 * an abrupt click when the last voice stops). */
typedef struct {
    double envelope;
} NoiseboyOutputGate;

void noiseboy_output_gate_init(NoiseboyOutputGate *g);
double noiseboy_output_gate_process(NoiseboyOutputGate *g, double x, int voicesActive, double sampleRate);

typedef struct {
    /* Set to NOISEBOY_ENGINE_INIT_MAGIC by noiseboy_engine_init, and
     * checked at the START of that same function, BEFORE the
     * unconditional memset that follows -- this is the only reliable
     * way to tell "this instance was already initialized once (its
     * per-layer LoopSource buffers are valid, already-allocated
     * pointers -- free them before reallocating)" apart from "this is
     * genuinely fresh, uninitialized (stack) memory (don't try to free
     * whatever garbage bytes happen to be sitting in the buffer
     * pointer fields -- that's undefined behaviour, not a real
     * pointer)". A NULL-check on the buffer pointers alone can't
     * distinguish these two cases, since fresh stack memory isn't
     * guaranteed to be zeroed. This matters in practice, not just in
     * theory: this project's own test suite calls
     * noiseboy_engine_init repeatedly on the SAME stack-declared
     * instance, in loops of up to several thousand iterations (sweeping
     * seeds) -- without this guard, every one of those re-init calls
     * would leak its previous LoopSource allocations, which would
     * exhaust memory within a handful of iterations. */
    unsigned int initMagic;
    double sampleRate;
    unsigned int rngState;
    Voice voices[NOISEBOY_MAX_VOICES];
    NoiseboyParams params;

    /* The randomized recipe decided once at instantiation -- see
     * LayerRecipe's comment. */
    LayerRecipe recipe[NOISEBOY_MAX_LAYERS];
    int numRecipeLayers;

    /* Recipe-level timbre character, per direct report ("much less
     * randomness to the Randomize" after last session's resonance-
     * evenness fix). That fix removed real variety from two places:
     * filter type is no longer randomized (always Moog now, was 50/50
     * against Korg35LP, whose own resonance formula measured up to
     * 79x weaker at the same knob value -- too broken and risky to
     * re-calibrate quickly, see the pitch-tracking filter's own
     * comment), and the new frequency compensation curve actively
     * narrows how different notes' resonance peaks can be from each
     * other. This restores SOME of that lost distinctiveness.
     *
     * Applied to the pitch-tracking filter's CUTOFF, not resonance --
     * an earlier version of this fix varied resonance instead, which
     * seemed reasonable but turned out not to work reliably: measured
     * directly, resonance at this stage in the chain typically already
     * sits on the FLAT TOP of this filter's own peak-gain curve (an
     * initial +-20% multiplier there moved measured output by under
     * 1%), and worse, that curve was measured with impulse excitation,
     * while the real signal chain feeds this filter continuous noise
     * -- under continuous excitation the resonance-vs-output
     * relationship behaves differently again, and a careful isolated
     * comparison (same seed/recipe, only the multiplier different)
     * confirmed under 1% actual difference in practice. Cutoff, by
     * contrast, measured a clean, reliable, monotonic ~21% RMS range
     * under the SAME continuous-noise-excitation test -- a far more
     * dependable lever. Bounded to +-15% (narrower than the range
     * actually tested) specifically because cutoff is this filter's
     * PITCH-tracking parameter -- wide enough to read as a genuine
     * brightness difference between "instruments", conservative enough
     * to not noticeably detune the instrument the way a wider swing
     * risked doing. */
    double timbreCharacterMul;

    /* Tape wobble depth, per explicit request -- see TapeWobble's own
     * header comment for the full design. Randomized ONCE globally
     * (recipe-level, same lifecycle as timbreCharacterMul), 1%-5%
     * (0.01-0.05) per explicit spec -- each "instrument" gets its own
     * fixed, consistent wobble amount rather than a different one each
     * note, matching how a single physical tape machine has one
     * consistent mechanical wobble character. */
    double mellotronDepth01;

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
