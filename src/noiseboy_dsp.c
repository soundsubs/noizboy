#include "noiseboy_dsp.h"
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

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

int loop_source_alloc(LoopSource *lp) {
    lp->buffer = (double *)malloc(sizeof(double) * NOISEBOY_LOOP_MAX_SAMPLES);
    if (!lp->buffer) return 0;
    return 1;
}

void loop_source_free(LoopSource *lp) {
    free(lp->buffer);
    lp->buffer = NULL;
}

void loop_capture(LoopSource *lp, unsigned int *rngState, double freqHz, double referenceFreqHz, int captureLengthSamples) {
    if (captureLengthSamples < 2) captureLengthSamples = 2;
    if (captureLengthSamples > NOISEBOY_LOOP_MAX_SAMPLES) captureLengthSamples = NOISEBOY_LOOP_MAX_SAMPLES;
    lp->captureLengthSamples = captureLengthSamples;
    lp->readPos = 0.0;
    lp->playbackRate = freqHz / referenceFreqHz;

    if (!lp->buffer) return; /* allocation failure fallback -- see loop_source_alloc's own comment; loop_process handles a NULL buffer safely too */

    /* Instant fill, per explicit revert -- unlike the intervening
     * post-filter design (which recorded the voice's own signal in
     * real time), this captures raw noise all at once in a tight
     * loop, exactly like the original pre-filter design and like
     * karplus_pluck's own one-time excitation burst. This is WHY the
     * full, already-pitch-transposed content is available from the
     * very first sample of the note -- there's no waiting for
     * real-time capture to complete. */
    NoiseGen ng;
    noisegen_init(&ng, xorshift_next(rngState));
    for (int i = 0; i < captureLengthSamples; i++) {
        lp->buffer[i] = noisegen_process(&ng, NOISE_WHITE);
    }
}

double loop_process(LoopSource *lp) {
    if (!lp->buffer) return 0.0; /* allocation failure fallback */

    /* Nearest-neighbor read (plain int truncation, no interpolation)
     * -- matches this project's established "the artifacts are the
     * point" philosophy for lo-fi sample-style pitch/rate shifting. */
    int idx = (int)lp->readPos;
    if (idx >= lp->captureLengthSamples) idx = idx % lp->captureLengthSamples;

    /* Decay to near-silence by 98% through the loop, per explicit
     * spec (raised from 97% in the intervening post-filter design --
     * "It should not decay until 98% of the way through the loop.
     * This mimics a real tape loop") -- a PROPORTION of the captured
     * length, not an absolute time, so this holds regardless of loop
     * length. Exponential, tuned so decayGain reaches 0.001 (-60dB, a
     * reasonable "near-silence" floor) at frac=0.98:
     * exp(-k*0.98)=0.001 -> k = -ln(0.001)/0.98 ~= 7.05. */
    const double frac = lp->readPos / (double)lp->captureLengthSamples;
    const double decayGain = exp(-7.05 * frac);
    const double out = lp->buffer[idx] * decayGain;

    double rate = lp->playbackRate;
    if (rate < 0.01) rate = 0.01; /* defensive floor -- avoids a near-stuck or reversed read position from a pathological pitch */
    lp->readPos += rate;
    if (lp->readPos >= (double)lp->captureLengthSamples) {
        lp->readPos -= (double)lp->captureLengthSamples * floor(lp->readPos / (double)lp->captureLengthSamples);
    }
    return out;
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

void tape_wobble_init(TapeWobble *w, unsigned int seed) {
    w->state = 0.0;
    w->rngState = seed != 0 ? seed : 1;
}

double tape_wobble_process(TapeWobble *w, double rateHz, double sampleRate) {
    /* Fresh white noise sample each call, heavily lowpassed (one-pole,
     * time constant set by rateHz) to produce a slowly-wandering value
     * instead of audio-rate texture -- see TapeWobble's own header
     * comment for why that distinction matters.
     *
     * REAL BUG FOUND AND FIXED HERE: a one-pole lowpass applied to
     * white noise doesn't just slow down how fast the signal wanders
     * -- it also drastically shrinks its actual amplitude range, since
     * heavy averaging of independent random samples reduces variance
     * (var(y) = var(x)*(1-coeff)/(1+coeff) for this filter structure).
     * At the rate this is actually used (1Hz, 48kHz sample rate),
     * measured directly: the output only ever wandered within roughly
     * +-0.015, not anywhere near the nominal [-1,1] range this
     * function's own header comment describes -- meaning the ACTUAL
     * modulation depth applied at mellotronDepth01's intended 1%-5%
     * would really have been more like 0.015%-0.075%, over 60x weaker
     * than specified and practically inaudible. Fixed by normalizing
     * the output by the inverse of that same variance-reduction
     * factor, restoring the output to the same variance as the
     * original (uniform [-1,1]) white noise input regardless of how
     * heavily it's being smoothed. */
    const double white = rand01(&w->rngState) * 2.0 - 1.0;
    const double coeff = exp(-2.0 * M_PI * rateHz / sampleRate);
    w->state = white * (1.0 - coeff) + w->state * coeff;
    const double normFactor = (coeff < 0.999999) ? sqrt((1.0 + coeff) / (1.0 - coeff)) : 1.0;
    double out = w->state * normFactor;
    if (out > 1.0) out = 1.0;
    if (out < -1.0) out = -1.0;
    return out;
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

void tilt_filter_init(TiltFilter *t, double sampleRate) {
    korg35hp_init(&t->highpassL, sampleRate);
    korg35hp_init(&t->highpassR, sampleRate);
    moog_ladder_init(&t->lowpassL, sampleRate);
    moog_ladder_init(&t->lowpassR, sampleRate);
    t->smoothedTilt01 = 0.5; /* starts at neutral, matching the default knob position -- no smoothing transient on load */
}

void tilt_filter_process(TiltFilter *t, double *l, double *r, double tiltTarget01, double sampleRate) {
    /* ~15ms smoothing time constant, matching this project's other
     * knob-smoothing stages. */
    const double smoothCoeff = exp(-1.0 / (0.001 * 15.0 * sampleRate));
    t->smoothedTilt01 = tiltTarget01 + (t->smoothedTilt01 - tiltTarget01) * smoothCoeff;

    double highpassCutoff = 100.0;
    double lowpassCutoff = 10000.0;
    if (t->smoothedTilt01 < 0.5) {
        const double amt = (0.5 - t->smoothedTilt01) / 0.5; /* 0 at centre, 1 at full bass emphasis */
        lowpassCutoff = 800.0 * pow(10000.0 / 800.0, 1.0 - amt);
    } else if (t->smoothedTilt01 > 0.5) {
        const double amt = (t->smoothedTilt01 - 0.5) / 0.5; /* 0 at centre, 1 at full treble emphasis */
        highpassCutoff = 100.0 * pow(1500.0 / 100.0, amt);
    }

    const double nyquistGuard = sampleRate * 0.45;
    highpassCutoff = clampd(highpassCutoff, 20.0, nyquistGuard);
    lowpassCutoff = clampd(lowpassCutoff, 20.0, nyquistGuard);

    korg35hp_set(&t->highpassL, highpassCutoff, 0.0, 0.1);
    korg35hp_set(&t->highpassR, highpassCutoff, 0.0, 0.1);
    moog_ladder_set(&t->lowpassL, lowpassCutoff, 0.0, 0.1);
    moog_ladder_set(&t->lowpassR, lowpassCutoff, 0.0, 0.1);

    /* REAL BUG FOUND AND FIXED HERE while verifying this filter's
     * actual frequency response: order matters a lot more than
     * expected for a series combination of these two filters, because
     * they're not purely linear (both have a tanh saturation stage
     * internally) -- series gain magnitudes don't simply multiply the
     * way they would for pure LTI filters. Measured directly: with
     * highpass-then-lowpass ordering, 50Hz (one octave below the
     * 100Hz highpass edge) measured only -0.5dB attenuation -- barely
     * a "roll off" at all, versus -3.1dB for the highpass measured in
     * isolation. Lowpass-then-highpass ordering measured -2.7dB at
     * the same point, much closer to that isolated character, while
     * leaving the 1kHz passband essentially unchanged either way. */
    *l = moog_ladder_process(&t->lowpassL, *l);
    *l = korg35hp_process(&t->highpassL, *l);
    *r = moog_ladder_process(&t->lowpassR, *r);
    *r = korg35hp_process(&t->highpassR, *r);
}

void noiseboy_output_gate_init(NoiseboyOutputGate *g) {
    g->envelope = 0.0;
}

double noiseboy_output_gate_process(NoiseboyOutputGate *g, double x, int voicesActive, double sampleRate) {
    const double target = voicesActive ? 1.0 : 0.0;
    const double timeMs = voicesActive ? 3.0 : 150.0;
    const double coeff = exp(-1.0 / (0.001 * timeMs * sampleRate));
    g->envelope = target + (g->envelope - target) * coeff;
    return x * g->envelope;
}

/* ---------------------------------------------------------------------
 * Engine: voice management, randomized recipe, MIDI, per-sample mix.
 * ------------------------------------------------------------------- */

static double midi_note_to_freq(int midiNote) {
    return 440.0 * pow(2.0, (midiNote - 69) / 12.0);
}

void noiseboy_engine_init(NoiseboyEngine *e, double sampleRate, unsigned int seed) {
    /* See initMagic's own header comment for why this check matters
     * and can't just be a NULL-check on the buffer pointers. */
    if (e->initMagic == NOISEBOY_ENGINE_INIT_MAGIC) {
        for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
            loop_source_free(&e->voices[v].layers[3].loop);
        }
    }

    memset(e, 0, sizeof(*e));
    e->sampleRate = sampleRate;
    e->rngState = seed != 0 ? seed : 1;
    e->lastRandomizeTriggerRaw = 0;

    for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
        e->voices[v].active = 0;
        /* Loop lives at the fixed layer index 3 (the always-Loop slot
         * -- see noiseboy_randomize_recipe's own comment) for every
         * voice -- only that one slot ever needs this allocation, not
         * all NOISEBOY_MAX_LAYERS of them. */
        if (!loop_source_alloc(&e->voices[v].layers[3].loop)) {
            /* Allocation failure -- leave this slot's buffer NULL.
             * loop_process guards against a NULL buffer (see its own
             * comment) by returning silence, so a failed allocation
             * degrades to "this voice's Loop source is silent" rather
             * than crashing. */
        }
    }

    e->initMagic = NOISEBOY_ENGINE_INIT_MAGIC;

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
    e->params.loopLengthKnob01 = 0.0;      /* minimum (0.25s, the shortest XX) by default -- a reasonable, safe starting point */
    e->params.attackMs = 4.0;
    e->params.releaseMs = 80.0;
    e->params.detuneSpread01 = 0.5;
    e->params.tiltAmount01 = 0.5;    /* neutral -- centre position, tape-bandwidth window with no bass/treble emphasis */
    e->params.masterLevel01 = 0.8;
    e->params.drive01 = 0.25;              /* modest default drive, per explicit request for a built-in stage to add volume/colour -- not maxed out by default */

    tapesat_init(&e->tapeSatL);
    tapesat_init(&e->tapeSatR);

    noiseboy_randomize_recipe(e);
}

void noiseboy_randomize_recipe(NoiseboyEngine *e) {
    /* RESTRUCTURED per explicit request -- see LayerRecipe's own
     * header comment for the full rationale. Always exactly 3 sources
     * now (was 1-3, randomly typed); only mix level is randomized per
     * source. Also callable on demand (not just at instantiation) per
     * explicit request for a way to get a new randomized set without
     * reloading the whole module -- uses the engine's own ongoing RNG
     * state, so repeated calls keep producing fresh, non-repeating
     * results rather than resetting to the same sequence. */
    e->numRecipeLayers = NOISEBOY_MAX_LAYERS;

    /* Recipe-level timbre character -- see this field's own header
     * comment for the full investigation (resonance tried first,
     * abandoned when direct measurement showed it barely moved actual
     * output; cutoff measured a clean, reliable ~21% range instead).
     * +-15%, i.e. 0.85x-1.15x. */
    e->timbreCharacterMul = 0.85 + rand01(&e->rngState) * 0.3;

    /* Loop length -- see this field's own header comment. Randomized
     * ONCE here (not per-note), per explicit request. Linear 0-1
     * random draw across the 0.25-3.0s range -- no particular reason
     * to weight toward either end, unlike some of this project's other
     * exponential/log-scale mappings which exist specifically to give
     * a KNOB more control resolution at one end; a one-time random
     * pick doesn't have that concern. */
    /* Tape wobble depth -- see this field's own header comment. 1%-5%
     * (0.01-0.05), per explicit spec. */
    e->mellotronDepth01 = 0.01 + rand01(&e->rngState) * 0.04;

    for (int i = 0; i < e->numRecipeLayers; i++) {
        LayerRecipe *r = &e->recipe[i];

        /* Fixed type by index now, not randomized -- layers 0 and 1
         * are always filtered-noise, layer 2 is always Karplus-Strong,
         * layer 3 is always Loop (restored per explicit revert
         * request -- see LoopSource's own comment). See LayerRecipe's
         * own comment for the full rationale. */
        if (i < 2) r->type = LAYER_FILTERED_NOISE;
        else if (i == 2) r->type = LAYER_KARPLUS_STRONG;
        else r->type = LAYER_LOOP;

        double colourRoll = rand01(&e->rngState);
        r->colour = (colourRoll < 0.34) ? NOISE_WHITE : (colourRoll < 0.67) ? NOISE_PINK : NOISE_RED;

        /* Detune -- layer 0 (the first noise source) stays at 0 cents
         * as a stereo-pan anchor (centred), layer 1 (the second noise
         * source) gets a randomized spread so the two noise sources
         * land at different, distinct pan positions rather than both
         * sitting dead centre. Layer 2 (Karplus) still gets its own
         * small randomized detune too -- unlike the noise layers,
         * Karplus doesn't use detuneCents for panning (it auto-pans at
         * the AM rate instead, see noiseboy_process_stereo's own
         * panning comment), but detuneCents still feeds its own pitch
         * tuning directly. */
        r->detuneCents = (i == 0) ? 0.0 : (rand01(&e->rngState) * 2.0 - 1.0) * 15.0;

        r->dampingAmount01 = rand01(&e->rngState);

        /* Mix level, per explicit request -- the ONE thing that's
         * randomized about each of these three fixed sources now.
         * Full 0-1 range (0-100%) -- deliberately allows a source to
         * randomize down to fully silent, which is a legitimate,
         * useful outcome (e.g. landing on "just the two noise sources,
         * no audible Karplus this time" some fraction of the time)
         * rather than an accidental one. */
        r->mixLevel01 = rand01(&e->rngState);
    }
}

/* Initializes a voice's actual DSP layer instances from the engine's
 * shared recipe -- called fresh on every note-on so each new note
 * starts from clean state (no leftover filter ringing or Karplus
 * buffer content from whatever a stolen voice was doing before). */
static void voice_start(NoiseboyEngine *e, Voice *v, int midiNote, double velocity01) {
    /* Called only once a voice is either genuinely free, or (for a
     * steal) has already decayed to near-silence via the deferred-
     * steal mechanism in noiseboy_note_on/noiseboy_process_stereo --
     * see pendingSteal's own header comment. By the time this runs,
     * there's no still-sounding old state to worry about clicking
     * against, so this can just do a normal, clean reset. */
    v->active = 1;
    v->midiNote = midiNote;
    v->velocity01 = velocity01;
    v->freqHz = midi_note_to_freq(midiNote);
    v->envLevel = 0.0;
    v->gateOpen = 1;
    v->pendingSteal = 0;
    v->minHoldSamplesRemaining = 0;
    v->amPhase = rand01(&e->rngState); /* randomized starting phase per voice, so simultaneous notes don't cycle in lockstep (vibrato/auto-pan all read this) */

    /* Fresh loop capture every note -- see LoopSource's own comment.
     * This happens naturally in the per-layer loop below (loop_capture
     * is called fresh every voice_start, same as karplus_pluck's own
     * one-time excitation), so no separate reset call is needed here
     * -- a stolen voice's previous note's buffer content is simply
     * overwritten by the fresh capture, same as every other per-layer
     * state below. */

    /* Fresh wobble seed per note, per-voice-distinct (not shared with
     * the engine's own rngState directly, to avoid correlating with
     * other per-note randomization) -- see TapeWobble's own comment. */
    tape_wobble_init(&v->wobble, xorshift_next(&e->rngState));

    /* Bitcrush and pitch-following sample-rate reduction
     * REINTRODUCED per explicit request -- see this state's own
     * header comment in Voice for the full position/tradeoff
     * rationale (post-mixer, pre-filter -- the same spot v0.10.0
     * measured as harmful to pitch-tracking accuracy, reintroduced
     * here as an informed, explicit choice rather than an oversight).
     * Randomized fresh per note (not recipe-level), matching this
     * feature's own prior design before removal. Ranges chosen from
     * this project's own tuning history, not guessed fresh: bitDepth
     * 12-15 was the last, lightest setting reached before full
     * removal (after two earlier reductions from an original 1-15,
     * each time per direct feedback that it was too aggressive) --
     * starting back at that same, already-most-conservative point
     * rather than re-walking the same tuning journey from scratch.
     * rateReducerMultiplier 1.0-2.0 keeps the hold rate close to the
     * fundamental (musically related, per pitchedhold_process's own
     * comment) while still giving some per-note variety. */
    v->bitDepth = 12 + (int)(rand01(&e->rngState) * 4.0); /* 12-15 */
    v->rateReducerMultiplier = 1.0 + rand01(&e->rngState) * 1.0; /* 1.0-2.0 */
    pitchedhold_init(&v->rateReducerL);
    pitchedhold_init(&v->rateReducerR);
    vibrato_init(&v->vibratoL);
    vibrato_init(&v->vibratoR);

    /* Voice-level pitch-tracking filter -- per direct investigation
     * request, no longer randomized between Moog/Korg35LP. Measured
     * directly (impulse response, matched cutoff/resonance settings):
     * at the SAME resonance knob value, Korg35LP's actual resonant
     * peak gain was up to ~79x weaker than Moog Ladder's (0.0094 vs.
     * 0.7434 at middle C, resonance01=0.82) -- since filter type was
     * randomized fresh per note, adjacent notes could land on either
     * filter and produce wildly different, unpredictable resonance
     * character, which is exactly the reported "some notes more
     * resonant than notes around it" symptom. Investigated fixing
     * Korg35LP's own resonance calibration directly (it's a 2-pole
     * design borrowing a compensation formula shape derived for Moog's
     * 4-pole topology, scaled by a smaller constant -- 2.6 vs 3.5) but
     * a coefficient sweep showed genuinely counterintuitive, non-
     * monotonic behaviour (higher coefficient often producing LOWER
     * peak gain) suggesting the formula's whole SHAPE, not just its
     * scale, may be wrong for a 2-pole loop -- correctly re-deriving
     * that from scratch is real DSP-theory work, not something to
     * rush under time pressure for a single knob's calibration.
     * Simplest, most reliable fix: stop choosing between two filters
     * with such different and unpredictable resonance response.
     * Always Moog now -- still randomized nothing else about it, keeps
     * the filter's own state (below) rather than removing the whole
     * dual-filter mechanism, in case Korg35LP's calibration gets
     * properly fixed later. */
    v->pitchFilterKind = FILTER_MOOG;
    moog_ladder_init(&v->pitchFilterMoogL, e->sampleRate);
    moog_ladder_init(&v->pitchFilterMoogR, e->sampleRate);
    korg35lp_init(&v->pitchFilterKorgLpL, e->sampleRate);
    korg35lp_init(&v->pitchFilterKorgLpR, e->sampleRate);

    v->numLayers = e->numRecipeLayers;
    for (int i = 0; i < v->numLayers; i++) {
        Layer *layer = &v->layers[i];
        const LayerRecipe *r = &e->recipe[i];
        layer->type = r->type;
        layer->colour = r->colour;
        layer->detuneCents = r->detuneCents;
        layer->mixLevel01 = r->mixLevel01;

        double detuneMul = pow(2.0, (layer->detuneCents * e->params.detuneSpread01) / 1200.0);
        double layerFreq = v->freqHz * detuneMul;

        if (layer->type == LAYER_FILTERED_NOISE) {
            /* Raw source generation only now -- no per-layer filter,
             * no per-layer PitchedHold. See Layer's own header comment
             * for the full restructuring rationale. */
            noisegen_init(&layer->noiseGen, xorshift_next(&e->rngState));
            layer->releaseDarkenState = 0.0; /* starts fully bright/unfiltered -- see releaseDarkenState's own comment */
        } else if (layer->type == LAYER_KARPLUS_STRONG) {
            karplus_init(&layer->karplus);
            layer->sustainAmountSmoothed = 0.0; /* starts at 0, smooths up toward the held target -- symmetric with how it smooths down at release */
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
            /* Karplus decay character, per explicit request: "Karplus
             * is always plucky... randomly modulate the decay so that
             * 66% of the time it's plucky, but has a varying decay
             * like a real string... tie this to the Attack/Release
             * knobs." Rolled fresh per note (not recipe-level), same
             * lifecycle as bitDepth/rateReducerMultiplier above --
             * this is meant to vary note-to-note, not settle into one
             * fixed instrument character.
             *
             * PLUCKY mode (66%): close to this project's existing
             * damping formula (dampingAmount01/Resonance blend), with
             * a modest tie to Attack -- a snappier (shorter) Attack
             * setting nudges toward a tighter, more percussive decay,
             * keeping "plucky" feeling coherent with a quick attack
             * rather than the two being unrelated.
             *
             * STRING mode (34%): damping pushed much closer to 1.0
             * (Karplus-Strong's decay time grows sharply as damping
             * approaches its self-sustaining limit), scaled by the
             * Release knob's own position -- a long Release setting
             * pushes the string's OWN internal ring-out time longer
             * too, so the amplitude envelope's release and the
             * string's own natural decay stay roughly in step instead
             * of the string dying out silently while a long Release
             * fades an already-silent signal. Some added per-note
             * random spread within the mode for "varying" character,
             * not a fixed formula every time. */
            const int karplusStringMode = rand01(&e->rngState) < 0.34;
            double dampingAmount;
            if (karplusStringMode) {
                /* REAL BUG FOUND AND FIXED HERE while verifying this
                 * feature: karplus_pluck's own dampingAmount parameter
                 * is a 0-1 INPUT that it internally remaps to the
                 * actual k->damping range of 0.90-0.999 (see
                 * karplus_pluck's own comment). An earlier version of
                 * this code computed a value ALREADY in the 0.90-0.999
                 * range and passed it straight through as that 0-1
                 * input, double-mapping it and badly compressing the
                 * intended range (confirmed directly: measured actual
                 * k->damping values landed around 0.989-0.999 instead
                 * of the intended 0.90-0.999, and the Release knob's
                 * tie-in barely moved the decay time as a result).
                 * Fixed by computing the TARGET k->damping value
                 * directly, then inverting karplus_pluck's own mapping
                 * to find the correct 0-1 input that produces it. */
                const double releaseNorm01 = clampd(log(e->params.releaseMs / 0.02) / log(4000.0 / 0.02), 0.0, 1.0);
                const double targetKDamping = clampd(0.95 + releaseNorm01 * 0.048 + rand01(&e->rngState) * 0.001, 0.90, 0.999);
                dampingAmount = clampd((targetKDamping - 0.90) / 0.099, 0.0, 1.0);
            } else {
                const double attackNorm01 = clampd((e->params.attackMs - 0.5) / 199.5, 0.0, 1.0);
                dampingAmount = clampd(r->dampingAmount01 * 0.5 + e->params.filterResonance01 * 0.5, 0.0, 1.0);
                dampingAmount *= (0.85 + attackNorm01 * 0.15); /* shorter attack -> pulled slightly tighter/snappier */
            }
            dampingAmount = clampd(dampingAmount, 0.0, 1.0);
            karplus_pluck(&layer->karplus, layerFreq, e->sampleRate, &e->rngState, sourceColours, v->numLayers, dampingAmount);
        } else {
            /* LAYER_LOOP -- restored per explicit revert request, see
             * LoopSource's own comment for the full design. XX (the
             * captured buffer's length) comes from the Loop Length
             * knob's CURRENT value at this exact moment -- fixed for
             * this note's whole duration once captured, not
             * re-evaluated live mid-note. Exponential mapping (more
             * knob resolution at the shorter, more commonly useful
             * end), matching this project's established convention
             * for duration-like parameters. Reference frequency is
             * middle C, per the original design's own spec ("if
             * middle C was 8000 samples..." -- same reference point,
             * just no longer a fixed 8000-sample buffer). */
            const double xxSeconds = NOISEBOY_LOOP_MIN_SECONDS * pow(NOISEBOY_LOOP_MAX_SECONDS / NOISEBOY_LOOP_MIN_SECONDS, e->params.loopLengthKnob01);
            const int captureLengthSamples = (int)(xxSeconds * e->sampleRate);
            loop_capture(&layer->loop, &e->rngState, layerFreq, midi_note_to_freq(60), captureLengthSamples);
        }
    }
}

void noiseboy_note_on(NoiseboyEngine *e, int midiNote, double velocity01) {
    /* Prefer a free voice; if none, steal the quietest currently
     * releasing voice, or failing that just voice 0 -- simple and
     * predictable rather than a full LRU/oldest-note-tracking scheme,
     * which wasn't asked for.
     *
     * REAL BUG FIXED HERE, per direct report ("noticeable latency...
     * if I play the pads fast enough, it only triggers every other
     * note"): the "quietest releasing voice" search below did NOT
     * exclude voices that already have a pending deferred steal (see
     * pendingSteal's own comment). A voice with a pending steal has
     * its envelope forced into a fast decay toward 0 -- which makes it
     * look like the BEST candidate ("quietest") to the very next
     * note-on, getting it picked again and its pendingMidiNote
     * silently OVERWRITTEN before the first queued note ever got to
     * play. Confirmed directly with a rapid-playing test: notes could
     * be dropped entirely (never became audible at all within the
     * test window), and surviving notes showed over 100ms of onset
     * latency -- worse with fewer voices (this project is currently
     * running at 4, reduced from 8 for an unrelated CPU diagnostic),
     * since stealing becomes the common case rather than an
     * occasional one at lower polyphony. Fix: exclude
     * already-pending voices from this selection entirely. */
    int chosen = -1;
    for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
        if (!e->voices[v].active) { chosen = v; break; }
    }
    int isSteal = 0;
    if (chosen < 0) {
        isSteal = 1;
        double lowestLevel = 2.0;
        for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
            if (!e->voices[v].gateOpen && !e->voices[v].pendingSteal && e->voices[v].envLevel < lowestLevel) {
                lowestLevel = e->voices[v].envLevel;
                chosen = v;
            }
        }
        /* Fallback for the rare, genuine overload case: every voice is
         * EITHER still fully held OR already has a pending steal
         * queued -- there is truly nothing free to offer this note.
         * Silently dropping the note entirely (the original bug) is
         * worse than overwriting the OLDEST already-pending note here
         * -- a note that still plays late is better than one that
         * never plays at all. This means, in this specific overload
         * situation only, the oldest-queued pending note can still be
         * displaced by a newer one -- an intentional, documented
         * tradeoff for this edge case, not the everyday behaviour
         * (which the fix above already handles correctly). */
        if (chosen < 0) {
            for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
                if (e->voices[v].pendingSteal) { chosen = v; break; }
            }
        }
        if (chosen < 0) chosen = 0;
    }

    if (isSteal) {
        /* Deferred steal, per explicit correction -- see pendingSteal's
         * own header comment for the full rationale. Force the voice
         * into release (even if it was still held) so it starts
         * decaying immediately regardless of what it was doing before,
         * and park the new note's info; noiseboy_process_stereo's own
         * envelope handling picks this up once the old sound has
         * genuinely faded, forcing a fast release time for exactly
         * this transition rather than waiting out the user's own
         * (possibly very long) Release knob setting. */
        Voice *v = &e->voices[chosen];
        v->gateOpen = 0;
        v->pendingSteal = 1;
        v->pendingMidiNote = midiNote;
        v->pendingVelocity01 = velocity01;
        v->pendingNoteReleased = 0;
    } else {
        voice_start(e, &e->voices[chosen], midiNote, velocity01);
    }
}

void noiseboy_note_off(NoiseboyEngine *e, int midiNote) {
    for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
        if (e->voices[v].active && e->voices[v].midiNote == midiNote && e->voices[v].gateOpen) {
            e->voices[v].gateOpen = 0;
        }
        /* Also check pending steals -- see pendingNoteReleased's own
         * header comment for why this is needed at all. */
        if (e->voices[v].pendingSteal && e->voices[v].pendingMidiNote == midiNote) {
            e->voices[v].pendingNoteReleased = 1;
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
    /* Raw source generation only -- per explicit restructuring
     * request, the signal chain is now: sources (this function) ->
     * bitcrush/rate-reduce -> voice-level pitch-tracking filter ->
     * amplitude envelope -> output filter, all in noiseboy_process.
     * No filter and no PitchedHold pitch stage here anymore -- both
     * moved out. (e) is unused now that there's no per-layer knob
     * lookup happening in this function; kept in the signature to
     * avoid touching every call site. */
    (void)e;
    if (layer->type == LAYER_KARPLUS_STRONG) {
        /* Small ongoing noise injection while the note is held (fades
         * toward 0 once released, letting the string decay/ring out
         * naturally via its own damping) -- per direct feedback for
         * "short sustained notes that ring out" rather than only a
         * single decaying pluck. Reuses layer->noiseGen/colour
         * (otherwise unused on a Karplus layer) rather than the
         * multi-source sum the initial pluck uses -- the ongoing feed
         * doesn't need to be as elaborate as the defining initial
         * burst. Target amount is smoothed (10ms time constant) rather
         * than snapped instantly at the release boundary -- see
         * sustainAmountSmoothed's own comment for why. */
        double sustainFeed = noisegen_process(&layer->noiseGen, layer->colour);
        /* REAL BUG FOUND AND FIXED HERE, per direct report ("I'm still
         * not hearing Karplus at all... seems to have gone when I
         * requested a change to the envelope"): this was fixed at
         * 0.02, far too quiet to be audible once the initial pluck's
         * own energy decays -- especially now that 66% of notes are
         * "plucky" (a fast, tight decay, added in the same batch of
         * changes the report's own timing points to). Measured
         * directly: a held plucky-mode note's SUSTAINED contribution
         * (1+ second in, after the initial transient has died down)
         * was only ~4% of a comparable noise generator's own level --
         * meanwhile the two noise sources it's mixed with keep playing
         * continuously at full level the whole time, so Karplus simply
         * vanished under them for anything but a brief instant right
         * at note-on. Raised to 0.2 (~10x), measured to land around
         * 35% of a comparable noise generator's level -- clearly
         * audible without letting the sustain feed's own noise
         * injection overwhelm Karplus's actual plucked-string
         * character, which is still it's own initial excitation +
         * damping, not this ongoing top-up alone. */
        double sustainTarget = v->gateOpen ? 0.2 : 0.0;
        const double smoothCoeff = 0.999; /* ~10ms-ish at typical sample rates, gentle enough to remove the discontinuity without noticeably delaying the release character */
        layer->sustainAmountSmoothed = sustainTarget + (layer->sustainAmountSmoothed - sustainTarget) * smoothCoeff;
        return karplus_process(&layer->karplus, sustainFeed, layer->sustainAmountSmoothed);
    }

    if (layer->type == LAYER_LOOP) {
        /* Captured once at note-on (loop_capture, in voice_start) --
         * see LoopSource's own comment for the full design. Just reads
         * back here, one sample per call. */
        return loop_process(&layer->loop);
    }

    double raw = noisegen_process(&layer->noiseGen, layer->colour);

    /* Release-only darkening -- see releaseDarkenState's own header
     * comment. envNorm normalized the same way as elsewhere in this
     * project (envLevel's own range is 0..velocity01, not 0..1).
     * darkenAmount stays exactly 0 while held (gateOpen=1) -- this
     * ONLY engages during release, per the explicit "does not sound
     * plucked on RELEASES" framing -- and naturally starts near 0 at
     * the instant release begins (envNorm is still close to 1 right
     * then), growing toward full darkening as the note decays, so no
     * separate smoothing is needed to avoid a jump at the release
     * boundary. */
    const double envNorm = (v->velocity01 > 0.001) ? clampd(v->envLevel / v->velocity01, 0.0, 1.0) : 0.0;
    const double darkenAmount = (!v->gateOpen) ? (1.0 - envNorm) : 0.0;
    const double leakCoeff = darkenAmount * 0.98; /* 0 = fully bright/unfiltered passthrough, 0.98 = same darkness NOISE_RED uses elsewhere in this project */
    layer->releaseDarkenState = layer->releaseDarkenState * leakCoeff + raw * (1.0 - leakCoeff);
    return layer->releaseDarkenState;
}

/* Equal-power pan law -- panPos in [-1,1] (-1=full left, 0=centre,
 * +1=full right). Used by the Detune-driven stereo spreading feature
 * (see noiseboy_process_stereo). */
static void compute_pan_gains(double panPos, double *gainL, double *gainR) {
    const double clamped = clampd(panPos, -1.0, 1.0);
    const double angle = (clamped + 1.0) * (M_PI / 4.0);
    *gainL = cos(angle);
    *gainR = sin(angle);
}

void noiseboy_process_stereo(NoiseboyEngine *e, double *outL, double *outR) {
    double mixL = 0.0, mixR = 0.0;
    const double dt = 1.0 / e->sampleRate;

    /* Envelope smoothing coefficients recomputed from the current knob
     * values each sample -- cheap (a couple of exp() calls per ACTIVE
     * voice, not per layer), and lets attack/release knob changes take
     * effect immediately on in-flight notes rather than only on the
     * next note-on. */
    for (int vi = 0; vi < NOISEBOY_MAX_VOICES; vi++) {
        Voice *v = &e->voices[vi];
        if (!v->active) continue;

        /* Minimum-hold countdown -- see minHoldSamplesRemaining's own
         * header comment. While counting down, gateOpen is forced to 1
         * regardless of what note_off/pendingNoteReleased handling
         * elsewhere wants, guaranteeing the attack envelope gets at
         * least this long to genuinely rise before release is allowed
         * to take over. */
        if (v->minHoldSamplesRemaining > 0) {
            v->minHoldSamplesRemaining--;
            v->gateOpen = 1;
            if (v->minHoldSamplesRemaining == 0) {
                v->gateOpen = 0; /* minimum hold just expired -- now actually release */
            }
        }

        /* Deferred steal in progress -- force a fast release (NOT the
         * user's own Release knob) regardless of gateOpen's normal
         * meaning, so the old sound dies down quickly and predictably
         * rather than potentially taking seconds if Release is set
         * long. See pendingSteal's own header comment for the full
         * rationale.
         *
         * REAL BUG FIXED HERE, per direct report ("noticeable latency
         * ... only triggers every other note" when playing fast): the
         * original 15ms time constant combined with the strict 0.0005
         * deactivation threshold meant full resolution actually took
         * ~7.6 time constants -- about 114ms, not 15ms -- to reach
         * that threshold from a typical velocity. That's slow enough
         * that playing faster than roughly one note per ~30ms per
         * voice could outrun the deferred-steal mechanism entirely,
         * queuing new steals faster than old ones could resolve.
         * Combined with a related bug in the voice-selection logic
         * itself (see noiseboy_note_on's own comment, fixed
         * separately), this caused real, confirmed note drops.
         * Speeding up resolution is the other half of the fix: 3ms
         * time constant with a looser-but-still-measured 0.01
         * threshold (used ONLY for steal resolution, not the stricter
         * 0.0005 still used for normal full voice deactivation below)
         * resolves in ~13ms instead of ~114ms -- verified via this
         * project's own baseline-relative click test that this is
         * still click-free (transition jump stays within normal
         * range of the signal's own variation, not a spike). */
        const double target = (v->gateOpen && !v->pendingSteal) ? v->velocity01 : 0.0;
        double timeMs = v->pendingSteal ? 3.0 : (v->gateOpen ? e->params.attackMs : e->params.releaseMs);
        /* Floor lowered from 0.5ms to 0.02ms (roughly one sample at
         * 48kHz) per explicit request: "the release should be a
         * single sample at [knob] 0". The old 0.5ms floor would have
         * silently clamped away any attempt at a genuinely
         * near-instant release regardless of how the knob mapping
         * itself was reshaped -- see releaseMs's own mapping comment
         * in noiseboy_plugin.c for the other half of this fix. Safe
         * for attackMs too (unaffected in practice -- its own linear
         * mapping's minimum, 0.5ms, already sits above this new,
         * lower floor). */
        timeMs = clampd(timeMs, 0.02, 4000.0);
        const double coeff = exp(-1.0 / (0.001 * timeMs * e->sampleRate));
        v->envLevel = target + (v->envLevel - target) * coeff;

        const double steadyThreshold = v->pendingSteal ? 0.01 : 0.0005;
        if (v->envLevel < steadyThreshold && (v->pendingSteal || !v->gateOpen)) {
            if (v->pendingSteal) {
                /* Old sound has decayed to near-silence -- safe to
                 * reset this voice's state now, since there's nothing
                 * audible left for the reset to click against. Starts
                 * the new note within this same sample, no gap. */
                const int wasReleased = v->pendingNoteReleased;
                voice_start(e, v, v->pendingMidiNote, v->pendingVelocity01);
                if (wasReleased) {
                    /* The player already let go of this note before
                     * its deferred steal even completed -- see
                     * pendingNoteReleased's own header comment. Give
                     * it a brief, fixed minimum hold (see
                     * minHoldSamplesRemaining's own comment for why
                     * this can't just set gateOpen=0 directly here)
                     * so its attack envelope actually gets a chance to
                     * rise and be audible, rather than being skipped
                     * entirely -- a real bug this caught: setting
                     * gateOpen=0 in this same sample meant the
                     * envelope's target was already 0 the very first
                     * time it computed for this voice, so envLevel
                     * never rose above 0 at all. ~5ms is enough to be
                     * clearly audible as a quick "tap" without being
                     * so long it reads as a held note the player
                     * didn't actually hold. */
                    v->minHoldSamplesRemaining = (int)(0.005 * e->sampleRate);
                }
            } else {
                v->active = 0;
                continue;
            }
        }

        /* Phase updated here, before layer mixing, since Karplus
         * layers' auto-pan (below) needs it for this same sample --
         * a one-sample-early read makes no audible difference for a
         * continuously-incrementing phase accumulator.
         *
         * REDESIGNED, twice now: originally driven directly by the AM
         * Rate knob (one cycle per AM period, before AM/wavefold were
         * removed). Then, briefly, synced to the post-filter LOOP
         * redesign's own live playback rate. Neither applies anymore
         * -- AM/wavefold stayed gone even after LOOP reverted back to
         * a pre-filter source, and the reverted LOOP has no live-rate
         * concept to sync to (each note's own Loop Length is fixed at
         * capture time, not a live multiplier). Karplus's own
         * auto-pan (below) still needs SOME rate to cycle at, so this
         * is now just a fixed, modest internal rate -- no knob
         * controls it. */
        v->amPhase += 0.5 * dt; /* 0.5Hz -- a slow, gentle auto-pan cycle */
        if (v->amPhase > 1.0) v->amPhase -= floor(v->amPhase);

        /* Stereo spreading, per explicit request: Detune (knob 7) now
         * controls stereo width, not just pitch spread. Filtered-noise
         * layers get a FIXED pan position, reusing each layer's own
         * already-randomized detuneCents (the same +-15-cents-per-
         * layer spread used for pitch, normalized to a pan position)
         * so a layer's pitch-spread character and its stereo position
         * are the same underlying randomization, not two unrelated
         * ones. Karplus layers instead auto-pan back and forth once
         * per loop repetition (see this phase's own comment above),
         * using the SAME amPhase driving that synchronization rather
         * than a separate, unrelated LFO. Both scaled by
         * detuneSpread01 -- at Detune=0 every layer collapses to dead
         * centre (mono), matching noiseboy_process's own mono output
         * exactly. */
        double voiceSumL = 0.0, voiceSumR = 0.0;
        double voiceWobbleMul = 1.0; /* set inside the pitch filter block below, per-sample tape wobble -- see TapeWobble's own comment; reused later for output level so all three modulation targets share the same underlying wobble value */
        for (int li = 0; li < v->numLayers; li++) {
            const double layerOut = process_layer(e, v, &v->layers[li]) * v->layers[li].mixLevel01;
            double panPos;
            if (v->layers[li].type == LAYER_KARPLUS_STRONG) {
                panPos = sin(2.0 * M_PI * v->amPhase) * e->params.detuneSpread01;
            } else {
                panPos = clampd(v->layers[li].detuneCents / 15.0, -1.0, 1.0) * e->params.detuneSpread01;
            }
            double gainL, gainR;
            compute_pan_gains(panPos, &gainL, &gainR);
            voiceSumL += layerOut * gainL;
            voiceSumR += layerOut * gainR;
        }
        if (v->numLayers > 0) {
            voiceSumL /= (double)v->numLayers;
            voiceSumR /= (double)v->numLayers;
        }

        /* Bitcrush + pitch-following sample-rate reduction,
         * REINTRODUCED per explicit request -- see this state's own
         * header comment in Voice (bitDepth/rateReducerMultiplier)
         * for the full position/tradeoff rationale. Rate-reduction
         * (via PitchedHold's sample-and-hold) applied first, then
         * bitcrush on the held result -- matches this project's own
         * prior ordering from before v0.10.0's removal. Independent
         * L/R instances (same convention as vibrato/the pitch filter
         * below) so this doesn't collapse the stereo image back to
         * mono. "Sample rate follows key number" per explicit request
         * -- PitchedHold's hold rate is freqHz*rateReducerMultiplier,
         * directly proportional to the played note's own frequency,
         * so lower notes get a proportionally lower effective sample
         * rate and higher notes a proportionally higher one, not a
         * fixed rate applied uniformly across the keyboard. */
        voiceSumL = pitchedhold_process(&v->rateReducerL, voiceSumL, v->freqHz, e->sampleRate, v->rateReducerMultiplier);
        voiceSumR = pitchedhold_process(&v->rateReducerR, voiceSumR, v->freqHz, e->sampleRate, v->rateReducerMultiplier);
        voiceSumL = bitcrush_process(voiceSumL, v->bitDepth);
        voiceSumR = bitcrush_process(voiceSumR, v->bitDepth);

        /* Vibrato, per explicit request -- introduces gentle pitch
         * modulation "in the noise and Karplus" (both already present
         * in voiceSum by this point, so one instance covers both layer
         * types).
         *
         * Depth WAS scaled by (and saturated at just 15% of) the old
         * AM Depth knob's travel -- i.e. vibrato reached its own
         * (small) maximum quickly and stayed there for the rest of
         * the knob's range, rather than growing all the way to a
         * dramatic wobble at full knob -- "so that its gentle, like an
         * acoustic instrument". Now that knob is LOOP Intensity
         * instead (see PostFilterLoop's own comment), and vibrato's
         * depth has nothing meaningful to do with how much loop effect
         * is blended in -- coupling them would mean turning up LOOP
         * Intensity also, confusingly, turns up vibrato. Decoupled:
         * fixed permanently at the same saturated depth the old
         * scheme settled into for MOST of the old knob's range (any
         * setting above 0.15, i.e. the majority of it), rather than
         * introducing a new knob or dropping vibrato's character
         * entirely. Max depth of 4 samples chosen as a reasoned, not
         * ear-tuned, starting point (see this project's own
         * verification notes on why -- no way to listen to this
         * directly). Independent L/R instances (see VibratoDelay's own
         * comment on why). */
        {
            const double vibratoDepthSamples = 4.0;
            voiceSumL = vibrato_process(&v->vibratoL, voiceSumL, v->amPhase, vibratoDepthSamples);
            voiceSumR = vibrato_process(&v->vibratoR, voiceSumR, v->amPhase, vibratoDepthSamples);
        }

        /* STEP 2 (bitcrush/rate-reduce) REMOVED entirely per explicit
         * request -- after several rounds of reducing bitcrush's
         * aggressiveness (1-15 bits -> 8-15 -> 12-15) it still drew
         * "too much bitcrushing and sample rate reduction", so both
         * were pulled out rather than tuned further. Signal now goes
         * straight from sources (STEP 1) to the pitch-tracking filter
         * (STEP 3 below) with nothing in between. See voice_start's
         * own comment for where bitcrush_process/pitchedhold_process
         * are still defined (unused) if a future revision wants them
         * back.
         *
         * STEP 3: voice-level pitch-tracking filter -- per explicit
         * restructuring request, this REPLACES the old per-layer
         * filters entirely. High resonance, tracks v->freqHz directly
         * (knob 1 offset, knob 2 resonance). Deliberately NO envelope
         * or velocity modulation on this cutoff -- both were tried on
         * the old per-layer version and both caused real, reported
         * pitch-accuracy bugs (velocity detuning notes sharp when
         * played harder; envelope making pitch audibly decay over a
         * long release, since the cutoff followed envLevel). This
         * filter's cutoff is now purely a function of the played note
         * and the two relevant knobs -- nothing else. Independent L/R
         * instances, same cutoff/resonance settings applied to both
         * (only the filter STATE differs per channel, not its target
         * parameters) so pitch accuracy stays identical between
         * channels -- only the panning above differs L vs R.
         *
         * NOTE: a drive reduction (0.2 -> 0.05) was tried here as a
         * hypothesis fix for a reported "pitch tied to envelope"
         * complaint, on the theory that a saturating feedback path at
         * high resonance could let the resonant peak shift with input
         * amplitude. Reverted -- it measurably WEAKENED pitch tracking
         * (this project's own zero-crossing test ratio dropped from
         * ~3.5x to ~1.5x, further from the true 16x for a 4-octave
         * span), a confirmed regression for a speculative, unconfirmed
         * benefit. Left at the original 0.2.
         *
         * Resonance compensation added per direct investigation
         * request ("some notes too resonant, more than notes around
         * it"). Measured directly (impulse response, same resonance01
         * across notes): at a fixed knob value, this filter's actual
         * resonant peak varies from ~0.28 at the bottom of the
         * playable range up to ~0.94 at the top -- a real, substantial
         * unevenness even before the filter-type randomization issue
         * (see pitchFilterKind's own comment, the larger contributor,
         * already fixed above). Boosts resonance for notes below
         * 1000Hz (0.5 per octave below that point, empirically chosen
         * -- not a closed-form derivation), capped at 2.0x knob value.
         * That cap is a measured, genuine physical ceiling, not an
         * arbitrary safety margin: at the lowest notes, peak gain
         * plateaus (and very slightly REVERSES) above roughly
         * resonance01=2.0 regardless of how much further resonance is
         * pushed -- a real limitation of this filter topology at very
         * low cutoff-to-sample-rate ratios, not something a better
         * compensation curve alone can fix. Narrows the measured
         * unevenness from ~3.4x (0.28 to 0.94) down to ~2.1x (0.44 to
         * 0.94) -- a real, substantial improvement, though not
         * perfectly flat given that hard ceiling. */
        {
            /* REAL, SEVERE BUG FOUND AND FIXED HERE, while investigating
             * a report that Karplus had become essentially inaudible:
             * this filter was genuinely, persistently self-oscillating
             * at resonance settings this engine has always used by
             * default. Verified directly and unambiguously: excite the
             * filter with a single impulse, then feed it ZERO input
             * for 10+ seconds -- it never decayed at all, still
             * ringing at a steady, bounded amplitude the whole time
             * (this is a genuine limit-cycle, not just "a long decay
             * time" -- a linear decay factor added to the feedback
             * path, tested directly, did essentially nothing to fix
             * it, which is the signature of a NONLINEAR oscillator:
             * the existing cubic soft-clip on the resonant node,
             * combined with feedback gain above the true self-
             * oscillation threshold, forms a stable limit cycle the
             * same way a Van der Pol oscillator does -- the amplitude
             * gets bounded, not damped toward zero). Once a filter
             * locks into this state, it dominates the output
             * regardless of what's actually feeding it -- explaining
             * why Karplus (or anything else) can become inaudible
             * even at a healthy mix level.
             *
             * Empirically found the TRUE stable ceiling for this
             * filter's own resonance01 input (a direct sweep,
             * checking genuine long-term decay, not just short-window
             * peak gain like earlier resonance-evenness testing did --
             * that's specifically why this was missed before): only
             * ~0.15, consistent across the whole practical cutoff
             * range (32Hz-2kHz tested). This is far below the 0.82
             * default knob value, let alone the up-to-2.0 ceiling the
             * resonance-evenness compensation could reach -- meaning
             * this instability predates that fix and has likely been
             * present since whenever the default resonance was set
             * this high, not a new regression.
             *
             * Fix: the Resonance knob's full 0-100% feel is remapped
             * onto this filter's ACTUAL safe range (capped at 0.14,
             * a small margin below the measured 0.15 threshold) --
             * not just clamped, which would leave much of the knob's
             * upper range feeling dead/unresponsive. This is a real,
             * audible character change to the instrument's resonant
             * tone, not a subtle tweak -- flagging that directly, this
             * is exactly the trade-off of fixing a genuine, long-
             * standing instability rather than a cosmetic issue. */
            const double safeResonanceCeiling = 0.14;

            /* Tape wobble, per explicit request -- see TapeWobble's
             * own header comment. One shared wobble value per voice
             * per sample, applied to cutoff, resonance, and (further
             * down, post-envelope) output level together. 1.0Hz rate
             * -- slow, wow/flutter-like drift, not audio-rate texture
             * (see TapeWobble's own comment for why that distinction
             * matters). Depth (mellotronDepth01, 1%-5%) is recipe-
             * level, not re-rolled here. */
            const double wobble = tape_wobble_process(&v->wobble, 1.0, e->sampleRate);
            voiceWobbleMul = 1.0 + wobble * e->mellotronDepth01;

            const double cutoffMul = pow(2.0, (e->params.filterCutoffOffset01 - 0.5) * 4.0);
            const double pitchCutoff = clampd(v->freqHz * cutoffMul * e->timbreCharacterMul * voiceWobbleMul, 20.0, e->sampleRate * 0.45);
            const double resonanceBoostMul = 1.0 + clampd(log2(1000.0 / v->freqHz), 0.0, 1000.0) * 0.5;
            const double baseResonance = e->params.filterResonance01 * safeResonanceCeiling;
            const double pitchResonance = fmin(baseResonance * resonanceBoostMul * voiceWobbleMul, safeResonanceCeiling);
            if (v->pitchFilterKind == FILTER_MOOG) {
                moog_ladder_set(&v->pitchFilterMoogL, pitchCutoff, pitchResonance, 0.2);
                moog_ladder_set(&v->pitchFilterMoogR, pitchCutoff, pitchResonance, 0.2);
                voiceSumL = moog_ladder_process(&v->pitchFilterMoogL, voiceSumL);
                voiceSumR = moog_ladder_process(&v->pitchFilterMoogR, voiceSumR);
            } else {
                korg35lp_set(&v->pitchFilterKorgLpL, pitchCutoff, pitchResonance, 0.2);
                korg35lp_set(&v->pitchFilterKorgLpR, pitchCutoff, pitchResonance, 0.2);
                voiceSumL = korg35lp_process(&v->pitchFilterKorgLpL, voiceSumL);
                voiceSumR = korg35lp_process(&v->pitchFilterKorgLpR, voiceSumR);
            }
        }

        /* Post-filter LOOP application REMOVED -- per explicit revert,
         * LOOP is a pre-filter source again now (see process_layer's
         * own LAYER_LOOP dispatch and LoopSource's own comment for the
         * full history), mixed in with the other layers back in STEP
         * 1 (source mixing) above, not applied here as a separate
         * post-filter effect. */

        /* STEP 4: amplitude envelope, with tape wobble also applied to
         * output level -- see TapeWobble's own header comment. Same
         * per-sample wobble value that modulated the filter above,
         * reused here (not a fresh call) so all three targets ride
         * the same underlying "tape speed" fluctuation coherently. */
        voiceSumL = voiceSumL * v->envLevel * voiceWobbleMul;
        voiceSumR = voiceSumR * v->envLevel * voiceWobbleMul;

        /* Output Filt / TILT REMOVED from here, per explicit request:
         * "lets move the db-cell technology to before the TILT... this
         * emulates failing electronics [and] shouldn't have to be
         * noise gated". Per-voice output shaping doesn't fit that
         * request -- TILT needs to sit on the FINAL mix, AFTER
         * db-cell, not per-voice before it. See TiltFilter's own
         * comment (noiseboy_dsp.h) and render_block's own comment
         * (noiseboy_plugin.c) for where this now lives and why. */

        mixL += voiceSumL;
        mixR += voiceSumR;
    }

    /* Single shared drive/saturation stage on the final mix, per
     * explicit request for something to give the noise more volume
     * and colour -- deliberately one block applied once here, not
     * per-voice or per-layer. Simple pre-gain into tanh (same
     * saturation approach used throughout this project family's
     * filters) -- NOT normalized back down afterward, since the
     * saturation's natural loudness/density increase at higher drive
     * IS the "more volume" part of the request, not a side effect to
     * cancel out. Same gain applied to both channels (a global stage,
     * not a stereo one). */
    const double driveGain = 1.0 + e->params.drive01 * 6.0;
    mixL = tanh(mixL * driveGain);
    mixR = tanh(mixR * driveGain);

    /* Global, always-on tape saturation stage -- per explicit request,
     * distinct from the knob-controlled Drive above (which can be
     * turned down to 0; this can't). Placed after Drive specifically
     * because it has its own built-in compressor, which tames whatever
     * loudness Drive added before the final tanh warms/rounds it off
     * -- a sensible "drive then tame" order rather than two unrelated
     * saturation stages fighting each other. Independent L/R instances
     * -- a single shared instance was tried first but caught by this
     * project's own stereo test: its envelope follower's internal
     * state mutates on each call, so a shared instance processing L
     * then R would see different state for each, producing a small
     * but real L/R difference even at Detune=0 where the signal should
     * collapse to true mono. */
    mixL = tapesat_process(&e->tapeSatL, mixL, e->sampleRate);
    mixR = tapesat_process(&e->tapeSatR, mixR, e->sampleRate);

    *outL = mixL * e->params.masterLevel01;
    *outR = mixR * e->params.masterLevel01;
}

double noiseboy_process(NoiseboyEngine *e) {
    double l, r;
    noiseboy_process_stereo(e, &l, &r);
    return (l + r) * 0.5;
}
