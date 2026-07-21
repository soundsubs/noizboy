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

        /* Detune spread across layers -- first layer stays at 0 cents
         * (an anchor pitch), the rest spread out symmetrically so
         * stacking layers doesn't just shift the whole chord sharp or
         * flat. +-15 cents max range, modest enough to read as
         * "richness" rather than an obvious chorus effect. Only
         * meaningful for Karplus-type layers now (their own pitch);
         * filtered-noise layers no longer have a per-layer filter to
         * detune. */
        r->detuneCents = (i == 0) ? 0.0 : (rand01(&e->rngState) * 2.0 - 1.0) * 15.0;

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

    /* Bitcrush and pitch-following sample-rate reduction REMOVED
     * entirely per explicit request, after several rounds of trying to
     * find a subtle-enough setting still drew "too much bitcrushing
     * and sample rate reduction" -- rather than keep tuning it,
     * removed outright. bitcrush_process/pitchedhold_process remain
     * defined in noiseboy_dsp.c/h (unused) per this project's
     * established "keep superseded options, don't delete" convention,
     * in case a future revision wants them back at a different
     * amount/design. */
    vibrato_init(&v->vibrato);
    moog_ladder_init(&v->outputLowpass, e->sampleRate);
    korg35hp_init(&v->outputHighpass, e->sampleRate);

    /* Voice-level pitch-tracking filter -- randomized fresh per note
     * (Moog/Korg35LP, 50/50; Korg35HP deliberately excluded, same
     * reasoning as before: a highpass tuned to the played pitch works
     * against sounding pitched, not for it), matching this project's
     * established per-note variety pattern now that filter choice is
     * no longer part of the fixed recipe. */
    v->pitchFilterKind = (rand01(&e->rngState) < 0.5) ? FILTER_MOOG : FILTER_KORG_LP;
    moog_ladder_init(&v->pitchFilterMoog, e->sampleRate);
    korg35lp_init(&v->pitchFilterKorgLp, e->sampleRate);

    v->numLayers = e->numRecipeLayers;
    for (int i = 0; i < v->numLayers; i++) {
        Layer *layer = &v->layers[i];
        const LayerRecipe *r = &e->recipe[i];
        layer->type = r->type;
        layer->colour = r->colour;
        layer->detuneCents = r->detuneCents;

        double detuneMul = pow(2.0, (layer->detuneCents * e->params.detuneSpread01) / 1200.0);
        double layerFreq = v->freqHz * detuneMul;

        if (layer->type == LAYER_FILTERED_NOISE) {
            /* Raw source generation only now -- no per-layer filter,
             * no per-layer PitchedHold. See Layer's own header comment
             * for the full restructuring rationale. */
            noisegen_init(&layer->noiseGen, xorshift_next(&e->rngState));
        } else {
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
        double sustainTarget = v->gateOpen ? 0.02 : 0.0;
        const double smoothCoeff = 0.999; /* ~10ms-ish at typical sample rates, gentle enough to remove the discontinuity without noticeably delaying the release character */
        layer->sustainAmountSmoothed = sustainTarget + (layer->sustainAmountSmoothed - sustainTarget) * smoothCoeff;
        return karplus_process(&layer->karplus, sustainFeed, layer->sustainAmountSmoothed);
    }

    return noisegen_process(&layer->noiseGen, layer->colour);
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
         * and the two relevant knobs -- nothing else.
         *
         * NOTE: a drive reduction (0.2 -> 0.05) was tried here as a
         * hypothesis fix for a reported "pitch tied to envelope"
         * complaint, on the theory that a saturating feedback path at
         * high resonance could let the resonant peak shift with input
         * amplitude. Reverted -- it measurably WEAKENED pitch tracking
         * (this project's own zero-crossing test ratio dropped from
         * ~3.5x to ~1.5x, further from the true 16x for a 4-octave
         * span), a confirmed regression for a speculative, unconfirmed
         * benefit. Left at the original 0.2. */
        {
            const double cutoffMul = pow(2.0, (e->params.filterCutoffOffset01 - 0.5) * 4.0);
            const double pitchCutoff = clampd(v->freqHz * cutoffMul, 20.0, e->sampleRate * 0.45);
            const double pitchResonance = e->params.filterResonance01;
            if (v->pitchFilterKind == FILTER_MOOG) {
                moog_ladder_set(&v->pitchFilterMoog, pitchCutoff, pitchResonance, 0.2);
                voiceSum = moog_ladder_process(&v->pitchFilterMoog, voiceSum);
            } else {
                korg35lp_set(&v->pitchFilterKorgLp, pitchCutoff, pitchResonance, 0.2);
                voiceSum = korg35lp_process(&v->pitchFilterKorgLp, voiceSum);
            }
        }

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

        /* STEP 4: amplitude envelope. */
        voiceSum = voiceSum * v->envLevel * amGain;

        /* STEP 5: OUTPUT FILT -- the final stage. Rebuilt as a TILT
         * filter, per direct request. Knob centred (12 o'clock, 64) =
         * completely bypassed, full signal through. Turning left
         * engages a lowpass whose cutoff falls (log scale, 20kHz down
         * to 20Hz) the further left you go, silencing the signal from
         * the top down. Turning right engages a highpass whose cutoff
         * rises (log scale, 20Hz up to 20kHz) the further right you
         * go, silencing the signal from the bottom up -- "make it
         * disappear either direction". Only ONE side is ever active at
         * a time (a true tilt, not two filters stacked) -- below
         * centre uses the lowpass exclusively, above centre uses the
         * highpass exclusively. No velocity or envelope influence, no
         * resonance -- purely a knob-controlled sweep, deliberately
         * simple and predictable given this replaces a previous
         * design that wasn't landing as an audible, useful control. */
        {
            const double knob = e->params.outputFilterFreq01;
            if (knob <= 0.5) {
                const double t = clampd((0.5 - knob) / 0.5, 0.0, 1.0);
                const double lpCutoff = clampd(20000.0 * pow(20.0 / 20000.0, t), 20.0, e->sampleRate * 0.45);
                moog_ladder_set(&v->outputLowpass, lpCutoff, 0.0, 0.1);
                voiceSum = moog_ladder_process(&v->outputLowpass, voiceSum);
                /* Same supplemental fade as the highpass side, for a
                 * symmetric guarantee of true silence at both extremes
                 * rather than relying solely on each filter's own
                 * natural rolloff (which, like the highpass side,
                 * still leaves some audible signal through even at its
                 * most extreme setting). */
                voiceSum *= (1.0 - t * t);
            } else {
                const double t = clampd((knob - 0.5) / 0.5, 0.0, 1.0);
                const double hpCutoff = clampd(20.0 * pow(20000.0 / 20.0, t), 20.0, e->sampleRate * 0.45);
                korg35hp_set(&v->outputHighpass, hpCutoff, 0.0, 0.1);
                voiceSum = korg35hp_process(&v->outputHighpass, voiceSum);
                /* Supplemental gain fade, highpass side only -- verified
                 * directly (isolated filter test against white noise)
                 * that Korg35HP's own attenuation plateaus around ~29%
                 * RMS remaining no matter how close the cutoff gets to
                 * Nyquist, a structural limit of this filter topology
                 * at extreme cutoff ratios, not something tunable away
                 * by pushing the cutoff further. The lowpass side needs
                 * no such help -- it naturally reaches near-total
                 * silence on its own. Quadratic fade (gentle at first,
                 * accelerating toward full silence exactly at t=1) so
                 * full-right guarantees "disappear" as explicitly
                 * requested, while the filter's own sweep character
                 * still dominates through most of the range. */
                voiceSum *= (1.0 - t * t);
            }
        }

        mix += voiceSum;
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
