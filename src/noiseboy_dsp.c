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


/* ---------------------------------------------------------------------
 * New for NOISEBOY: Karplus-Strong plucked-string synthesis.
 * ------------------------------------------------------------------- */

void karplus_init(KarplusString *k) {
    memset(k, 0, sizeof(*k));
    k->damping = 0.99;
}

void karplus_pluck(KarplusString *k, double freq_hz, double sample_rate, unsigned int *rngState, const NoiseColour *sourceColours, int numSources, double dampingAmount) {
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

    /* Excitation burst summed from numSources independently-seeded
     * generators (one per recipe layer's colour, per direct feedback
     * -- see this struct's own comment for why), normalized by count
     * so more sources doesn't mean a louder pluck, just a richer one. */
    if (numSources < 1) numSources = 1;
    if (numSources > NOISEBOY_MAX_LAYERS) numSources = NOISEBOY_MAX_LAYERS;
    NoiseGen sources[NOISEBOY_MAX_LAYERS];
    for (int s = 0; s < numSources; s++) {
        noisegen_init(&sources[s], xorshift_next(rngState));
    }

    for (int i = 0; i < length; i++) {
        double sum = 0.0;
        for (int s = 0; s < numSources; s++) {
            sum += noisegen_process(&sources[s], sourceColours[s]);
        }
        k->buffer[i] = sum / (double)numSources;
    }
}

double karplus_process(KarplusString *k, double sustainFeedSample, double sustainAmount) {
    int len = k->length;
    if (len < 2) return 0.0;
    int readPos = k->writePos;
    int nextPos = (readPos + 1 < len) ? (readPos + 1) : 0;
    double cur = k->buffer[readPos];
    double next = k->buffer[nextPos];
    /* Classic Karplus-Strong step: average two adjacent samples (a
     * simple one-pole lowpass, softening the string's harmonics each
     * pass) and apply the decay coefficient, writing the result back
     * into the position just read so it feeds forward next cycle.
     * sustainFeedSample*sustainAmount adds a small continuous
     * injection while the note is held (sustainAmount=0 after
     * release), keeping the string ringing rather than only decaying
     * from the initial pluck -- see this struct's own comment. */
    double filtered = (cur + next) * 0.5;
    k->buffer[readPos] = filtered * k->damping + sustainFeedSample * sustainAmount;
    k->writePos = nextPos;
    return cur;
}

/* ---------------------------------------------------------------------
 * New for NOISEBOY: sample-and-hold pitched noise stage.
 * ------------------------------------------------------------------- */

void pitchedhold_init(PitchedHold *h) {
    h->heldValue = 0.0;
    h->phase = 0.0;
}

double pitchedhold_process(PitchedHold *h, double newSample, double freqHz, double sampleRate, double holdMultiplier) {
    /* Advances a phase accumulator at freqHz*holdMultiplier cycles per
     * second; each time it wraps, latches newSample as the held value
     * -- a sample-and-hold whose rate itself tracks the played pitch,
     * giving the noise a buzzy/quantized character with a much
     * stronger, more obvious relationship to the note than filter
     * resonance alone provides. holdMultiplier > 1 holds at a multiple
     * of the fundamental (still musically related, just a higher
     * "grain rate") rather than always exactly at it, for some
     * per-layer variety. */
    double rate = freqHz * (holdMultiplier > 0.01 ? holdMultiplier : 1.0);
    h->phase += rate / sampleRate;
    if (h->phase >= 1.0) {
        h->phase -= floor(h->phase);
        h->heldValue = newSample;
    }
    return h->heldValue;
}

double bitcrush_process(double x, int bits) {
    /* Standard bit-depth quantizer, covering the full [-1,1] signal
     * range with 2^bits discrete levels. bits=1 crushes to essentially
     * {-1,0,1}; bits=15 is close to imperceptible (step ~0.00006). */
    if (bits < 1) bits = 1;
    if (bits > 16) bits = 16;
    double levels = pow(2.0, (double)bits);
    double step = 2.0 / levels;
    double quantized = floor(x / step + 0.5) * step;
    /* Never fully silence a genuinely nonzero input -- a real bug this
     * caught: at low bit depths (step is huge relative to typical
     * signal amplitudes -- 0.5 for bits=2), a small-but-real signal
     * (a Karplus-Strong voice sampled through the rate-reducer) can
     * round to exactly 0 on EVERY sample for the note's entire
     * duration, silencing the voice outright rather than just sounding
     * crunchy. Nudge to the nearest nonzero step in the input's own
     * direction instead -- keeps the extreme, harsh character low bit
     * depths are supposed to have, while guaranteeing some signal
     * always gets through. */
    if (quantized == 0.0 && fabs(x) > 1e-9) {
        quantized = (x > 0.0) ? step : -step;
    }
    return quantized;
}

double wavefold_process(double x, double amount) {
    /* Reflective wavefolder: pre-gain scales with amount, then any
     * excursion past +-1 bounces back into range (a triangle-wave-
     * style fold) rather than clipping -- the classic wavefolding
     * technique for rich, FM-like harmonics. Iteration count capped at
     * 8 as a safety net against runaway loops on extreme inputs; in
     * practice folds rarely need more than 1-2 reflections at the
     * amounts this is actually driven with. */
    if (amount <= 0.0001) return x;
    double driven = x * (1.0 + amount * 4.0);
    for (int i = 0; i < 8; i++) {
        if (driven > 1.0) driven = 2.0 - driven;
        else if (driven < -1.0) driven = -2.0 - driven;
        else break;
    }
    return driven;
}

void vibrato_init(VibratoDelay *v) {
    memset(v->buffer, 0, sizeof(v->buffer));
    v->writePos = 0;
}

double vibrato_process(VibratoDelay *v, double x, double phase01, double depthSamples) {
    v->buffer[v->writePos] = x;

    /* Fixed 32-sample centre delay (well inside the 512-sample buffer
     * even at the largest depthSamples this is ever driven with),
     * modulated +-depthSamples by a sine at the same phase driving
     * AM/wavefold. Linear interpolation between the two nearest
     * integer samples for a smooth fractional read position -- this
     * is what actually produces the pitch-bend sensation, unlike an
     * integer-only read which would just add a stepped/gritty comb
     * artifact instead of a smooth vibrato. */
    double lfo = sin(2.0 * M_PI * phase01);
    double delaySamples = 32.0 + lfo * depthSamples;

    double readPos = (double)v->writePos - delaySamples;
    while (readPos < 0.0) readPos += (double)NOISEBOY_VIBRATO_BUFFER_SIZE;

    int readPosInt = (int)readPos;
    double frac = readPos - (double)readPosInt;
    int readPosNext = (readPosInt + 1) % NOISEBOY_VIBRATO_BUFFER_SIZE;

    double sample = v->buffer[readPosInt] * (1.0 - frac) + v->buffer[readPosNext] * frac;

    v->writePos = (v->writePos + 1) % NOISEBOY_VIBRATO_BUFFER_SIZE;
    return sample;
}

void tapesat_init(TapeSaturation *t) {
    t->envelope = 0.0;
}

double tapesat_process(TapeSaturation *t, double x, double sampleRate) {
    /* Simple envelope-follower compressor (5ms attack, 80ms release,
     * threshold 0.3, ratio 3:1) feeding a fixed 2x drive into tanh
     * saturation -- "compresses, drives, and saturates", per explicit
     * request, kept deliberately simple/cheap rather than modeling
     * tape wow/flutter or head-bump EQ, matching the CPU-light mandate
     * this whole project follows. */
    double absX = fabs(x);
    const double attackCoeff = exp(-1.0 / (0.001 * 5.0 * sampleRate));
    const double releaseCoeff = exp(-1.0 / (0.001 * 80.0 * sampleRate));
    const double coeff = absX > t->envelope ? attackCoeff : releaseCoeff;
    t->envelope = absX + (t->envelope - absX) * coeff;

    const double threshold = 0.3;
    const double ratio = 3.0;
    double gainReduction = 1.0;
    if (t->envelope > threshold) {
        const double excess = t->envelope - threshold;
        const double compressedExcess = excess / ratio;
        gainReduction = (threshold + compressedExcess) / t->envelope;
    }

    const double compressed = x * gainReduction;
    const double driven = compressed * 2.0;
    return tanh(driven);
}

/* ---------------------------------------------------------------------
 * Engine: voice management, randomized recipe, MIDI, per-sample mix.
 * ------------------------------------------------------------------- */

static double midi_note_to_freq(int midiNote) {
    return 440.0 * pow(2.0, (midiNote - 69) / 12.0);
}

void noiseboy_engine_init(NoiseboyEngine *e, double sampleRate, unsigned int seed) {
    memset(e, 0, sizeof(*e));
    e->sampleRate = sampleRate;
    e->rngState = seed != 0 ? seed : 1;
    e->lastRandomizeTriggerRaw = 0;

    for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
        e->voices[v].active = 0;
    }

    /* Reasonable defaults -- overwritten by set_param once the host
     * reads knob positions, but these keep the instrument playable
     * (and not silent/broken) even before any knob has been touched.
     * filterResonance01 default raised significantly (was 0.3) --
     * per direct feedback/diagnosis that the noise "isn't quite
     * pitched": a resonant filter needs to sit much closer to
     * self-oscillation than 0.3 to produce an audible peak at its
     * cutoff from noise input, which is the whole mechanism "pitched
     * noise via resonant filter" depends on. */
    e->params.filterCutoffOffset01 = 0.5;  /* neutral: filter tracks exactly at the played pitch */
    e->params.filterResonance01 = 0.82;
    e->params.amRateHz = 4.0;
    e->params.amDepth01 = 0.0;             /* off by default -- AM is a deliberate extra character, not a default-on wobble */
    e->params.attackMs = 4.0;
    e->params.releaseMs = 80.0;
    e->params.detuneSpread01 = 0.5;
    e->params.outputFilterFreq01 = 0.5;    /* neutral -- no change to the velocity-driven base range */
    e->params.masterLevel01 = 0.8;
    e->params.drive01 = 0.25;              /* modest default drive, per explicit request for a built-in stage to add volume/colour -- not maxed out by default */

    tapesat_init(&e->tapeSat);

    noiseboy_randomize_recipe(e);
}

void noiseboy_randomize_recipe(NoiseboyEngine *e) {
    /* 1-3 layers, per explicit spec ("every time you instantiate, its
     * a randomized noise block"). Also callable on demand (not just at
     * instantiation) per explicit request for a way to get a new
     * randomized set without reloading the whole module -- uses the
     * engine's own ongoing RNG state, so repeated calls keep producing
     * fresh, non-repeating recipes rather than resetting to the same
     * sequence. */
    e->numRecipeLayers = 1 + (int)(rand01(&e->rngState) * 3.0);
    if (e->numRecipeLayers > NOISEBOY_MAX_LAYERS) e->numRecipeLayers = NOISEBOY_MAX_LAYERS;

    for (int i = 0; i < e->numRecipeLayers; i++) {
        LayerRecipe *r = &e->recipe[i];
        /* Per explicit clarification: filtered-noise layers always
         * track pitch via their filter; Karplus-Strong is the second,
         * separately-pitched method, chosen randomly per layer. */
        /* 25% per-layer chance, not 50% -- per direct feedback that
         * Karplus-Strong felt like it was on "most patches". Worked
         * out why: with 1-3 layers per recipe, a 50/50 per-layer coin
         * flip means the probability of AT LEAST ONE Karplus layer
         * appearing compounds across layers (50% at 1 layer, 75% at 2,
         * 87.5% at 3 -- averaging ~71% overall, which matches the
         * complaint). 25% per-layer brings that average down to ~42%,
         * a genuinely "sometimes" rate rather than "usually". */
        r->type = (rand01(&e->rngState) < 0.25) ? LAYER_KARPLUS_STRONG : LAYER_FILTERED_NOISE;

        double colourRoll = rand01(&e->rngState);
        r->colour = (colourRoll < 0.34) ? NOISE_WHITE : (colourRoll < 0.67) ? NOISE_PINK : NOISE_RED;

        /* FILTER_KORG_HP deliberately excluded from selection -- see
         * FilterKind's header comment for why a highpass tuned to the
         * played pitch actively works against sounding pitched.
         * Moog/Korg35LP only, 50/50. */
        r->filterKind = (rand01(&e->rngState) < 0.5) ? FILTER_MOOG : FILTER_KORG_LP;

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

    /* Per-voice bitcrusher and pitch-following sample-rate reducer,
     * randomized fresh each note-on per explicit request.
     * rateReducerMultiplier is randomized per note so different notes
     * on the same recipe get some variety in how aggressively reduced
     * they sound, not just where the reduction rate sits -- the
     * reduction rate itself (freqHz * multiplier, floored at 100Hz,
     * ceiled at Nyquist in noiseboy_process) is what actually "follows
     * played note numbers".
     *
     * Bit-crush range backed off AGAIN per direct feedback ("still too
     * much bitcrushing") -- was 8-15 (already halved once from an
     * original 1-15). Halving the worst-case distance from full 16-bit
     * resolution a second time: floor raised from 8 to 12, so the
     * range is now 12-15. At bitDepth=12 the quantization step is
     * already quite fine (~0.0005 of the signal range) -- this range
     * should read as a subtle texture rather than an obvious crush.
     * Worth noting: the rate-reducer just below (down to 100Hz) runs
     * alongside bitcrush and may itself be contributing to a
     * perceived "crushed" character independent of bit depth --
     * flagged, not changed here, since only bitcrushing specifically
     * was reported as excessive. */
    v->bitDepth = 12 + (int)(rand01(&e->rngState) * 4.0);
    if (v->bitDepth > 15) v->bitDepth = 15;
    v->rateReducerMultiplier = 0.5 + rand01(&e->rngState) * 7.5;
    pitchedhold_init(&v->rateReducer);
    vibrato_init(&v->vibrato);
    moog_ladder_init(&v->outputFilter, e->sampleRate);

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
            pitchedhold_init(&layer->pitchedHold);
        } else {
            karplus_init(&layer->karplus);
            /* noiseGen initialized here too (previously only used for
             * filtered-noise layers) -- karplus's ongoing sustain feed
             * in process_layer reuses it. */
            noisegen_init(&layer->noiseGen, xorshift_next(&e->rngState));
            /* Excitation summed from ALL of this recipe's layer
             * colours (including this layer's own), not just its own
             * single colour -- per direct feedback that the Karplus
             * layer should be "fed from the sum of the noise it's
             * randomized with", so it blends with the rest of the
             * patch's noise palette rather than sounding isolated. */
            NoiseColour sourceColours[NOISEBOY_MAX_LAYERS];
            for (int ci = 0; ci < v->numLayers; ci++) {
                sourceColours[ci] = e->recipe[ci].colour;
            }
            double damping = clampd(r->dampingAmount01 * 0.5 + e->params.filterResonance01 * 0.5, 0.0, 1.0);
            karplus_pluck(&layer->karplus, layerFreq, e->sampleRate, &e->rngState, sourceColours, v->numLayers, damping);
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

int noiseboy_any_voice_active(const NoiseboyEngine *e) {
    for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
        if (e->voices[v].active) return 1;
    }
    return 0;
}

static double process_layer(NoiseboyEngine *e, Voice *v, Layer *layer) {
    if (layer->type == LAYER_KARPLUS_STRONG) {
        /* Small ongoing noise injection while the note is held (0
         * once released, letting the string decay/ring out naturally
         * via its own damping) -- per direct feedback for "short
         * sustained notes that ring out" rather than only a single
         * decaying pluck. Reuses layer->noiseGen/colour (otherwise
         * unused on a Karplus layer) rather than the multi-source sum
         * the initial pluck uses -- the ongoing feed doesn't need to
         * be as elaborate as the defining initial burst. */
        double sustainFeed = noisegen_process(&layer->noiseGen, layer->colour);
        double sustainAmount = v->gateOpen ? 0.02 : 0.0;
        return karplus_process(&layer->karplus, sustainFeed, sustainAmount);
    }

    double raw = noisegen_process(&layer->noiseGen, layer->colour);

    /* Sample-and-hold pitch stage, applied BEFORE the filter -- per
     * direct feedback that filter tracking alone wasn't reading as
     * clearly pitched. Hold multiplier varies per layer (via
     * resonanceBias01, reused here rather than adding yet another
     * recipe field) between 1x and 3x the fundamental, so multiple
     * layers don't all buzz at exactly the same grain rate. */
    double detuneMul = pow(2.0, (layer->detuneCents * e->params.detuneSpread01) / 1200.0);
    double holdMultiplier = 1.0 + layer->resonanceBias01 * 2.0;
    raw = pitchedhold_process(&layer->pitchedHold, raw, v->freqHz * detuneMul, e->sampleRate, holdMultiplier);

    /* filterCutoffOffset01: 0.5 = neutral (filter sits exactly at the
     * played pitch, i.e. always tracks it); away from 0.5 brightens or
     * darkens the filter RELATIVE TO the tracked pitch, but never
     * stops tracking it -- the offset is a multiplier applied to the
     * pitch-derived base cutoff, not a replacement for it. */
    double cutoffMul = pow(2.0, (e->params.filterCutoffOffset01 - 0.5) * 4.0);
    /* Velocity is deliberately NOT applied here anymore -- it was
     * originally added as a multiplier on this same cutoff, but this
     * cutoff IS the pitch mechanism for filtered-noise layers (the
     * filter's resonant peak defines the perceived pitch). Modulating
     * it by velocity meant harder-played notes were also detuning
     * sharp relative to softer ones on the same key -- a real, direct
     * correction: velocity now drives a separate OUTPUT FILT stage
     * instead (see noiseboy_process), applied post-mix where it can
     * brighten the overall timbre without touching pitch accuracy at
     * all. envelopeMul below has the same underlying mechanism and
     * could theoretically have the same issue (pitch drifting up
     * during attack, down during release) -- left in place since it
     * wasn't flagged as a problem, but worth listening for if it
     * starts to sound off the same way velocity did. */
    /* Filter cutoff also follows the amplitude envelope itself (rises
     * during attack, falls during release), capped to a max 20%
     * brightening at the envelope's own peak -- per explicit request.
     * envLevel is normalized by velocity01 first since envLevel's own
     * range is 0..velocity01, not 0..1 -- without normalizing, a
     * quietly-played (low velocity) note would only ever reach a
     * fraction of the 20% cap, when the intent is that EVERY note's
     * own envelope shape drives this the same relative amount. */
    double envNorm = (v->velocity01 > 0.001) ? clampd(v->envLevel / v->velocity01, 0.0, 1.0) : 0.0;
    double envelopeMul = 1.0 + envNorm * 0.20;
    double cutoffHz = clampd(v->freqHz * detuneMul * cutoffMul * envelopeMul, 20.0, e->sampleRate * 0.45);
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

        /* AM phase updated here (before vibrato/AM/wavefold all need
         * it) rather than down where AM itself used to compute it --
         * vibrato needs the same shared phase too, per explicit
         * request that it stays in sync with the tremolo. */
        v->amPhase += e->params.amRateHz * dt;
        if (v->amPhase > 1.0) v->amPhase -= floor(v->amPhase);

        /* Vibrato, per explicit request -- introduces gentle pitch
         * modulation "in the noise and Karplus" (both already present
         * in voiceSum by this point, so one instance covers both layer
         * types). Depth saturates at just 15% of AM Depth's own knob
         * travel -- i.e. vibrato reaches its own (small) maximum
         * quickly and stays there for the rest of the knob's range,
         * rather than growing all the way to a dramatic wobble at full
         * knob -- "so that its gentle, like an acoustic instrument".
         * Max depth of 4 samples chosen as a reasoned, not ear-tuned,
         * starting point (see this project's own verification notes
         * on why -- no way to listen to this directly). */
        {
            const double vibratoDepthFactor01 = clampd(e->params.amDepth01 / 0.15, 0.0, 1.0);
            const double vibratoDepthSamples = vibratoDepthFactor01 * 4.0;
            voiceSum = vibrato_process(&v->vibrato, voiceSum, v->amPhase, vibratoDepthSamples);
        }

        /* Per-voice bitcrusher + pitch-following sample-rate reducer,
         * applied to the mixed voice signal (after layers, before AM)
         * -- per explicit request. Rate reducer's rate is derived from
         * the played note (freqHz * this voice's randomized
         * multiplier), floored at 100Hz and ceiled at Nyquist so it
         * always sits somewhere between "100Hz crunch" and "full
         * resolution" as requested, while still tracking pitch.
         *
         * ORDER MATTERS HERE, discovered via a real bug: rate-reduce
         * (sample-and-hold) MUST run before bitcrush, not after. At
         * low bit depths (bitDepth can randomize as low as 1-2),
         * bitcrush quantizes most small-amplitude samples straight to
         * 0 -- a Karplus-Strong voice's naturally quiet signal level
         * meant well over 80% of samples were landing exactly on 0
         * after crushing. Sampling THAT with a sparse hold (updating
         * only every ~46 samples at typical rates) had a real, non-
         * negligible chance of every single hold-update landing on one
         * of those zeroed samples, silencing the entire voice for the
         * whole note. Caught this via a debug seed sweep (seed=1
         * reproduced it consistently) rather than reasoning it out in
         * advance -- worth remembering that new per-voice per-note
         * randomization (bitDepth, rateReducerMultiplier) needs the
         * same seed-sweep verification as everything else, not just
         * the small sample of seeds a manual test happens to try. */
        {
            const double reducerRate = clampd(v->freqHz * v->rateReducerMultiplier, 100.0, e->sampleRate * 0.5);
            voiceSum = pitchedhold_process(&v->rateReducer, voiceSum, reducerRate, e->sampleRate, 1.0);
        }
        voiceSum = bitcrush_process(voiceSum, v->bitDepth);

        /* Per-voice amplitude modulation -- a simple sine tremolo,
         * depth/rate from knobs 3/4. amDepth01=0 leaves the voice
         * completely untouched (multiplying by 1.0 always), matching
         * "controlled by the knobs" rather than being a fixed always-on
         * wobble. */
        const double amCyclePosition = 0.5 * (1.0 - cos(2.0 * M_PI * v->amPhase)); // 0 at the AM peak, 1 at the AM dip
        const double amGain = 1.0 - e->params.amDepth01 * amCyclePosition;

        /* Wavefolder linked to the SAME AM cycle, per explicit request
         * that it "comes in and out with the AM" -- fold amount peaks
         * exactly when amGain is at its quietest, so the dip in volume
         * is accompanied by a dip into more distorted/folded texture
         * rather than the two being unrelated. Zero when AM depth is
         * zero, matching "controlled by the knobs" like AM itself. */
        const double foldAmount = e->params.amDepth01 * amCyclePosition;
        voiceSum = wavefold_process(voiceSum, foldAmount);

        /* OUTPUT FILT -- a second lowpass applied after the whole
         * synth signal (everything above: layers, vibrato, bitcrush,
         * rate-reduce, AM, wavefold) but before the amplitude
         * envelope. Velocity-driven brightness lives here instead of
         * on the per-layer pitch-tracking filters (see voice_start's
         * own comment on why that was wrong). 800Hz (soft) to 16kHz
         * (bright) across the velocity range is the base, further
         * multiplied by knob 8 (same offset-around-neutral pattern as
         * knob 1's filterCutoffOffset01) -- knob 8 was deliberately
         * moved here from Level per explicit request, making it "the
         * final knob to control audible sound" since this filter is
         * the last stage before the envelope/output. Resonance is
         * fixed at 0.0 (NOT knob-controlled, not even the small fixed
         * 0.15 this had before) per direct correction -- even that
         * modest resonance was apparently still creating a perceivable
         * pitch-like artifact, which a filter with zero pitch-defining
         * role shouldn't be doing at all. */
        {
            const double outputCutoffMul = pow(2.0, (e->params.outputFilterFreq01 - 0.5) * 4.0);
            const double outputCutoff = clampd((800.0 + v->velocity01 * 15200.0) * outputCutoffMul, 20.0, e->sampleRate * 0.45);
            moog_ladder_set(&v->outputFilter, outputCutoff, 0.0, 0.1);
            voiceSum = moog_ladder_process(&v->outputFilter, voiceSum);
        }

        mix += voiceSum * v->envLevel * amGain;
    }

    /* Single shared drive/saturation stage on the final mix, per
     * explicit request for something to give the noise more volume
     * and colour -- deliberately one block applied once here, not
     * per-voice or per-layer. Simple pre-gain into tanh (same
     * saturation approach used throughout this project family's
     * filters) -- NOT normalized back down afterward, since the
     * saturation's natural loudness/density increase at higher drive
     * IS the "more volume" part of the request, not a side effect to
     * cancel out. */
    double driveGain = 1.0 + e->params.drive01 * 6.0;
    mix = tanh(mix * driveGain);

    /* Global, always-on tape saturation stage -- per explicit request,
     * distinct from the knob-controlled Drive above (which can be
     * turned down to 0; this can't). Placed after Drive specifically
     * because it has its own built-in compressor, which tames whatever
     * loudness Drive added before the final tanh warms/rounds it off
     * -- a sensible "drive then tame" order rather than two unrelated
     * saturation stages fighting each other. */
    mix = tapesat_process(&e->tapeSat, mix, e->sampleRate);

    return mix * e->params.masterLevel01;
}
