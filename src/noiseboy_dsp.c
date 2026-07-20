#include "noiseboy_dsp.h"
#include <math.h>
#include <string.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double clampd(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

/* ---------------------------------------------------------------------
 * Reused verbatim from distroy_dsp.c -- see that project's own test
 * suite for verification of these specific algorithms; not re-derived
 * or re-tested here, just carried over for sonic consistency with
 * DISTROY (the stated use case chains NOISEBOY into DISTROY).
 * ------------------------------------------------------------------- */

void noise_init(SimpleNoise *n, unsigned int seed) {
    n->state = seed != 0 ? seed : 1;
}

double noise_next(SimpleNoise *n) {
    unsigned int s = n->state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    n->state = s;
    return ((double)s / (double)UINT32_MAX) * 2.0 - 1.0;
}

void noisegen_init(NoiseGen *n, unsigned int seed) {
    noise_init(&n->white, seed);
    n->pink_b0 = 0.0;
    n->pink_b1 = 0.0;
    n->pink_b2 = 0.0;
    n->brown_state = 0.0;
}

double noisegen_process(NoiseGen *n, NoiseColour colour) {
    double white = noise_next(&n->white);
    switch (colour) {
        case NOISE_PINK: {
            n->pink_b0 = 0.99765 * n->pink_b0 + white * 0.0990460;
            n->pink_b1 = 0.96300 * n->pink_b1 + white * 0.2965164;
            n->pink_b2 = 0.57000 * n->pink_b2 + white * 1.0526913;
            double pink = n->pink_b0 + n->pink_b1 + n->pink_b2 + white * 0.1848;
            return pink * 0.11;
        }
        case NOISE_RED: {
            n->brown_state = n->brown_state * 0.98 + white * 0.02;
            return n->brown_state * 3.5;
        }
        case NOISE_WHITE:
        default:
            return white;
    }
}

void moog_ladder_init(MoogLadder *f, double sample_rate) {
    memset(f, 0, sizeof(*f));
    f->sample_rate = sample_rate;
    f->drive = 1.0;
    moog_ladder_set(f, 1000.0, 0.0, 0.0);
}

void moog_ladder_set(MoogLadder *f, double cutoff_hz, double resonance01, double drive01) {
    double fc = clampd(cutoff_hz / (f->sample_rate * 0.5), 0.0001, 0.99);
    f->p = fc * (1.8 - 0.8 * fc);
    f->k = 2.0 * sin(fc * M_PI * 0.5) - 1.0;
    double t1 = (1.0 - f->p) * 1.386249;
    double t2 = 12.0 + t1 * t1;
    f->resonance = resonance01 * 3.5 * (t2 + 6.0 * t1) / (t2 - 6.0 * t1);
    f->drive = 1.0 + drive01 * 11.0;
}

double moog_ladder_process(MoogLadder *f, double x) {
    x *= f->drive;
    x = tanh(x);

    double input = x - f->resonance * f->stage[3];
    f->stage[0] = input * f->p + f->delay[0] * f->p - f->k * f->stage[0];
    f->stage[1] = f->stage[0] * f->p + f->delay[1] * f->p - f->k * f->stage[1];
    f->stage[2] = f->stage[1] * f->p + f->delay[2] * f->p - f->k * f->stage[2];
    f->stage[3] = f->stage[2] * f->p + f->delay[3] * f->p - f->k * f->stage[3];
    f->stage[3] -= (f->stage[3] * f->stage[3] * f->stage[3]) / 6.0;

    for (int i = 0; i < 4; i++) {
        f->stage[i] = clampd(f->stage[i], -8.0, 8.0);
    }

    f->delay[0] = input;
    f->delay[1] = f->stage[0];
    f->delay[2] = f->stage[1];
    f->delay[3] = f->stage[2];

    return f->stage[3];
}

void korg35lp_init(Korg35LP *f, double sample_rate) {
    memset(f, 0, sizeof(*f));
    f->sample_rate = sample_rate;
    f->drive = 1.0;
    korg35lp_set(f, 1000.0, 0.0, 0.0);
}

void korg35lp_set(Korg35LP *f, double cutoff_hz, double resonance01, double drive01) {
    double fc = clampd(cutoff_hz / (f->sample_rate * 0.5), 0.0001, 0.99);
    f->p = fc * (1.8 - 0.8 * fc);
    f->k = 2.0 * sin(fc * M_PI * 0.5) - 1.0;
    double t1 = (1.0 - f->p) * 1.386249;
    double t2 = 12.0 + t1 * t1;
    f->resonance = resonance01 * 2.6 * (t2 + 6.0 * t1) / (t2 - 6.0 * t1);
    f->drive = 1.0 + drive01 * 9.0;
}

double korg35lp_process(Korg35LP *f, double x) {
    x *= f->drive;
    x = tanh(x);

    double input = x - f->resonance * f->stage[1];
    f->stage[0] = input * f->p + f->delay[0] * f->p - f->k * f->stage[0];
    f->stage[1] = f->stage[0] * f->p + f->delay[1] * f->p - f->k * f->stage[1];
    f->stage[1] -= (f->stage[1] * f->stage[1] * f->stage[1]) / 6.0;

    f->stage[0] = clampd(f->stage[0], -8.0, 8.0);
    f->stage[1] = clampd(f->stage[1], -8.0, 8.0);

    f->delay[0] = input;
    f->delay[1] = f->stage[0];

    return f->stage[1];
}

void korg35hp_init(Korg35HP *f, double sample_rate) {
    korg35lp_init(&f->core, sample_rate);
}

void korg35hp_set(Korg35HP *f, double cutoff_hz, double resonance01, double drive01) {
    korg35lp_set(&f->core, cutoff_hz, resonance01, drive01);
}

double korg35hp_process(Korg35HP *f, double x) {
    double lp = korg35lp_process(&f->core, x);
    return x - lp;
}

/* ---------------------------------------------------------------------
 * New for NOISEBOY: Karplus-Strong plucked-string synthesis.
 * ------------------------------------------------------------------- */

void karplus_init(KarplusString *k) {
    memset(k, 0, sizeof(*k));
    k->damping = 0.99;
}

void karplus_pluck(KarplusString *k, double freq_hz, double sample_rate, NoiseGen *noiseGen, NoiseColour colour, double dampingAmount) {
    int length = (int)(sample_rate / (freq_hz > 20.0 ? freq_hz : 20.0) + 0.5);
    if (length < 2) length = 2;
    if (length > NOISEBOY_KS_MAX_SAMPLES - 1) length = NOISEBOY_KS_MAX_SAMPLES - 1;
    k->length = length;
    k->writePos = 0;
    k->lastOut = 0.0;
    /* dampingAmount 0-1 -> damping coefficient 0.90-0.999: lower =
     * darker/faster decay, higher = brighter/longer sustain -- the
     * classic Karplus-Strong "string material" control. */
    k->damping = 0.90 + clampd(dampingAmount, 0.0, 1.0) * 0.099;

    for (int i = 0; i < length; i++) {
        k->buffer[i] = noisegen_process(noiseGen, colour);
    }
}

double karplus_process(KarplusString *k) {
    int len = k->length;
    if (len < 2) return 0.0;
    int readPos = k->writePos;
    int nextPos = (readPos + 1 < len) ? (readPos + 1) : 0;
    double cur = k->buffer[readPos];
    double next = k->buffer[nextPos];
    /* Classic Karplus-Strong step: average two adjacent samples (a
     * simple one-pole lowpass, softening the string's harmonics each
     * pass) and apply the decay coefficient, writing the result back
     * into the position just read so it feeds forward next cycle. */
    double filtered = (cur + next) * 0.5;
    k->buffer[readPos] = filtered * k->damping;
    k->writePos = nextPos;
    return cur;
}

/* ---------------------------------------------------------------------
 * Engine: voice management, randomized recipe, MIDI, per-sample mix.
 * ------------------------------------------------------------------- */

static unsigned int xorshift_next(unsigned int *state) {
    unsigned int s = *state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *state = s;
    return s;
}

static double rand01(unsigned int *state) {
    return (double)xorshift_next(state) / (double)UINT32_MAX;
}

static double midi_note_to_freq(int midiNote) {
    return 440.0 * pow(2.0, (midiNote - 69) / 12.0);
}

void noiseboy_engine_init(NoiseboyEngine *e, double sampleRate, unsigned int seed) {
    memset(e, 0, sizeof(*e));
    e->sampleRate = sampleRate;
    e->rngState = seed != 0 ? seed : 1;

    for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
        e->voices[v].active = 0;
    }

    /* Reasonable defaults -- overwritten by set_param once the host
     * reads knob positions, but these keep the instrument playable
     * (and not silent/broken) even before any knob has been touched. */
    e->params.filterCutoffOffset01 = 0.5;  /* neutral: filter tracks exactly at the played pitch */
    e->params.filterResonance01 = 0.3;
    e->params.amRateHz = 4.0;
    e->params.amDepth01 = 0.0;             /* off by default -- AM is a deliberate extra character, not a default-on wobble */
    e->params.attackMs = 4.0;
    e->params.releaseMs = 80.0;
    e->params.detuneSpread01 = 0.5;
    e->params.masterLevel01 = 0.8;

    /* The randomized recipe -- decided ONCE here, per explicit spec
     * ("every time you instantiate, its a randomized noise block").
     * 1-3 layers. */
    e->numRecipeLayers = 1 + (int)(rand01(&e->rngState) * 3.0);
    if (e->numRecipeLayers > NOISEBOY_MAX_LAYERS) e->numRecipeLayers = NOISEBOY_MAX_LAYERS;

    for (int i = 0; i < e->numRecipeLayers; i++) {
        LayerRecipe *r = &e->recipe[i];
        /* Per explicit clarification: filtered-noise layers always
         * track pitch via their filter; Karplus-Strong is the second,
         * separately-pitched method, chosen randomly per layer. */
        r->type = (rand01(&e->rngState) < 0.5) ? LAYER_FILTERED_NOISE : LAYER_KARPLUS_STRONG;

        double colourRoll = rand01(&e->rngState);
        r->colour = (colourRoll < 0.34) ? NOISE_WHITE : (colourRoll < 0.67) ? NOISE_PINK : NOISE_RED;

        double filterRoll = rand01(&e->rngState);
        r->filterKind = (filterRoll < 0.34) ? FILTER_MOOG : (filterRoll < 0.67) ? FILTER_KORG_LP : FILTER_KORG_HP;

        /* Detune spread across layers -- first layer stays at 0 cents
         * (an anchor pitch), the rest spread out symmetrically so
         * stacking layers doesn't just shift the whole chord sharp or
         * flat. +-15 cents max range, modest enough to read as
         * "richness" rather than an obvious chorus effect. */
        r->detuneCents = (i == 0) ? 0.0 : (rand01(&e->rngState) * 2.0 - 1.0) * 15.0;

        r->resonanceBias01 = rand01(&e->rngState);
        r->dampingAmount01 = rand01(&e->rngState);
    }
}

/* Initializes a voice's actual DSP layer instances from the engine's
 * shared recipe -- called fresh on every note-on so each new note
 * starts from clean state (no leftover filter ringing or Karplus
 * buffer content from whatever a stolen voice was doing before). */
static void voice_start(NoiseboyEngine *e, Voice *v, int midiNote, double velocity01) {
    v->active = 1;
    v->midiNote = midiNote;
    v->velocity01 = velocity01;
    v->freqHz = midi_note_to_freq(midiNote);
    v->envLevel = 0.0;
    v->gateOpen = 1;
    v->amPhase = rand01(&e->rngState); /* randomized starting phase per voice, so simultaneous notes don't AM in lockstep */

    v->numLayers = e->numRecipeLayers;
    for (int i = 0; i < v->numLayers; i++) {
        Layer *layer = &v->layers[i];
        const LayerRecipe *r = &e->recipe[i];
        layer->type = r->type;
        layer->colour = r->colour;
        layer->filterKind = r->filterKind;
        layer->detuneCents = r->detuneCents;
        layer->resonanceBias01 = r->resonanceBias01;

        double detuneMul = pow(2.0, (layer->detuneCents * e->params.detuneSpread01) / 1200.0);
        double layerFreq = v->freqHz * detuneMul;

        if (layer->type == LAYER_FILTERED_NOISE) {
            noisegen_init(&layer->noiseGen, xorshift_next(&e->rngState));
            moog_ladder_init(&layer->moog, e->sampleRate);
            korg35lp_init(&layer->korgLp, e->sampleRate);
            korg35hp_init(&layer->korgHp, e->sampleRate);
        } else {
            karplus_init(&layer->karplus);
            NoiseGen seedGen;
            noisegen_init(&seedGen, xorshift_next(&e->rngState));
            double damping = clampd(r->dampingAmount01 * 0.5 + e->params.filterResonance01 * 0.5, 0.0, 1.0);
            karplus_pluck(&layer->karplus, layerFreq, e->sampleRate, &seedGen, r->colour, damping);
        }
    }
}

void noiseboy_note_on(NoiseboyEngine *e, int midiNote, double velocity01) {
    /* Prefer a free voice; if none, steal the quietest currently
     * releasing voice, or failing that just voice 0 -- simple and
     * predictable rather than a full LRU/oldest-note-tracking scheme,
     * which wasn't asked for. */
    int chosen = -1;
    for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
        if (!e->voices[v].active) { chosen = v; break; }
    }
    if (chosen < 0) {
        double lowestLevel = 2.0;
        for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
            if (!e->voices[v].gateOpen && e->voices[v].envLevel < lowestLevel) {
                lowestLevel = e->voices[v].envLevel;
                chosen = v;
            }
        }
        if (chosen < 0) chosen = 0;
    }
    voice_start(e, &e->voices[chosen], midiNote, velocity01);
}

void noiseboy_note_off(NoiseboyEngine *e, int midiNote) {
    for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
        if (e->voices[v].active && e->voices[v].midiNote == midiNote && e->voices[v].gateOpen) {
            e->voices[v].gateOpen = 0;
        }
    }
}

void noiseboy_all_notes_off(NoiseboyEngine *e) {
    for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
        e->voices[v].gateOpen = 0;
    }
}

static double process_layer(NoiseboyEngine *e, Voice *v, Layer *layer) {
    if (layer->type == LAYER_KARPLUS_STRONG) {
        return karplus_process(&layer->karplus);
    }

    double raw = noisegen_process(&layer->noiseGen, layer->colour);

    double detuneMul = pow(2.0, (layer->detuneCents * e->params.detuneSpread01) / 1200.0);
    /* filterCutoffOffset01: 0.5 = neutral (filter sits exactly at the
     * played pitch, i.e. always tracks it); away from 0.5 brightens or
     * darkens the filter RELATIVE TO the tracked pitch, but never
     * stops tracking it -- the offset is a multiplier applied to the
     * pitch-derived base cutoff, not a replacement for it. */
    double cutoffMul = pow(2.0, (e->params.filterCutoffOffset01 - 0.5) * 4.0);
    double cutoffHz = clampd(v->freqHz * detuneMul * cutoffMul, 20.0, e->sampleRate * 0.45);
    double resonance = clampd(e->params.filterResonance01 + (layer->resonanceBias01 - 0.5) * 0.2, 0.0, 0.95);

    switch (layer->filterKind) {
        case FILTER_MOOG:
            moog_ladder_set(&layer->moog, cutoffHz, resonance, 0.2);
            return moog_ladder_process(&layer->moog, raw);
        case FILTER_KORG_LP:
            korg35lp_set(&layer->korgLp, cutoffHz, resonance, 0.2);
            return korg35lp_process(&layer->korgLp, raw);
        case FILTER_KORG_HP:
        default:
            korg35hp_set(&layer->korgHp, cutoffHz, resonance, 0.2);
            return korg35hp_process(&layer->korgHp, raw);
    }
}

double noiseboy_process(NoiseboyEngine *e) {
    double mix = 0.0;
    const double dt = 1.0 / e->sampleRate;

    /* Envelope smoothing coefficients recomputed from the current knob
     * values each sample -- cheap (a couple of exp() calls per ACTIVE
     * voice, not per layer), and lets attack/release knob changes take
     * effect immediately on in-flight notes rather than only on the
     * next note-on. */
    for (int vi = 0; vi < NOISEBOY_MAX_VOICES; vi++) {
        Voice *v = &e->voices[vi];
        if (!v->active) continue;

        double target = v->gateOpen ? v->velocity01 : 0.0;
        double timeMs = v->gateOpen ? e->params.attackMs : e->params.releaseMs;
        timeMs = clampd(timeMs, 0.5, 4000.0);
        double coeff = exp(-1.0 / (0.001 * timeMs * e->sampleRate));
        v->envLevel = target + (v->envLevel - target) * coeff;

        if (!v->gateOpen && v->envLevel < 0.0005) {
            v->active = 0;
            continue;
        }

        double voiceSum = 0.0;
        for (int li = 0; li < v->numLayers; li++) {
            voiceSum += process_layer(e, v, &v->layers[li]);
        }
        if (v->numLayers > 0) voiceSum /= (double)v->numLayers;

        /* Per-voice amplitude modulation -- a simple sine tremolo,
         * depth/rate from knobs 3/4. amDepth01=0 leaves the voice
         * completely untouched (multiplying by 1.0 always), matching
         * "controlled by the knobs" rather than being a fixed always-on
         * wobble. */
        v->amPhase += e->params.amRateHz * dt;
        if (v->amPhase > 1.0) v->amPhase -= floor(v->amPhase);
        double amGain = 1.0 - e->params.amDepth01 * 0.5 * (1.0 - cos(2.0 * M_PI * v->amPhase));

        mix += voiceSum * v->envLevel * amGain;
    }

    return mix * e->params.masterLevel01;
}
