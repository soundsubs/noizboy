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
 * instantiation (fixed for that load, not re-randomized per note).
 * Each layer is independently either:
 *   - filtered noise: a noise generator (random colour) through a
 *     resonant filter (randomly Moog Ladder or Korg35 LP/HP) tuned to
 *     the played note's frequency, or
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

#define NOISEBOY_MAX_VOICES 8
#define NOISEBOY_MAX_LAYERS 3
#define NOISEBOY_KS_MAX_SAMPLES 2400 /* enough delay-line length for the lowest supported note (~A0, 27.5Hz) at up to 48kHz-ish sample rates, with headroom */

/* ---- Reused verbatim from distroy_dsp.c (see that file's own tests --
 * these are proven, already-tested building blocks, not reimplemented
 * from scratch here) ---- */

typedef struct {
    unsigned int state;
} SimpleNoise;

void noise_init(SimpleNoise *n, unsigned int seed);
double noise_next(SimpleNoise *n); /* returns -1.0..1.0 */

typedef enum { NOISE_WHITE = 0, NOISE_PINK, NOISE_RED } NoiseColour;

typedef struct {
    SimpleNoise white;
    double pink_b0, pink_b1, pink_b2;
    double brown_state;
} NoiseGen;

void noisegen_init(NoiseGen *n, unsigned int seed);
double noisegen_process(NoiseGen *n, NoiseColour colour);

typedef struct {
    double stage[4];
    double delay[4];
    double p, k, resonance, drive;
    double sample_rate;
} MoogLadder;

void moog_ladder_init(MoogLadder *f, double sample_rate);
void moog_ladder_set(MoogLadder *f, double cutoff_hz, double resonance01, double drive01);
double moog_ladder_process(MoogLadder *f, double x);

typedef struct {
    double stage[2];
    double delay[2];
    double p, k, resonance, drive;
    double sample_rate;
} Korg35LP;

void korg35lp_init(Korg35LP *f, double sample_rate);
void korg35lp_set(Korg35LP *f, double cutoff_hz, double resonance01, double drive01);
double korg35lp_process(Korg35LP *f, double x);

typedef struct {
    Korg35LP core;
} Korg35HP;

void korg35hp_init(Korg35HP *f, double sample_rate);
void korg35hp_set(Korg35HP *f, double cutoff_hz, double resonance01, double drive01);
double korg35hp_process(Korg35HP *f, double x);

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

typedef enum { LAYER_FILTERED_NOISE = 0, LAYER_KARPLUS_STRONG } LayerType;
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
     * still knob-controllable (see NoiseboyVoiceParams) so it isn't a
     * hard instant on/off -- just smoothed enough to avoid clicks. */
    double envLevel;
    int gateOpen;

    /* Per-voice amplitude modulation phase -- each voice runs its own
     * AM phase (not perfectly in sync with other voices), which reads
     * more like an ensemble of slightly-differently-wobbling noise
     * sources than one synchronized tremolo. */
    double amPhase;
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
 * knobs 1-8. */
typedef struct {
    double filterCutoffOffset01;  /* knob 1: brightens/darkens the pitch-tracked filter cutoff, -1..1 mapped from 0..1 */
    double filterResonance01;     /* knob 2 */
    double amRateHz;              /* knob 3: 0.1-20 Hz */
    double amDepth01;             /* knob 4: 0 = no AM, 1 = full tremolo */
    double attackMs;              /* knob 5: 0.5-200 ms */
    double releaseMs;             /* knob 6: 5-2000 ms */
    double detuneSpread01;        /* knob 7: scales each layer's per-layer detuneCents, 0 = unison, 1 = full spread */
    double masterLevel01;         /* knob 8 */
} NoiseboyParams;

typedef struct {
    double sampleRate;
    unsigned int rngState;
    Voice voices[NOISEBOY_MAX_VOICES];
    NoiseboyParams params;

    /* The randomized recipe decided once at instantiation -- see
     * LayerRecipe's comment. */
    LayerRecipe recipe[NOISEBOY_MAX_LAYERS];
    int numRecipeLayers;
} NoiseboyEngine;

void noiseboy_engine_init(NoiseboyEngine *e, double sampleRate, unsigned int seed);
void noiseboy_note_on(NoiseboyEngine *e, int midiNote, double velocity01);
void noiseboy_note_off(NoiseboyEngine *e, int midiNote);
void noiseboy_all_notes_off(NoiseboyEngine *e);
/* Processes and returns ONE mono sample -- there's no meaningful
 * stereo width to a noise+filter/Karplus-Strong source without adding
 * complexity that wasn't asked for, so the caller duplicates this to
 * both output channels. */
double noiseboy_process(NoiseboyEngine *e);

#endif /* NOISEBOY_DSP_H */
