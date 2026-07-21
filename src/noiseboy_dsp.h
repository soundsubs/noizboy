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
 * not from any separate resonant filter. */
typedef struct {
    double buffer[NOISEBOY_KS_MAX_SAMPLES];
    int length;       /* current delay length in samples, set per note */
    int writePos;
    double damping;   /* 0-1, one-pole feedback damping coefficient */
    double lastOut;   /* one-pole filter state */
} KarplusString;

void karplus_init(KarplusString *k);
/* Reseeds the delay line with noise and sets its length for freq_hz at
 * the given sample rate -- call once on note-on. noiseGen supplies the
 * excitation burst (reusing the same three-colour noise generator so
 * the pluck's initial timbre can vary the same way filtered-noise
 * layers do). */
void karplus_pluck(KarplusString *k, double freq_hz, double sample_rate, NoiseGen *noiseGen, NoiseColour colour, double dampingAmount);
double karplus_process(KarplusString *k);

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

    /* Filtered-noise layer state. */
    NoiseGen noiseGen;
    NoiseColour colour;
    FilterKind filterKind;
    MoogLadder moog;
    Korg35LP korgLp;
    Korg35HP korgHp;
    PitchedHold pitchedHold;

    /* Karplus-Strong layer state. */
    KarplusString karplus;

    /* Per-layer randomized character, decided once at instantiation:
     * a small detune (in cents) for chorused richness when multiple
     * layers stack, and (filtered-noise only) a damping-equivalent
     * resonance bias so not every layer sounds identical even at the
     * same knob settings. */
    double detuneCents;
    double resonanceBias01;
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

    /* Per-voice bitcrusher and pitch-following sample-rate reducer,
     * per explicit request -- randomized fresh on every note-on (not
     * part of the fixed recipe), so different notes played on the
     * same recipe get some additional per-note variety. bitDepth is
     * literal (1-15 bits, quantization applied directly). The rate
     * reducer reuses the same PitchedHold mechanism the per-layer
     * pitch stage uses (a sample-and-hold whose rate tracks the
     * played note), applied here at the VOICE level (after layers are
     * mixed) rather than per-layer -- "the ear will detect this as
     * pitch" is the same principle as PitchedHold's own rationale,
     * just as a second, voice-wide instance of it. */
    int bitDepth;
    PitchedHold rateReducer;
    double rateReducerMultiplier; /* how far above the 100Hz floor this voice's reduction rate can reach, randomized per note */
} Voice;

/* Layer RECIPE, decided once at module instantiation (not per-note) --
 * per explicit spec ("every time you instantiate, its a randomized
 * noise block"). Every voice is initialized from this same shared
 * recipe on note-on (same layer count/types/colours), giving each note
 * fresh independent DSP state (filter/noise-generator/Karplus-string
 * instances) but a consistent "instrument identity" across the whole
 * session, rather than re-randomizing per note. */
typedef struct {
    LayerType type;
    NoiseColour colour;      /* filtered-noise layers only */
    FilterKind filterKind;   /* filtered-noise layers only */
    double detuneCents;
    double resonanceBias01;
    double dampingAmount01;  /* Karplus-Strong layers only */
} LayerRecipe;

/* Knob-controlled parameters, shared across all voices (not per-voice
 * state) -- set via set_param in the Schwung plugin wrapper, mapped to
 * knobs 1-8 (plus two additional chain_params beyond the 8 physical
 * knobs -- drive and randomize-trigger -- still reachable via the
 * module's parameter menu). */
typedef struct {
    double filterCutoffOffset01;  /* knob 1: brightens/darkens the pitch-tracked filter cutoff, -1..1 mapped from 0..1 */
    double filterResonance01;     /* knob 2 */
    double amRateHz;              /* knob 3: 0.1-20 Hz */
    double amDepth01;             /* knob 4: 0 = no AM, 1 = full tremolo */
    double attackMs;              /* knob 5: 0.5-200 ms */
    double releaseMs;             /* knob 6: 5-2000 ms */
    double detuneSpread01;        /* knob 7: scales each layer's per-layer detuneCents, 0 = unison, 1 = full spread */
    double masterLevel01;         /* knob 8 */
    double drive01;               /* chain_param 9: single shared drive/saturation stage on the final mix -- see noiseboy_process */
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
     * DBCELL processing, in the plugin wrapper). */
    TapeSaturation tapeSat;
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
/* Processes and returns ONE mono sample -- there's no meaningful
 * stereo width to a noise+filter/Karplus-Strong source without adding
 * complexity that wasn't asked for, so the caller duplicates this to
 * both output channels. */
double noiseboy_process(NoiseboyEngine *e);

#endif /* NOISEBOY_DSP_H */
