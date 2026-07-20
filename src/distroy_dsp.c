#include "distroy_dsp.h"
#include <math.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------
 * Pedal type metadata.
 *
 * KNOB MODE DESIGN NOTE: the project spec says knobs default to a
 * "wet/dry amount" parameter, with "some distortions" using Gain
 * instead. I've assigned WET_DRY to the pedals that are commonly used
 * as blended boosts/texture in modern pedalboards (Boss OD, Tubescreamer,
 * Sansamp, Rat, Geiger Counter), and GAIN to the classic "always fully
 * driven" pedals where drive amount itself is the defining character
 * (Fuzz, Metal Zone, Big Muff). This split wasn't fully specified in the
 * request, so it's my interpretation -- easy to rebalance later if a
 * different split is wanted.
 * ------------------------------------------------------------------- */
static const DistroyTypeInfo kTypeInfo[DISTROY_TYPE_COUNT] = {
    [DISTROY_BOSS_OD]      = { "Boss OD",       "OD",     DISTROY_KNOB_WET_DRY },
    [DISTROY_FUZZ]         = { "Fuzz",          "FUZZ",   DISTROY_KNOB_WET_DRY },
    [DISTROY_METAL]        = { "Metal",         "METAL",  DISTROY_KNOB_WET_DRY },
    [DISTROY_TUBESCREAMER] = { "Tubescreamer",  "TS9",    DISTROY_KNOB_WET_DRY },
    [DISTROY_BIG_MUFF]     = { "Big Muff",      "MUFF",   DISTROY_KNOB_WET_DRY },
    [DISTROY_SANSAMP]      = { "Sansamp",       "SANS",   DISTROY_KNOB_WET_DRY },
    [DISTROY_RAT]          = { "Rat",           "RAT",    DISTROY_KNOB_WET_DRY },
    [DISTROY_GEIGER_COUNTER] = { "Geiger Counter", "GEIGER", DISTROY_KNOB_WET_DRY },
    [DISTROY_MOOG_LADDER]  = { "Moog Ladder",   "MOOG",   DISTROY_KNOB_CUTOFF },
    [DISTROY_KORG_MS20]    = { "Korg MS-20",    "MS20",   DISTROY_KNOB_CUTOFF },
    [DISTROY_MUTRON]       = { "Mu-Tron",       "MUTRON", DISTROY_KNOB_SENS },
    [DISTROY_CRYBABY]      = { "Cry Baby 535Q", "CRYB",   DISTROY_KNOB_SENS },
    [DISTROY_JENSEN]       = { "Jensen",        "JENSEN", DISTROY_KNOB_WET_DRY },
    [DISTROY_LUNDAHL]      = { "Lundahl",       "LUND",   DISTROY_KNOB_WET_DRY },
    [DISTROY_LOFI]         = { "LoFi",          "LOFI",   DISTROY_KNOB_RATE },
    [DISTROY_FZ1W]         = { "Boss FZ-1W",    "FZ1W",   DISTROY_KNOB_WET_DRY },
    [DISTROY_CLIP]         = { "Clip",          "CLIP",   DISTROY_KNOB_WET_DRY },
    [DISTROY_REKT]         = { "Rekt",          "REKT",   DISTROY_KNOB_WET_DRY },
    [DISTROY_WHAM]         = { "Whammy",        "WHAMMY", DISTROY_KNOB_WET_DRY },
    [DISTROY_TAPE]         = { "Tape",          "TAPE",   DISTROY_KNOB_WET_DRY },
    [DISTROY_SPKR]         = { "Speaker",       "SPKR",   DISTROY_KNOB_SIZE },
    [DISTROY_NOIZ]         = { "Noiz",          "NOIZ",   DISTROY_KNOB_WET_DRY },
    [DISTROY_TUBE]         = { "Tube",          "TUBE",   DISTROY_KNOB_WET_DRY },
    [DISTROY_CABL]         = { "Cable Fault",   "CABL",   DISTROY_KNOB_WET_DRY },
    [DISTROY_SEM]          = { "Oberheim SEM",  "SEM",    DISTROY_KNOB_CUTOFF },
    [DISTROY_POLIVOKS]     = { "Polivoks",      "POLI",   DISTROY_KNOB_CUTOFF },
    [DISTROY_OCTAVE]       = { "Octafuzz",      "OCT",    DISTROY_KNOB_WET_DRY },
    [DISTROY_BASS_MUFF]    = { "Bass Big Muff", "BMUFF",  DISTROY_KNOB_WET_DRY },
    [DISTROY_MXR_BASS]     = { "MXR Bass Dist", "MXRB",   DISTROY_KNOB_WET_DRY },
    [DISTROY_BOSS_ODB3]    = { "Boss ODB-3",    "ODB3",   DISTROY_KNOB_WET_DRY },
    [DISTROY_BASS_EQ]      = { "Low EQ Boost",  "LOEQ",   DISTROY_KNOB_GAIN },
};

const DistroyTypeInfo* distroy_type_info(DistroyType type) {
    if (type < 0 || type >= DISTROY_TYPE_COUNT) return &kTypeInfo[0];
    return &kTypeInfo[type];
}

const char* distroy_knob_mode_label(DistroyKnobMode mode) {
    switch (mode) {
        case DISTROY_KNOB_GAIN: return "GAIN";
        case DISTROY_KNOB_CUTOFF: return "CUTOFF";
        case DISTROY_KNOB_SENS: return "SENS";
        case DISTROY_KNOB_SIZE: return "SIZE";
        case DISTROY_KNOB_RATE: return "RATE";
        default: return "MIX";
    }
}

/* Forward declaration -- defined below in the waveshaping primitives
 * section, but needed earlier by the Moog/Korg35 filter setters. */
static double clampd(double x, double lo, double hi);

/* ---------------------------------------------------------------------
 * Filter helpers
 * ------------------------------------------------------------------- */

void onepole_set_lowpass(OnePole *f, double cutoff_hz, double sample_rate) {
    double x = exp(-2.0 * M_PI * cutoff_hz / sample_rate);
    f->b0 = 1.0 - x;
    f->b1 = 0.0;
    f->a1 = -x;
}

void onepole_set_highpass(OnePole *f, double cutoff_hz, double sample_rate) {
    double x = exp(-2.0 * M_PI * cutoff_hz / sample_rate);
    f->b0 = (1.0 + x) / 2.0;
    f->b1 = -(1.0 + x) / 2.0;
    f->a1 = -x;
}

double onepole_process(OnePole *f, double x) {
    double y = f->b0 * x + f->b1 * f->x1 - f->a1 * f->y1;
    f->x1 = x;
    f->y1 = y;
    return y;
}

void zcsmoother_init(ZeroCrossingSmoother *z, double sample_rate) {
    z->prevSample = 0.0;
    z->zcRate = 0.0;
    z->slewedSample = 0.0;
    z->maxSlewMs = 4.0;
    onepole_set_lowpass(&z->smoothFilter, 12000.0, sample_rate);
}

void zcsmoother_set_max_slew_ms(ZeroCrossingSmoother *z, double maxMs) {
    if (maxMs < 0.0) maxMs = 0.0;
    if (maxMs > 20.0) maxMs = 20.0;
    z->maxSlewMs = maxMs;
}

double zcsmoother_process(ZeroCrossingSmoother *z, double x, double sample_rate) {
    /* Detect a zero crossing (sign change since the last sample). */
    int crossed = ((x >= 0.0) != (z->prevSample >= 0.0)) ? 1 : 0;
    z->prevSample = x;

    /* Smooth the crossing indicator into a running rate estimate via a
     * simple leaky integrator (~30ms time constant) -- avoids the
     * cutoff frequency (and slew time below) jumping around on a
     * per-sample basis, which would itself sound like an artifact. */
    double rateCoeff = exp(-1.0 / (0.001 * 30.0 * sample_rate));
    z->zcRate = z->zcRate * rateCoeff + (double)crossed * (1.0 - rateCoeff);

    /* Stage 1: slew-rate limiting (amplitude domain) -- caps how fast
     * the signal is allowed to change per sample. Time constant scales
     * 0 to ~4ms with the same zero-crossing rate driving the lowpass
     * below (harsher signal = more limiting), expressed as "time to
     * cross the full -1..+1 range at the current maximum slew rate".
     * At zcRate=0 this is a no-op (maxDelta effectively unlimited). */
    double slewTimeMs = z->zcRate * z->maxSlewMs;
    if (slewTimeMs > 1e-6) {
        double maxDeltaPerSample = 2.0 / (slewTimeMs * 0.001 * sample_rate);
        double delta = x - z->slewedSample;
        if (delta > maxDeltaPerSample) delta = maxDeltaPerSample;
        if (delta < -maxDeltaPerSample) delta = -maxDeltaPerSample;
        z->slewedSample += delta;
    } else {
        z->slewedSample = x;
    }

    /* Stage 2: the existing gentle dynamic lowpass (frequency domain),
     * now fed the slew-limited signal rather than the raw input -- a
     * harsher/buzzier signal (higher rate) gets pulled down toward more
     * smoothing on both stages; an already-smooth signal is left mostly
     * untouched by either. Range chosen to be a genuinely gentle
     * "smooth it a little" effect per spec, not an aggressive
     * tone-shaping filter. */
    double cutoff = 12000.0 - z->zcRate * 8000.0;
    if (cutoff < 3000.0) cutoff = 3000.0;
    onepole_set_lowpass(&z->smoothFilter, cutoff, sample_rate);
    return onepole_process(&z->smoothFilter, z->slewedSample);
}

void biquad_set_peaking(Biquad *f, double freq_hz, double q, double gain_db, double sample_rate) {
    double A = pow(10.0, gain_db / 40.0);
    double w0 = 2.0 * M_PI * freq_hz / sample_rate;
    double alpha = sin(w0) / (2.0 * q);
    double cosw0 = cos(w0);

    double b0 = 1.0 + alpha * A;
    double b1 = -2.0 * cosw0;
    double b2 = 1.0 - alpha * A;
    double a0 = 1.0 + alpha / A;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha / A;

    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
}

void biquad_set_lowshelf(Biquad *f, double freq_hz, double gain_db, double sample_rate) {
    double A = pow(10.0, gain_db / 40.0);
    double w0 = 2.0 * M_PI * freq_hz / sample_rate;
    double cosw0 = cos(w0);
    double sinw0 = sin(w0);
    double S = 1.0; /* shelf slope -- 1.0 is the standard gentle/musical default */
    double alpha = sinw0 / 2.0 * sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
    double twoSqrtAalpha = 2.0 * sqrt(A) * alpha;

    double b0 = A * ((A + 1.0) - (A - 1.0) * cosw0 + twoSqrtAalpha);
    double b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
    double b2 = A * ((A + 1.0) - (A - 1.0) * cosw0 - twoSqrtAalpha);
    double a0 = (A + 1.0) + (A - 1.0) * cosw0 + twoSqrtAalpha;
    double a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
    double a2 = (A + 1.0) + (A - 1.0) * cosw0 - twoSqrtAalpha;

    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
}

double biquad_process(Biquad *f, double x) {
    double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}

void biquad_set_bandpass(Biquad *f, double freq_hz, double q, double sample_rate) {
    double w0 = 2.0 * M_PI * freq_hz / sample_rate;
    double alpha = sin(w0) / (2.0 * q);
    double cosw0 = cos(w0);

    double b0 = alpha;
    double b1 = 0.0;
    double b2 = -alpha;
    double a0 = 1.0 + alpha;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha;

    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
}

void tilteq_init(TiltEQ *t, double center_hz, double sample_rate) {
    t->tone = 0.5;
    onepole_set_lowpass(&t->lowshelf, center_hz, sample_rate);
    onepole_set_highpass(&t->highshelf, center_hz, sample_rate);
}

double tilteq_process(TiltEQ *t, double x) {
    /* Classic tilt EQ: sum the low-passed and high-passed signal with
     * complementary gains that shift with "tone". At tone=0.5 both
     * gains are 1.0 (flat). Range chosen to be characterful without
     * being extreme (+-4dB-ish). */
    double lo = onepole_process(&t->lowshelf, x);
    double hi = onepole_process(&t->highshelf, x);
    double tilt = (t->tone - 0.5) * 2.0; /* -1.0 .. 1.0 */
    double lo_gain = 1.0 - tilt * 0.6;
    double hi_gain = 1.0 + tilt * 0.6;
    return lo * lo_gain + hi * hi_gain;
}

/* ---------------------------------------------------------------------
 * Moog ladder filter -- Stilson/Smith discrete approximation.
 * ------------------------------------------------------------------- */

void moog_ladder_init(MoogLadder *f, double sample_rate) {
    *f = (MoogLadder){0};
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
    /* resonance01 0-1 -> scaled so it gets characterful without
     * crossing the classic self-oscillation threshold (~4.0 for this
     * formula) -- 3.5 leaves headroom given the cubic soft-clip alone
     * isn't a strong enough damper right at the boundary (verified via
     * make test: 4.2 diverged to inf/-nan at cutoff=8kHz, resonance=0.5
     * default -- see also the hard clamp below as a second safety net). */
    f->resonance = resonance01 * 3.5 * (t2 + 6.0 * t1) / (t2 - 6.0 * t1);
    f->drive = 1.0 + drive01 * 11.0; /* 1x - 12x input pre-gain */
}

double moog_ladder_process(MoogLadder *f, double x) {
    x *= f->drive;
    x = tanh(x); /* input saturation stage -- the explicit "Drive" character */

    double input = x - f->resonance * f->stage[3];
    f->stage[0] = input * f->p + f->delay[0] * f->p - f->k * f->stage[0];
    f->stage[1] = f->stage[0] * f->p + f->delay[1] * f->p - f->k * f->stage[1];
    f->stage[2] = f->stage[1] * f->p + f->delay[2] * f->p - f->k * f->stage[2];
    f->stage[3] = f->stage[2] * f->p + f->delay[3] * f->p - f->k * f->stage[3];
    /* cubic soft-clip on the resonant node -- models the ladder's own
     * inherent saturation, prevents runaway self-oscillation blowup */
    f->stage[3] -= (f->stage[3] * f->stage[3] * f->stage[3]) / 6.0;

    /* Hard safety clamp: the cubic term above is a soft damper, not a
     * hard limit -- near the resonance/cutoff combination that
     * approaches self-oscillation it can still diverge over many
     * samples. This is a standard second safety net in production
     * ladder filter implementations; the clamp range is well outside
     * normal operating levels so it doesn't audibly affect typical use. */
    for (int i = 0; i < 4; i++) {
        f->stage[i] = clampd(f->stage[i], -8.0, 8.0);
    }

    f->delay[0] = input;
    f->delay[1] = f->stage[0];
    f->delay[2] = f->stage[1];
    f->delay[3] = f->stage[2];

    return f->stage[3];
}

/* ---------------------------------------------------------------------
 * Korg-style resonant filter pair (MS-20 character)
 * ------------------------------------------------------------------- */

void korg35lp_init(Korg35LP *f, double sample_rate) {
    *f = (Korg35LP){0};
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
    /* Only 2 poles instead of 4 -- self-oscillation threshold is
     * different from the Moog ladder. Same conservative headroom
     * reasoning as MoogLadder (see its comment) applied here too. */
    f->resonance = resonance01 * 2.6 * (t2 + 6.0 * t1) / (t2 - 6.0 * t1);
    f->drive = 1.0 + drive01 * 9.0;
}

double korg35lp_process(Korg35LP *f, double x) {
    x *= f->drive;
    x = tanh(x);

    double input = x - f->resonance * f->stage[1];
    f->stage[0] = input * f->p + f->delay[0] * f->p - f->k * f->stage[0];
    f->stage[1] = f->stage[0] * f->p + f->delay[1] * f->p - f->k * f->stage[1];
    /* saturate the resonant node -- this is the "distorts on its own"
     * self-saturating character the MS-20 is known for */
    f->stage[1] -= (f->stage[1] * f->stage[1] * f->stage[1]) / 6.0;

    /* Hard safety clamp -- same reasoning as MoogLadder's, second
     * safety net beyond the soft cubic damper. */
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
    /* Resonant highpass derived as input minus its own independently
     * resonant/saturating lowpass core -- see header comment for why
     * this isn't a literal transcription of the real analog HPF. */
    double lp = korg35lp_process(&f->core, x);
    return x - lp;
}

/* ---------------------------------------------------------------------
 * Envelope follower (auto-wah)
 * ------------------------------------------------------------------- */

void envfollow_init(EnvelopeFollower *e, double attack_ms, double release_ms, double sample_rate) {
    e->envelope = 0.0;
    e->attack_coeff = exp(-1.0 / (0.001 * attack_ms * sample_rate));
    e->release_coeff = exp(-1.0 / (0.001 * release_ms * sample_rate));
}

double envfollow_process(EnvelopeFollower *e, double x) {
    double rectified = fabs(x);
    double coeff = (rectified > e->envelope) ? e->attack_coeff : e->release_coeff;
    e->envelope = coeff * e->envelope + (1.0 - coeff) * rectified;
    return e->envelope;
}

/* ---------------------------------------------------------------------
 * Pitch shifter (WHAM) -- see header comment for the algorithm summary.
 * ------------------------------------------------------------------- */

void pitchshift_init(PitchShifter *ps) {
    for (int i = 0; i < PITCHSHIFT_BUFFER_SIZE; i++) ps->buffer[i] = 0.0;
    ps->write_pos = 0;
    ps->read_offset1 = 0.0;
    ps->read_offset2 = PITCHSHIFT_WINDOW * 0.5; /* permanently half a window apart */
    ps->ratio = 1.0;
}

void pitchshift_set_semitones(PitchShifter *ps, double semitones) {
    ps->ratio = pow(2.0, semitones / 12.0);
}

static double pitchshift_read_interp(PitchShifter *ps, double pos) {
    double floor_pos = floor(pos);
    int i0 = (int)floor_pos;
    double frac = pos - floor_pos;
    int idx0 = ((i0 % PITCHSHIFT_BUFFER_SIZE) + PITCHSHIFT_BUFFER_SIZE) % PITCHSHIFT_BUFFER_SIZE;
    int idx1 = (idx0 + 1) % PITCHSHIFT_BUFFER_SIZE;
    return ps->buffer[idx0] * (1.0 - frac) + ps->buffer[idx1] * frac;
}

double pitchshift_process(PitchShifter *ps, double x) {
    ps->buffer[ps->write_pos] = x;

    /* Advance both taps relative to the write head by (1 - ratio) per
     * sample -- ratio>1 (pitch up) makes the read heads fall behind
     * more slowly (read faster through history); ratio<1 (pitch down)
     * makes them fall behind faster (read slower through history). */
    ps->read_offset1 += (1.0 - ps->ratio);
    ps->read_offset2 += (1.0 - ps->ratio);

    if (ps->read_offset1 < 0.0) ps->read_offset1 += PITCHSHIFT_WINDOW;
    if (ps->read_offset1 >= PITCHSHIFT_WINDOW) ps->read_offset1 -= PITCHSHIFT_WINDOW;
    if (ps->read_offset2 < 0.0) ps->read_offset2 += PITCHSHIFT_WINDOW;
    if (ps->read_offset2 >= PITCHSHIFT_WINDOW) ps->read_offset2 -= PITCHSHIFT_WINDOW;

    double pos1 = (double)ps->write_pos - ps->read_offset1;
    double pos2 = (double)ps->write_pos - ps->read_offset2;

    double s1 = pitchshift_read_interp(ps, pos1);
    double s2 = pitchshift_read_interp(ps, pos2);

    /* Triangular crossfade: each tap's gain peaks at the center of its
     * window and fades to 0 at the edges, where the OTHER tap is at
     * its own peak -- classic complementary 2-tap granular crossfade. */
    double w1 = 1.0 - fabs((ps->read_offset1 / PITCHSHIFT_WINDOW) * 2.0 - 1.0);
    double w2 = 1.0 - fabs((ps->read_offset2 / PITCHSHIFT_WINDOW) * 2.0 - 1.0);

    double out = s1 * w1 + s2 * w2;

    ps->write_pos = (ps->write_pos + 1) % PITCHSHIFT_BUFFER_SIZE;
    return out;
}

/* ---------------------------------------------------------------------
 * Decimator + bit quantizer (LOFI)
 * ------------------------------------------------------------------- */

void decimator_init(SimpleDecimator *d) {
    d->held_value = 0.0;
    d->phase = 0.0;
}

double decimator_process(SimpleDecimator *d, double x, double target_hz, double host_sample_rate) {
    d->phase += target_hz / host_sample_rate;
    if (d->phase >= 1.0) {
        d->phase -= 1.0;
        d->held_value = x;
    }
    return d->held_value;
}

double quantize_bits(double x, int bits) {
    if (bits >= 16) return x; /* not reachable given LOFI caps at 15, safety net */
    double levels = pow(2.0, (double)bits - 1); /* signed range, symmetric around 0 */
    if (levels < 1.0) levels = 1.0;
    return round(x * levels) / levels;
}

/* ---------------------------------------------------------------------
 * Noise generator (TAPE hiss)
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

/* ---------------------------------------------------------------------
 * Three-colour noise generator (NOIZ)
 * ------------------------------------------------------------------- */

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
            /* Paul Kellet's well-known "economy" pink noise filter --
             * a standard cheap 3-pole approximation reused widely for
             * exactly this purpose, not a theoretically exact -3dB/oct
             * filter. */
            n->pink_b0 = 0.99765 * n->pink_b0 + white * 0.0990460;
            n->pink_b1 = 0.96300 * n->pink_b1 + white * 0.2965164;
            n->pink_b2 = 0.57000 * n->pink_b2 + white * 1.0526913;
            double pink = n->pink_b0 + n->pink_b1 + n->pink_b2 + white * 0.1848;
            return pink * 0.11; /* rescale back toward -1..1 range */
        }
        case NOISE_RED: {
            /* Simple leaky-integrator brown/red noise -- heavily
             * lowpassed white noise, rescaled up since integration
             * attenuates amplitude a lot. */
            n->brown_state = n->brown_state * 0.98 + white * 0.02;
            return n->brown_state * 3.5;
        }
        case NOISE_WHITE:
        default:
            return white;
    }
}

/* ---------------------------------------------------------------------
 * Broken 1/4" cable/jack fault simulation (CABL)
 * ------------------------------------------------------------------- */

static unsigned int cablesim_rand_seeded(unsigned int *state) {
    unsigned int s = *state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *state = s;
    return s;
}

void cablesim_init(CableSim *c, unsigned int seed) {
    c->state = CABLE_NORMAL;
    c->stateSamplesRemaining = 0;
    c->eventCountdown = 4410; /* ~0.1s before the first possible event */
    c->cutoutLevel = 0.1;
    c->humPhase = 0.0;
    noise_init(&c->noise, seed);
    c->rngState = seed != 0 ? seed : 1;
    /* 60Hz mains hum, randomized once per load, 0-10% -- simulates a
     * ground loop/electrical interference a real bad cable can pick up. */
    c->humLevel = (double)(cablesim_rand_seeded(&c->rngState) % 1000) / 1000.0 * 0.10;
}

static unsigned int cablesim_rand(CableSim *c) {
    return cablesim_rand_seeded(&c->rngState);
}

double cablesim_process(CableSim *c, double x, double severity01, double sample_rate) {
    double sev = clampd(severity01, 0.0, 1.0);

    if (c->state == CABLE_NORMAL) {
        c->eventCountdown--;
        if (c->eventCountdown <= 0) {
            unsigned int r = cablesim_rand(c);
            double roll = (double)(r % 1000) / 1000.0;
            if (roll < 0.55) {
                c->state = CABLE_CRACKLE;
                /* 5-35ms crackle burst */
                double durSec = 0.005 + (double)((r >> 8) % 100) / 100.0 * 0.03;
                c->stateSamplesRemaining = (int)(sample_rate * durSec);
            } else {
                c->state = CABLE_CUTOUT;
                /* IMPORTANT: never fully silent -- 5-20% of signal
                 * always survives a "cutout", matching the spec's
                 * "never fully off" requirement. */
                c->cutoutLevel = 0.05 + (double)((r >> 16) % 100) / 100.0 * 0.15;
                double durSec = 0.01 + (double)((r >> 4) % 150) / 150.0 * 0.08;
                c->stateSamplesRemaining = (int)(sample_rate * durSec);
            }
            /* Next event timing scales with severity -- higher severity
             * (knob turned up) means more frequent glitches. Range is
             * wide either way (a "barely broken" cable still glitches
             * occasionally; a "very broken" one glitches often). */
            double minGap = 0.08 + (1.0 - sev) * 0.4;   /* 0.08-0.48s */
            double maxGap = 0.3 + (1.0 - sev) * 2.2;     /* 0.3-2.5s */
            double gapSec = minGap + (double)(cablesim_rand(c) % 1000) / 1000.0 * (maxGap - minGap);
            c->eventCountdown = (int)(sample_rate * gapSec);
        }
    } else {
        c->stateSamplesRemaining--;
        if (c->stateSamplesRemaining <= 0) {
            c->state = CABLE_NORMAL;
        }
    }

    double out;
    switch (c->state) {
        case CABLE_CRACKLE:
            out = x + noise_next(&c->noise) * (0.3 + sev * 0.4);
            break;
        case CABLE_CUTOUT:
            out = x * c->cutoutLevel + noise_next(&c->noise) * 0.05;
            break;
        case CABLE_NORMAL:
        default:
            /* Subtle baseline crackle even when "working" -- a
             * genuinely bad cable has some character even between
             * glitch events. */
            out = x + noise_next(&c->noise) * (0.003 + sev * 0.01);
            break;
    }

    /* 60Hz mains hum -- randomized level (0-10%) set once at load,
     * present continuously regardless of glitch state (a real ground
     * loop hums all the time, not just during crackles/cutouts). */
    c->humPhase += 2.0 * M_PI * 60.0 / sample_rate;
    if (c->humPhase > 2.0 * M_PI) c->humPhase -= 2.0 * M_PI;
    out += sin(c->humPhase) * c->humLevel;

    return out;
}

/* ---------------------------------------------------------------------
 * Oberheim SEM-style state-variable filter
 * ------------------------------------------------------------------- */

void semfilter_init(SEMFilter *f) {
    f->low = 0.0;
    f->band = 0.0;
    f->f = 0.3;
    f->q = 1.0;
}

void semfilter_set(SEMFilter *f, double cutoff_hz, double resonance01, double sample_rate) {
    double cutoff = clampd(cutoff_hz, 20.0, sample_rate * 0.45);
    f->f = 2.0 * sin(M_PI * cutoff / sample_rate);
    /* q_min kept safely above 0 -- close to self-oscillation at 100%
     * resonance but deliberately not reaching it ("doesn't fully
     * resonate" per spec), unlike the Moog ladder/Korg35 above. */
    const double q_max = 1.2; /* resonance01=0 -> heavily damped */
    const double q_min = 0.18; /* resonance01=1 -> peaky but controlled */
    f->q = q_max - resonance01 * (q_max - q_min);
}

double semfilter_process(SEMFilter *f, double x) {
    double high = x - f->low - f->q * f->band;
    f->band += f->f * high;
    f->low += f->f * f->band;
    /* Safety clamp -- same belt-and-suspenders reasoning as the Moog
     * ladder/Korg35 filters, even though this topology is inherently
     * better-behaved. */
    f->band = clampd(f->band, -8.0, 8.0);
    f->low = clampd(f->low, -8.0, 8.0);
    return f->low;
}

/* ---------------------------------------------------------------------
 * Polivoks-style growly resonant filter
 * ------------------------------------------------------------------- */

void polivoks_init(PolivoksFilter *f) {
    f->stage[0] = 0.0; f->stage[1] = 0.0;
    f->delay[0] = 0.0; f->delay[1] = 0.0;
    f->p = 0.3; f->k = 0.0;
    f->resonance = 0.0;
    f->drive = 1.0;
}

void polivoks_set(PolivoksFilter *f, double cutoff_hz, double resonance01, double drive01, double sample_rate) {
    double fc = clampd(cutoff_hz / (sample_rate * 0.5), 0.0001, 0.99);
    f->p = fc * (1.8 - 0.8 * fc);
    f->k = 2.0 * sin(fc * M_PI * 0.5) - 1.0;
    double t1 = (1.0 - f->p) * 1.386249;
    double t2 = 12.0 + t1 * t1;
    f->resonance = resonance01 * 3.2 * (t2 + 6.0 * t1) / (t2 - 6.0 * t1);
    f->drive = 1.0 + drive01 * 6.0;
}

double polivoks_process(PolivoksFilter *f, double x) {
    x *= f->drive;
    x = tanh(x * 1.5); /* pre-saturation stage */

    double input = x - f->resonance * f->stage[1];
    f->stage[0] = input * f->p + f->delay[0] * f->p - f->k * f->stage[0];
    f->stage[1] = f->stage[0] * f->p + f->delay[1] * f->p - f->k * f->stage[1];

    /* HARD clip (not the cubic soft-clip the Korg35 uses) on the
     * resonant node -- this is what gives Polivoks its distinctly
     * gritty/"growly" heavily distorted character even at moderate
     * resonance, rather than a cleaner resonant peak. */
    f->stage[1] = clampd(f->stage[1] * 1.3, -1.0, 1.0);

    f->delay[0] = input;
    f->delay[1] = f->stage[0];

    return f->stage[1];
}

/* ---------------------------------------------------------------------
 * Brickwall limiter (stereo-linked, look-ahead) -- see header comment.
 * ------------------------------------------------------------------- */

void brickwall_limiter_init(BrickwallLimiter *lim, double ceiling_db, double release_ms, double sample_rate) {
    for (int i = 0; i < LIMITER_LOOKAHEAD_SAMPLES; i++) {
        lim->delay_l[i] = 0.0;
        lim->delay_r[i] = 0.0;
        lim->peak_window[i] = 0.0;
    }
    lim->write_pos = 0;
    lim->gain = 1.0;
    lim->ceiling = pow(10.0, ceiling_db / 20.0);
    lim->release_coeff = exp(-1.0 / (0.001 * release_ms * sample_rate));
}

void brickwall_limiter_process(BrickwallLimiter *lim, double *l, double *r) {
    double in_l = *l;
    double in_r = *r;
    double peak_now = fabs(in_l);
    if (fabs(in_r) > peak_now) peak_now = fabs(in_r);

    lim->delay_l[lim->write_pos] = in_l;
    lim->delay_r[lim->write_pos] = in_r;
    lim->peak_window[lim->write_pos] = peak_now;

    /* True-peak-ish lookahead: scan the WHOLE window for its max, not
     * just the instantaneous sample, so a sharp transient anywhere in
     * the next ~2.9ms is already accounted for before it reaches the
     * output. O(128) per sample is trivial next to the filter chains
     * already running per-sample elsewhere in this project. */
    double max_peak = 0.0;
    for (int i = 0; i < LIMITER_LOOKAHEAD_SAMPLES; i++) {
        if (lim->peak_window[i] > max_peak) max_peak = lim->peak_window[i];
    }

    double target_gain = (max_peak > lim->ceiling) ? (lim->ceiling / max_peak) : 1.0;

    if (target_gain < lim->gain) {
        /* Instant attack -- safe specifically because the lookahead
         * already "saw this peak coming" before it reaches the output
         * (the delayed sample it'll be applied to hasn't been output
         * yet). A no-lookahead limiter couldn't safely do this. */
        lim->gain = target_gain;
    } else {
        lim->gain = lim->release_coeff * lim->gain + (1.0 - lim->release_coeff) * target_gain;
    }

    int read_pos = (lim->write_pos + 1) % LIMITER_LOOKAHEAD_SAMPLES;
    double out_l = lim->delay_l[read_pos] * lim->gain;
    double out_r = lim->delay_r[read_pos] * lim->gain;

    lim->write_pos = (lim->write_pos + 1) % LIMITER_LOOKAHEAD_SAMPLES;

    /* Final hard clamp -- the actual safety guarantee regardless of
     * anything above. This is what makes it a real "brickwall": output
     * NEVER exceeds ceiling, full stop, even in some edge case the
     * smoothed gain path didn't fully catch. */
    out_l = clampd(out_l, -lim->ceiling, lim->ceiling);
    out_r = clampd(out_r, -lim->ceiling, lim->ceiling);

    *l = out_l;
    *r = out_r;
}

/* ---------------------------------------------------------------------
 * Battery-starve simulator (VST3 power icon feature only)
 * ------------------------------------------------------------------- */

void powerstarve_init(PowerStarve *ps, unsigned int seed) {
    ps->amount = 0.0;
    ps->lfoPhase = 0.0;
    noise_init(&ps->noise, seed);
}

void powerstarve_set_amount(PowerStarve *ps, double amount01) {
    ps->amount = clampd(amount01, 0.0, 1.0);
}

double powerstarve_process(PowerStarve *ps, double x, double sample_rate) {
    if (ps->amount <= 0.0001) return x; /* fast path when normal/unused */

    const double a = ps->amount;

    /* Sag/compression: reduced headroom as the battery weakens, more
     * aggressive soft clipping. */
    double driveAmt = 1.0 + a * 2.5;
    double norm = tanh(driveAmt);
    double sagged = (norm > 1e-9) ? (tanh(x * driveAmt) / norm) : x;

    /* Amplitude wobble: unstable supply voltage, gets faster and deeper
     * as the battery dies further. */
    double lfoRateHz = 0.6 + a * 3.0;
    ps->lfoPhase += 2.0 * M_PI * lfoRateHz / sample_rate;
    if (ps->lfoPhase > 2.0 * M_PI) ps->lfoPhase -= 2.0 * M_PI;
    double wobble = 1.0 + sin(ps->lfoPhase) * a * 0.18;

    /* Overall level loss -- approaches near-total signal loss at
     * amount=1.0, but floored at 1% rather than allowed to reach exact
     * 0 (per feedback: should never cause true silence, "near silence,
     * like 1%" instead). */
    double levelGain = 0.01 + 0.99 * pow(1.0 - a, 2.0);

    /* Power-supply noise bleeding in as headroom drops. */
    double hiss = noise_next(&ps->noise) * a * 0.02;

    return sagged * levelGain * wobble + hiss;
}

/* ---------------------------------------------------------------------
 * Noise gate (stereo-linked, slow-release) -- see header comment.
 * ------------------------------------------------------------------- */

void noisegate_init(NoiseGate *gate, double threshold_db, double sample_rate) {
    gate->envelope = 0.0;
    /* Start CLOSED, not open. Starting open (the original v0.10.0
     * choice) meant the gate offered zero protection for the very
     * first block(s) of audio -- exactly when a startup transient from
     * the DSP chain settling (freshly randomized filters/resonance,
     * etc.) is most likely, since the envelope follower hasn't had any
     * time yet to detect anything and react. Starting closed and
     * ramping open via the existing fast attack coefficient below
     * gives a genuine "slowly ramping on" startup, protecting against
     * that blast, while still opening quickly enough (10ms) to not
     * perceptibly delay real playing once actual signal arrives. */
    gate->gain = 0.0;
    gate->thresholdLinear = pow(10.0, threshold_db / 20.0);
    /* Envelope follower on the input: fast attack so real transients are
     * detected immediately, moderate release so brief dips between
     * notes don't cause chattering. */
    gate->envAttackCoeff = exp(-1.0 / (0.001 * 2.0 * sample_rate));
    gate->envReleaseCoeff = exp(-1.0 / (0.001 * 50.0 * sample_rate));
    /* Gate's own gain smoothing: fast-ish attack (re-opening, and now
     * also the initial startup ramp-on above) so legitimate playing
     * isn't perceptibly clipped when it resumes; SLOW release
     * (closing) -- "ramps down slowly to silence" per spec, a graceful
     * fade rather than an abrupt cutoff that would sound like a
     * glitch. */
    gate->gateAttackCoeff = exp(-1.0 / (0.001 * 10.0 * sample_rate));
    gate->gateReleaseCoeff = exp(-1.0 / (0.001 * 500.0 * sample_rate));
}

void noisegate_process(NoiseGate *gate, double inputL, double inputR, double *outputL, double *outputR) {
    double rectified = fabs(inputL);
    if (fabs(inputR) > rectified) rectified = fabs(inputR);

    double envCoeff = (rectified > gate->envelope) ? gate->envAttackCoeff : gate->envReleaseCoeff;
    gate->envelope = envCoeff * gate->envelope + (1.0 - envCoeff) * rectified;

    double targetGain = (gate->envelope > gate->thresholdLinear) ? 1.0 : 0.0;
    double gainCoeff = (targetGain > gate->gain) ? gate->gateAttackCoeff : gate->gateReleaseCoeff;
    gate->gain = gainCoeff * gate->gain + (1.0 - gainCoeff) * targetGain;

    *outputL *= gate->gain;
    *outputR *= gate->gain;
}

/* ---------------------------------------------------------------------
 * Waveshaping primitives
 * ------------------------------------------------------------------- */

static double clampd(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

/* Symmetric soft clip, normalized so output doesn't blow past unity as
 * gain increases. */
static double soft_clip(double x, double gain) {
    if (gain < 1e-6) return x;
    double norm = tanh(gain);
    if (norm < 1e-9) return x;
    return tanh(gain * x) / norm;
}

/* Asymmetric soft clip -- different curve above/below zero. Used for
 * germanium-fuzz and diode-asymmetry character. DC offset this
 * introduces is removed downstream by each block's dc_block filter. */
static double asym_soft_clip(double x, double pos_k, double neg_k) {
    if (x >= 0.0) {
        double norm = tanh(pos_k);
        return norm > 1e-9 ? tanh(pos_k * x) / norm : x;
    } else {
        double norm = tanh(neg_k);
        return norm > 1e-9 ? tanh(neg_k * x) / norm : x;
    }
}

/* Hard clip -- flatter "top" than tanh, closer to op-amp/diode clipping
 * character (Rat, Metal Zone). */
static double hard_clip(double x, double gain, double threshold) {
    double y = clampd(gain * x, -threshold, threshold);
    return y / threshold;
}

/* Coarse quantization -- used sparingly for Geiger Counter's
 * unstable/glitchy character at high drive. */
static double quantize(double x, double levels) {
    if (levels < 2.0) return x;
    return round(x * levels) / levels;
}

/* Maps a 0.0-1.0 random value to a WHAM pitch-shift amount in
 * semitones, weighted per the project spec: never 0, mostly +-12
 * (70% combined), sometimes another characterful interval (30%,
 * spread across octave/4th/5th/2nd shifts up and down). */
static double decode_wham_semitone(double u) {
    if (u < 0.35) return 12.0;
    if (u < 0.70) return -12.0;
    static const double others[] = { -24.0, -7.0, -5.0, -2.0, 2.0, 5.0, 7.0, 24.0 };
    double remainder = (u - 0.70) / 0.30;
    int idx = (int)(remainder * 8.0);
    if (idx > 7) idx = 7;
    if (idx < 0) idx = 0;
    return others[idx];
}

/* ---------------------------------------------------------------------
 * Block (single pedal slot)
 * ------------------------------------------------------------------- */

void distroy_block_init(DistroyBlock *b, DistroyType type, double sample_rate) {
    b->sample_rate = sample_rate;
    b->knob = 0.5;
    b->sub_drive = 0.6;
    b->sub_tone = 0.5;
    b->sub_level = 0.5;
    b->dc_block = (OnePole){0};
    onepole_set_highpass(&b->dc_block, 15.0, sample_rate);
    b->color_lp = (OnePole){0};
    b->color_hs = (OnePole){0};
    b->color_peak = (Biquad){0};
    b->tone_stage = (TiltEQ){0};
    b->env = (EnvelopeFollower){0};
    b->wah_filter = (Biquad){0};
    pitchshift_init(&b->pitch);
    decimator_init(&b->decim);
    noise_init(&b->noise, (unsigned int)(uintptr_t)b ^ 0x9e3779b9u);
    b->battery_amount = 0.0;
    distroy_block_set_type(b, type);
}

/* Tilt EQ center frequency per pedal -- picked to suit each pedal's
 * typical tonal range (a fuzz's tilt sits lower than a treble-forward
 * pedal like Rat/Tubescreamer). */
static double tilt_center_hz(DistroyType type) {
    switch (type) {
        case DISTROY_BOSS_OD:      return 1000.0;
        case DISTROY_FUZZ:         return 700.0;
        case DISTROY_METAL:        return 900.0;
        case DISTROY_TUBESCREAMER: return 1200.0;
        case DISTROY_BIG_MUFF:     return 800.0;
        case DISTROY_SANSAMP:      return 1000.0;
        case DISTROY_RAT:          return 1500.0;
        case DISTROY_GEIGER_COUNTER: return 900.0;
        case DISTROY_MUTRON:       return 1000.0;
        case DISTROY_CRYBABY:      return 1000.0;
        case DISTROY_JENSEN:       return 1500.0;
        case DISTROY_LUNDAHL:      return 1000.0;
        case DISTROY_FZ1W:         return 1000.0;
        case DISTROY_CLIP:         return 1000.0;
        case DISTROY_REKT:         return 900.0;
        case DISTROY_WHAM:         return 1000.0;
        case DISTROY_TAPE:         return 1500.0;
        case DISTROY_TUBE:         return 1200.0;
        case DISTROY_OCTAVE:       return 900.0;
        case DISTROY_BASS_MUFF:    return 400.0;
        case DISTROY_MXR_BASS:     return 350.0;
        case DISTROY_BOSS_ODB3:    return 450.0;
        default:                   return 1000.0;
    }
}

void distroy_block_set_type(DistroyBlock *b, DistroyType type) {
    b->type = type;
    double sr = b->sample_rate;

    tilteq_init(&b->tone_stage, tilt_center_hz(type), sr);
    b->tone_stage.tone = b->sub_tone;

    /* Battery-starve per-module malfunction personality (VST3-only
     * feature) -- each block gets its own randomized "which way it
     * fails" category, seeded from its address + type so different
     * slots (and the same slot landing on different types) get varied
     * personalities. NOTE: this seed is deterministic per (address,
     * type) pair, not re-randomized on every chain randomize call the
     * way sub_drive/tone/level are -- a known simplification (the
     * malfunction_rng state still generates fresh unpredictable
     * per-sample timing/amounts regardless, so the actual chaos still
     * varies; only the CATEGORY -- cutout vs crackle vs wobble vs
     * extra distortion -- is fixed per address+type). */
    unsigned int malfSeed = (unsigned int)(uintptr_t)b ^ ((unsigned int)type * 0x2545f491u) ^ 0x7f4a7c15u;
    malfSeed ^= malfSeed << 13; malfSeed ^= malfSeed >> 17; malfSeed ^= malfSeed << 5;
    b->malfunction_type = malfSeed % 4;
    b->malfunction_rng = malfSeed | 1u; /* ensure nonzero */
    b->malfunction_cutout_remaining = 0;
    b->malfunction_wobble_phase = 0.0;

    switch (type) {
        case DISTROY_BOSS_OD:
            onepole_set_highpass(&b->color_hs, 2000.0, sr); /* presence lift helper */
            break;
        case DISTROY_FUZZ:
            onepole_set_highpass(&b->color_hs, 150.0, sr); /* bass-thinning helper */
            break;
        case DISTROY_METAL:
            biquad_set_peaking(&b->color_peak, 600.0, 1.2, -7.0, sr); /* scooped mids */
            break;
        case DISTROY_TUBESCREAMER:
            biquad_set_peaking(&b->color_peak, 720.0, 0.7, 6.0, sr); /* mid hump, pre-clip */
            break;
        case DISTROY_BIG_MUFF:
            biquad_set_peaking(&b->color_peak, 500.0, 1.0, -3.0, sr); /* mild scoop */
            break;
        case DISTROY_SANSAMP:
            onepole_set_lowpass(&b->color_lp, 400.0, sr); /* warmth-boost helper */
            break;
        case DISTROY_RAT:
            onepole_set_lowpass(&b->color_lp, 5000.0, sr); /* "Filter" darkening, retuned per-sample below */
            break;
        case DISTROY_GEIGER_COUNTER:
            /* no fixed filter -- character comes from asym clip + quantize */
            break;
        case DISTROY_MOOG_LADDER:
            moog_ladder_init(&b->moog, sr);
            break;
        case DISTROY_KORG_MS20:
            korg35hp_init(&b->korg_hp, sr);
            korg35lp_init(&b->korg_lp, sr);
            break;
        case DISTROY_MUTRON:
            /* smoother/rounder auto-wah -- wider Q */
            envfollow_init(&b->env, 6.0, 120.0, sr);
            break;
        case DISTROY_CRYBABY:
            /* narrower/more vocal auto-wah -- higher Q, snappier envelope */
            envfollow_init(&b->env, 3.0, 90.0, sr);
            break;
        case DISTROY_JENSEN:
            onepole_set_lowpass(&b->color_lp, 12000.0, sr); /* bright, extended top */
            biquad_set_peaking(&b->color_peak, 80.0, 0.9, 2.0, sr); /* clean low-end lift */
            break;
        case DISTROY_LUNDAHL:
            onepole_set_lowpass(&b->color_lp, 8000.0, sr); /* darker top */
            biquad_set_peaking(&b->color_peak, 120.0, 0.9, 3.0, sr); /* more colored low-mid */
            break;
        case DISTROY_TAPE:
            onepole_set_lowpass(&b->color_lp, 9000.0, sr); /* tape HF softening */
            break;
        case DISTROY_FZ1W:
            biquad_set_peaking(&b->color_peak, 1000.0, 1.0, 2.0, sr); /* tighter presence than vintage Fuzz */
            break;
        case DISTROY_NOIZ:
            noisegen_init(&b->noisegen, (unsigned int)(uintptr_t)b ^ 0x51ed270bu);
            break;
        case DISTROY_TUBE:
            onepole_set_lowpass(&b->color_lp, 10000.0, sr); /* warm top-end rolloff */
            break;
        case DISTROY_CABL:
            cablesim_init(&b->cable, (unsigned int)(uintptr_t)b ^ 0x27d4eb2du);
            break;
        case DISTROY_SEM:
            semfilter_init(&b->sem);
            break;
        case DISTROY_POLIVOKS:
            polivoks_init(&b->polivoks);
            break;
        case DISTROY_BASS_MUFF:
            /* Split point for the low/high band divide -- see
             * type_process(). Low band stays clean-ish to preserve bass
             * fundamental; only the high band gets heavily clipped. */
            onepole_set_lowpass(&b->color_lp, 100.0, sr);
            break;
        case DISTROY_MXR_BASS:
            onepole_set_lowpass(&b->color_lp, 150.0, sr);
            break;
        case DISTROY_BOSS_ODB3:
            onepole_set_lowpass(&b->color_lp, 120.0, sr);
            break;
        case DISTROY_BASS_EQ:
            /* Low-shelf boost, knob-controlled amount (see
             * type_process() -- this just sets a placeholder here,
             * actually recomputed per-call since gain depends on the
             * knob). */
            biquad_set_lowshelf(&b->color_peak, 250.0, 0.0, sr);
            break;
        default:
            break;
    }
}

/* Per-type characteristic processing at a given drive amount (0.0-1.0).
 * Returns the "wet" (fully processed) signal; mixing with dry (for
 * WET_DRY-mode types) happens in distroy_block_process(). */
static double type_process(DistroyBlock *b, double x, double drive) {
    switch (b->type) {
        case DISTROY_BOSS_OD: {
            double gain = 1.0 + drive * 9.0;
            double y = soft_clip(x, gain);
            /* presence lift: blend a little high-passed signal back in */
            double hp = onepole_process(&b->color_hs, y);
            return y + 0.15 * hp;
        }
        case DISTROY_FUZZ: {
            double g = 3.0 + drive * 27.0;
            double y = asym_soft_clip(g * x, 3.0 + drive * 2.0, 1.5 + drive * 1.0);
            /* thin the bass a bit for classic fuzz character */
            double hp = onepole_process(&b->color_hs, y);
            return y * 0.6 + hp * 0.4;
        }
        case DISTROY_METAL: {
            double gain = 5.0 + drive * 35.0;
            double y = hard_clip(x, gain, 1.0);
            y = hard_clip(y, 2.0, 1.0); /* second cascaded stage */
            return biquad_process(&b->color_peak, y);
        }
        case DISTROY_TUBESCREAMER: {
            double pre = biquad_process(&b->color_peak, x); /* mid hump before clip */
            double g = 1.0 + drive * 6.0;
            return asym_soft_clip(g * pre, 2.0 + drive * 3.0, 1.5 + drive * 2.0);
        }
        case DISTROY_BIG_MUFF: {
            double gain = 3.0 + drive * 20.0;
            double y = hard_clip(x, gain, 1.0);
            y = hard_clip(y, 1.8, 1.0); /* second cascaded stage -- sustain character */
            return biquad_process(&b->color_peak, y);
        }
        case DISTROY_SANSAMP: {
            double gain = 2.0 + drive * 10.0;
            double sat = soft_clip(x, gain);
            double warm = onepole_process(&b->color_lp, x);
            return sat * 0.7 + warm * 0.3; /* amp-in-a-box blend character */
        }
        case DISTROY_RAT: {
            double gain = 5.0 + drive * 40.0;
            double y = hard_clip(x, gain, 1.0);
            /* darken more as drive increases -- Rat's Filter interaction */
            double cutoff = 8000.0 - drive * 6000.0;
            onepole_set_lowpass(&b->color_lp, cutoff, b->sample_rate);
            return onepole_process(&b->color_lp, y);
        }
        case DISTROY_GEIGER_COUNTER: {
            double gain = 8.0 + drive * 60.0;
            double y = asym_soft_clip(gain * x, 4.0 + drive * 4.0, 1.0 + drive * 6.0);
            double levels = 64.0 - drive * 48.0; /* more crunch at higher drive */
            return quantize(y, levels);
        }
        case DISTROY_MOOG_LADDER: {
            /* For CUTOFF-mode types, the "drive" argument here is
             * actually the primary knob value (0.0-1.0), log-mapped to
             * cutoff Hz. sub_drive/sub_tone are repurposed as the
             * filter's own Drive/Resonance (not the usual meaning). */
            double cutoff_hz = 80.0 * pow(8000.0 / 80.0, drive);
            moog_ladder_set(&b->moog, cutoff_hz, b->sub_tone, b->sub_drive);
            return moog_ladder_process(&b->moog, x);
        }
        case DISTROY_KORG_MS20: {
            double cutoff_hz = 80.0 * pow(8000.0 / 80.0, drive);
            /* HPF corner tracks proportionally below the main cutoff,
             * giving the classic MS-20 "sweeping narrow band" character
             * as the single knob moves, rather than a fixed HP corner. */
            double hp_cutoff = clampd(cutoff_hz * 0.15, 40.0, 2000.0);
            korg35hp_set(&b->korg_hp, hp_cutoff, b->sub_tone, b->sub_drive);
            korg35lp_set(&b->korg_lp, cutoff_hz, b->sub_tone, b->sub_drive);
            double y = korg35hp_process(&b->korg_hp, x);
            y = korg35lp_process(&b->korg_lp, y);
            return y;
        }
        case DISTROY_MUTRON: {
            /* SENS-mode: "drive" here is the knob value = envelope
             * sensitivity/depth. Smoother/rounder sweep than Cry Baby
             * (wider Q, wider but gentler frequency range). */
            double env = envfollow_process(&b->env, x);
            double sens = drive;
            double freq = 300.0 + clampd(env * sens * 6.0, 0.0, 1.0) * 1500.0;
            biquad_set_bandpass(&b->wah_filter, freq, 3.0, b->sample_rate);
            return biquad_process(&b->wah_filter, x);
        }
        case DISTROY_CRYBABY: {
            /* Narrower Q, snappier envelope, more vocal/aggressive sweep
             * than Mu-Tron. */
            double env = envfollow_process(&b->env, x);
            double sens = drive;
            double freq = 400.0 + clampd(env * sens * 6.0, 0.0, 1.0) * 1800.0;
            biquad_set_bandpass(&b->wah_filter, freq, 5.0, b->sample_rate);
            return biquad_process(&b->wah_filter, x);
        }
        case DISTROY_JENSEN: {
            double gain = 1.5 + drive * 3.5;
            double y = asym_soft_clip(gain * x, 3.0, 2.7); /* gentle, fairly symmetric */
            y = onepole_process(&b->color_lp, y); /* bright/extended top */
            return biquad_process(&b->color_peak, y); /* clean low-end lift */
        }
        case DISTROY_LUNDAHL: {
            double gain = 1.5 + drive * 3.0;
            double y = asym_soft_clip(gain * x, 2.5, 3.3); /* more asymmetric/colored */
            y = onepole_process(&b->color_lp, y); /* darker top */
            return biquad_process(&b->color_peak, y); /* more colored low-mid */
        }
        case DISTROY_LOFI: {
            /* RATE-mode: "drive" is the knob value, now directly
             * controlling sample rate (increasing with the knob -- at
             * max knob, sample rate is maximum), per spec. This
             * replaced the old WET_DRY blend where sub_tone encoded a
             * randomized sample rate and the knob was just a blend
             * amount. sub_drive still randomly encodes bit depth
             * (unrelated to the knob) -- never 16-bit. sub_tone is now
             * free for normal TiltEQ tone duty again (LOFI removed from
             * the tilt-skip list). */
            int bits = 1 + (int)(b->sub_drive * 14.0); /* 1-15, never 16 */
            double target_hz = 100.0 + drive * 9900.0; /* 100-10000 Hz, increases with knob, never 44100 */
            double y = decimator_process(&b->decim, x, target_hz, b->sample_rate);
            return quantize_bits(y, bits);
        }
        case DISTROY_FZ1W: {
            double gain = 4.0 + drive * 20.0;
            double y = asym_soft_clip(gain * x, 3.5 + drive * 1.5, 3.0 + drive * 1.5); /* tighter/more symmetric than Fuzz */
            return biquad_process(&b->color_peak, y);
        }
        case DISTROY_CLIP: {
            double gain = 3.0 + drive * 40.0;
            return hard_clip(x, gain, 1.0);
        }
        case DISTROY_REKT: {
            double gain = 3.0 + drive * 40.0;
            double y = hard_clip(x, gain, 1.0);
            /* Full-wave rectify -- the resulting DC offset is removed by
             * the universal dc_block downstream, leaving just the
             * harsh, pitched-up-sounding buzz character. */
            return fabs(y);
        }
        case DISTROY_WHAM: {
            double semitone = decode_wham_semitone(b->sub_drive);
            pitchshift_set_semitones(&b->pitch, semitone);
            return pitchshift_process(&b->pitch, x);
        }
        case DISTROY_TAPE: {
            double gain = 1.3 + drive * 1.8;
            double y = soft_clip(x, gain);
            y = onepole_process(&b->color_lp, y);
            double hiss = noise_next(&b->noise) * 0.0025;
            return y + hiss;
        }
        case DISTROY_SPKR: {
            /* SIZE-mode: "drive" here is the knob value, 0=impossibly
             * small (cell-phone-speaker tinny) to 1=impossibly large
             * (2-foot-woofer boomy). Reuses color_hs/color_lp/
             * color_peak (already-existing generic fields) as the
             * HPF/LPF/resonance-bump stages, no new struct fields
             * needed. Range deliberately extreme per spec ("impossibly
             * small... to impossibly large"), not a realistic speaker
             * range -- earlier version (80-300Hz HP / 3.5-6kHz LP) was
             * too subtle to hear clearly. */
            double size = drive;
            double hp_cutoff = 900.0 - size * 880.0;   /* 900Hz (phone) -> 20Hz (huge woofer) */
            double lp_cutoff = 5000.0 - size * 3700.0; /* 5000Hz (tinny) -> 1300Hz (dark/boomy) */
            double peak_freq = 2200.0 - size * 2140.0; /* 2200Hz (tinny peak) -> 60Hz (boom peak) */
            double peak_q = 2.5 - size * 1.3;          /* sharper tinny resonance -> looser boomy resonance */
            double peak_gain = 4.0 + size * 2.0;
            onepole_set_highpass(&b->color_hs, hp_cutoff, b->sample_rate);
            onepole_set_lowpass(&b->color_lp, lp_cutoff, b->sample_rate);
            biquad_set_peaking(&b->color_peak, peak_freq, peak_q, peak_gain, b->sample_rate);
            double y = onepole_process(&b->color_hs, x);
            y = onepole_process(&b->color_lp, y);
            return biquad_process(&b->color_peak, y);
        }
        case DISTROY_NOIZ: {
            /* WET_DRY-mode but with a SPECIAL CAP applied externally in
             * distroy_block_process (never lets the blend exceed 50%,
             * so the dry signal is never fully interrupted -- see the
             * DISTROY_NOIZ branch below for the cap value/history) --
             * this function just generates the raw noise; the cap is
             * applied at the mixing stage, not here. sub_tone
             * (repurposed, tilt skipped for this type) picks which
             * noise colour: 0.0-0.33 white, 0.33-0.66 pink, 0.66-1.0
             * red/brown. */
            NoiseColour colour = (b->sub_tone < 0.33) ? NOISE_WHITE
                                : (b->sub_tone < 0.66) ? NOISE_PINK
                                : NOISE_RED;
            (void)drive; /* drive/knob handled entirely by the capped blend in distroy_block_process */
            return noisegen_process(&b->noisegen, colour);
        }
        case DISTROY_TUBE: {
            /* Vintage Russian tube character -- gently rounded
             * asymmetric saturation (lower steepness than before =
             * smoother/rounder transition into clipping, per feedback
             * that it should "round off waveforms"), PLUS explicit
             * even-harmonic generation (the x*|x| term below is a
             * classic, well-known technique for adding warm 2nd-
             * harmonic "tube" character on top of the clipping curve
             * itself), warm HF rolloff, and a very subtle noise floor
             * (real tubes have some inherent hiss). */
            double gain = 1.3 + drive * 2.2;
            double driven = gain * x;
            double y = asym_soft_clip(driven, 1.6 + drive * 0.6, 2.0 + drive * 0.8);
            double harmonic = driven * fabs(driven) * 0.15; /* adds warm 2nd-harmonic content */
            y = y * 0.85 + harmonic * 0.15;
            y = onepole_process(&b->color_lp, y);
            double hiss = noise_next(&b->noise) * 0.0015;
            return y + hiss;
        }
        case DISTROY_CABL: {
            /* Full-wet, "drive" here is the knob value used directly as
             * the glitch severity parameter (see distroy_block_process
             * -- CABL is dispatched as a special case, not through the
             * normal WET_DRY blend, since the state machine already
             * decides internally how much of the dry signal survives
             * moment to moment). */
            return cablesim_process(&b->cable, x, drive, b->sample_rate);
        }
        case DISTROY_SEM: {
            /* CUTOFF-mode, but with an inverse cutoff/resonance
             * coupling per spec: "knob controlling increasing filter
             * cutoff frequency while decreasing Resonance. So with
             * knob at 0, cutoff is near 0, resonance is maximum. With
             * knob at 100%, cutoff is maximum, resonance is minimum."
             * sub_tone (repurposed, tilt skipped) holds the randomized
             * resonance CEILING (always 0.5-1.0, see
             * distroy_chain_randomize_all()) -- knob sweeps resonance
             * down from that ceiling toward 0 as it sweeps cutoff up.
             * Safe up to 100% resonance (see semfilter_set()'s comment
             * -- this topology doesn't reach true self-oscillation, no
             * "always 0" safety rule needed here unlike Moog/Korg). */
            double cutoff_hz = 80.0 * pow(8000.0 / 80.0, drive);
            double resonance = b->sub_tone * (1.0 - drive);
            semfilter_set(&b->sem, cutoff_hz, resonance, b->sample_rate);
            return semfilter_process(&b->sem, x);
        }
        case DISTROY_POLIVOKS: {
            /* CUTOFF-mode, same pattern as SEM -- sub_tone = resonance.
             * Also safe at high resonance (same 2-pole ladder-style
             * structure as Korg35, just with a harder clip instead of a
             * cubic soft-clip on the resonant node for the "growly"
             * distorted character). */
            double cutoff_hz = 80.0 * pow(8000.0 / 80.0, drive);
            polivoks_set(&b->polivoks, cutoff_hz, b->sub_tone, b->sub_drive, b->sample_rate);
            return polivoks_process(&b->polivoks, x);
        }
        case DISTROY_OCTAVE: {
            /* WET_DRY-mode. sub_tone (NOT repurposed for tilt-skip --
             * this type keeps normal Tone/TiltEQ, sub_tone here just
             * ALSO happens to pick direction, read once effectively
             * fixed per load since it doesn't change) picks octave
             * direction: <0.5 up (classic Octavia-style full-wave
             * rectification octave-up-fuzz technique), >=0.5 down
             * (reuses the WHAM pitch shifter fixed at -12 semitones,
             * plus some grit for authenticity -- real octave-down fuzz
             * pedals aren't perfectly clean either). */
            double gain = 3.0 + drive * 15.0;
            if (b->sub_tone >= 0.5) {
                pitchshift_set_semitones(&b->pitch, -12.0);
                double shifted = pitchshift_process(&b->pitch, x);
                return asym_soft_clip(gain * 0.6 * shifted, 3.0, 3.0);
            } else {
                double driven = tanh(gain * x);
                return fabs(driven); /* full-wave rectify -- dc_block downstream removes the resulting offset */
            }
        }
        case DISTROY_BASS_MUFF: {
            /* Bass-voiced Big Muff -- real bass distortion/fuzz pedals
             * typically split the signal internally, keeping the low
             * fundamental relatively clean/lightly saturated while
             * heavily clipping the upper harmonics, since naively
             * clipping the WHOLE signal (guitar-pedal style) tends to
             * mangle the low end a bass player actually needs. color_lp
             * (set in distroy_block_set_type -- ~100Hz here) is the
             * split point; low band gets gentle warmth, high band gets
             * the same cascaded-hard-clip character as the regular Big
             * Muff above. */
            double lowBand = onepole_process(&b->color_lp, x);
            double highBand = x - lowBand;
            double lowOut = soft_clip(lowBand * 1.4, 1.6);
            double gain = 3.0 + drive * 20.0;
            double y = hard_clip(highBand * gain, 1.0, 1.0);
            y = hard_clip(y, 1.8, 1.0);
            return lowOut * 0.85 + y * 0.55;
        }
        case DISTROY_MXR_BASS: {
            /* MXR Bass Distortion -- same low/high split idea (split
             * point ~150Hz), but with the more asymmetric clipping
             * curve characteristic of the MXR circuit rather than Big
             * Muff's symmetric cascaded clip. */
            double lowBand = onepole_process(&b->color_lp, x);
            double highBand = x - lowBand;
            double lowOut = soft_clip(lowBand * 1.3, 1.4);
            double gain = 4.0 + drive * 22.0;
            double y = asym_soft_clip(highBand * gain, 3.0 + drive * 2.0, 2.0 + drive * 1.5);
            return lowOut * 0.8 + y * 0.6;
        }
        case DISTROY_BOSS_ODB3: {
            /* Boss ODB-3 -- distinctly buzzy/"synth-like" bass overdrive
             * character (split point ~120Hz), achieved with a harder,
             * more aggressive high-band clip plus a touch of coarse
             * quantization for that characteristic ODB-3 buzz that
             * distinguishes it from smoother bass overdrives. */
            double lowBand = onepole_process(&b->color_lp, x);
            double highBand = x - lowBand;
            double lowOut = soft_clip(lowBand * 1.2, 1.3);
            double gain = 6.0 + drive * 30.0;
            double y = hard_clip(highBand * gain, 1.0, 1.0);
            double levels = 40.0 - drive * 20.0; /* coarse quantization -- the buzzy ODB-3 grit */
            y = quantize(y, levels);
            return lowOut * 0.75 + y * 0.6;
        }
        case DISTROY_BASS_EQ: {
            /* GAIN-mode: "drive" is the knob value directly, 0.0-1.0
             * mapped to 0-12dB of low-shelf boost centered at 250Hz
             * (comfortably covers the spec's "<300Hz emphasis"). Simple
             * EQ, not a distortion -- no waveshaping at all. */
            double boost_db = drive * 12.0;
            biquad_set_lowshelf(&b->color_peak, 250.0, boost_db, b->sample_rate);
            return biquad_process(&b->color_peak, x);
        }
        default:
            return x;
    }
}

/* Battery-starve per-module chaos (VST3-only feature, see the
 * DistroyBlock/DistroyChain struct comments) -- applied at the very end
 * of distroy_block_process(), after everything else, so it colors
 * whatever that pedal's normal output would have been. Inert whenever
 * battery_amount is 0 (the default, and always the case on Move). Each
 * block's malfunction_type (randomized once in distroy_block_set_type)
 * picks ONE of four failure characters, so different pedals in the same
 * chain misbehave differently rather than one uniform global effect --
 * that's layered separately via PowerStarve in the wrapper. */
static double apply_battery_malfunction(DistroyBlock *b, double out) {
    double a = b->battery_amount;
    if (a <= 0.001) return out;

    unsigned int *rng = &b->malfunction_rng;

    switch (b->malfunction_type) {
        case 0: {
            /* Intermittent cutout -- like a loose connection dropping
             * out entirely for brief moments. Never fully silent during
             * a cutout (same "never truly 0" pattern used elsewhere in
             * this project for safety-adjacent glitch effects). */
            if (b->malfunction_cutout_remaining > 0) {
                b->malfunction_cutout_remaining--;
                return out * 0.05;
            }
            *rng ^= *rng << 13; *rng ^= *rng >> 17; *rng ^= *rng << 5;
            double chance = a * 0.00015; /* scales with how dead the battery is */
            if ((double)(*rng % 1000000) / 1000000.0 < chance) {
                *rng ^= *rng << 13; *rng ^= *rng >> 17; *rng ^= *rng << 5;
                double durSec = 0.01 + a * 0.06 + (double)(*rng % 1000) / 1000.0 * 0.04;
                b->malfunction_cutout_remaining = (int)(b->sample_rate * durSec);
            }
            return out;
        }
        case 1: {
            /* Crackle burst -- like a failing solder joint or dying
             * capacitor spitting noise. */
            *rng ^= *rng << 13; *rng ^= *rng >> 17; *rng ^= *rng << 5;
            double chance = a * 0.0004;
            if ((double)(*rng % 1000000) / 1000000.0 < chance) {
                *rng ^= *rng << 13; *rng ^= *rng >> 17; *rng ^= *rng << 5;
                double burst = ((double)(*rng % 2000000) / 1000000.0 - 1.0) * a * 0.7;
                out += burst;
            }
            return out;
        }
        case 2: {
            /* Wobble -- an unstable operating point, amplitude
             * fluttering irregularly rather than the smooth global
             * wobble PowerStarve already applies. */
            b->malfunction_wobble_phase += 2.0 * M_PI * (4.0 + a * 20.0) / b->sample_rate;
            if (b->malfunction_wobble_phase > 2.0 * M_PI) b->malfunction_wobble_phase -= 2.0 * M_PI;
            return out * (1.0 + sin(b->malfunction_wobble_phase) * a * 0.2);
        }
        case 3:
        default: {
            /* Extra distortion -- collapsing headroom makes an already-
             * driven circuit clip harder and get fartier as voltage
             * sags. */
            double driveAmt = 1.0 + a * 3.5;
            double norm = tanh(driveAmt);
            return (norm > 1e-9) ? (tanh(out * driveAmt) / norm) : out;
        }
    }
}

double distroy_block_process(DistroyBlock *b, double x) {
    const DistroyTypeInfo *info = distroy_type_info(b->type);
    double wet, out;
    /* These types repurpose sub_tone for something other than TiltEQ
     * tone (Moog/Korg/SEM/Polivoks: Resonance, NOIZ: noise colour
     * select) -- keep the tone stage neutral/flat for them so it
     * doesn't double up. NOTE: this is NOT simply "all CUTOFF-mode
     * types" -- SPKR is also CUTOFF mode but keeps normal Tone/TiltEQ,
     * since its sub_tone still means Tone for that type. LOFI USED TO
     * be in this list (sub_tone encoded sample rate) but the knob now
     * controls sample rate directly, freeing sub_tone back up for
     * normal Tone/TiltEQ duty. */
    int skip_tilt = (b->type == DISTROY_MOOG_LADDER || b->type == DISTROY_KORG_MS20
                      || b->type == DISTROY_NOIZ
                      || b->type == DISTROY_SEM || b->type == DISTROY_POLIVOKS);

    b->tone_stage.tone = skip_tilt ? 0.5 : b->sub_tone;

    if (b->type == DISTROY_CABL) {
        /* Special case: NOT the standard WET_DRY blend, even though its
         * declared knob_mode is WET_DRY (for label purposes -- "MIX"
         * reads reasonably as "how broken"). The knob is passed
         * straight through as the glitch severity parameter; the state
         * machine inside type_process/cablesim_process already decides
         * internally how much dry signal survives moment to moment, so
         * an ADDITIONAL external blend here would just double up. */
        wet = type_process(b, x, b->knob);
        out = wet;
    } else if (b->type == DISTROY_NOIZ) {
        /* Special case: standard WET_DRY blend formula, but the wet
         * contribution is capped -- originally 66%, lowered to 50%
         * since even 66% got too loud once amplified downstream in a
         * chain, per direct feedback. Guarantees at least 50% dry
         * signal always survives. */
        double cappedKnob = b->knob * 0.50;
        wet = type_process(b, x, b->sub_drive);
        out = x * (1.0 - cappedKnob) + wet * cappedKnob;
    } else if (info->knob_mode == DISTROY_KNOB_WET_DRY) {
        wet = type_process(b, x, b->sub_drive);
        out = x * (1.0 - b->knob) + wet * b->knob;
    } else {
        /* GAIN, CUTOFF, SENS, and SIZE modes all pass the knob straight
         * through and are fully wet (no dry blend) -- see type_process
         * for how each mode interprets this argument (drive, cutoff
         * frequency, or envelope sensitivity respectively). */
        wet = type_process(b, x, b->knob);
        out = wet;
    }

    out = tilteq_process(&b->tone_stage, out);
    out = onepole_process(&b->dc_block, out);

    /* Level trim: sub_level 0.0-1.0 maps to roughly 0.7x-1.3x (+-3dB-ish),
     * tasteful range so it colors output level without wrecking gain
     * staging through the rest of the chain. */
    double level_gain = 0.7 + b->sub_level * 0.6;
    out = out * level_gain;

    return apply_battery_malfunction(b, out);
}

double distroy_block_get_envelope_level(const DistroyBlock *b) {
    return b->env.envelope;
}

/* ---------------------------------------------------------------------
 * Chain (8 slots)
 * ------------------------------------------------------------------- */

void distroy_chain_init(DistroyChain *c, double sample_rate) {
    c->sample_rate = sample_rate;
    c->reverse = 0; /* default: left to right (slot 0 first) */
    c->battery_amount = 0.0;
    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
        distroy_block_init(&c->slots[i], DISTROY_BOSS_OD, sample_rate);
    }
    for (int i = 0; i < DISTROY_NUM_GAPS; i++) {
        c->phaseInvertGap[i] = 0;
        c->zcSmoothGap[i] = 0;
        zcsmoother_init(&c->zcSmoothers[i], sample_rate);
    }
    for (int i = 0; i < DISTROY_NUM_GAPS; i++) {
        tilteq_init(&c->masterToneGaps[i], 900.0, sample_rate); /* tilteq_init already sets .tone = 0.5 (flat/neutral, 12 o'clock default) */
    }
}

static unsigned int xorshift_next(unsigned int *state) {
    unsigned int s = *state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *state = s;
    return s;
}

/* Shuffles all DISTROY_TYPE_COUNT type indices (Fisher-Yates) into
 * `out`, writing the first `count` entries. Guarantees no duplicate
 * pedal type among the slots -- requires DISTROY_TYPE_COUNT >= count
 * (10 >= 8, currently true; if more slots than types ever existed this
 * would need to wrap/repeat, not attempted here since it's not the
 * current situation). */
static void shuffle_distinct_types(DistroyType *out, int count, unsigned int *state) {
    DistroyType pool[DISTROY_TYPE_COUNT];
    for (int i = 0; i < DISTROY_TYPE_COUNT; i++) pool[i] = (DistroyType)i;
    for (int i = DISTROY_TYPE_COUNT - 1; i > 0; i--) {
        unsigned int j = xorshift_next(state) % (unsigned int)(i + 1);
        DistroyType tmp = pool[i];
        pool[i] = pool[j];
        pool[j] = tmp;
    }
    for (int i = 0; i < count && i < DISTROY_TYPE_COUNT; i++) {
        out[i] = pool[i];
    }
}

void distroy_chain_randomize(DistroyChain *c, unsigned int seed) {
    unsigned int state = seed;
    DistroyType types[DISTROY_NUM_SLOTS];
    shuffle_distinct_types(types, DISTROY_NUM_SLOTS, &state);
    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
        distroy_block_set_type(&c->slots[i], types[i]);
    }
}

static void shuffle_pool(DistroyType *pool, int poolCount, unsigned int *state) {
    for (int i = poolCount - 1; i > 0; i--) {
        unsigned int j = xorshift_next(state) % (unsigned int)(i + 1);
        DistroyType tmp = pool[i];
        pool[i] = pool[j];
        pool[j] = tmp;
    }
}

/* Not static -- exposed publicly (see distroy_dsp.h) so callers can
 * force a SPECIFIC slot to a specific type with full proper
 * randomization/safety-capping of its sub-parameters, reusing the
 * exact same logic distroy_chain_randomize_all(_restricted) uses
 * internally rather than duplicating it. Used by db-cell to guarantee
 * one slot is always DISTROY_NOIZ. state is a pointer to an ongoing
 * RNG state the caller owns and can keep advancing across multiple
 * calls. */
void distroy_randomize_slot_as_type(DistroyBlock *slot, DistroyType t, unsigned int *state) {
    distroy_block_set_type(slot, t);
    /* xorshift_next returns a full-range unsigned int -- scale to
     * 0.0-1.0 */
    slot->knob = (double)xorshift_next(state) / (double)UINT32_MAX;
    slot->sub_drive = (double)xorshift_next(state) / (double)UINT32_MAX;

    double tone_rand = (double)xorshift_next(state) / (double)UINT32_MAX;
    if (t == DISTROY_MOOG_LADDER || t == DISTROY_KORG_MS20) {
        /* SAFETY: resonance (repurposed sub_tone) is no longer
         * randomized at all for these two types -- always 0. Even
         * a 25% cap (v0.4.2) still howled loudly enough in some
         * randomized chains to risk hurting ears/speakers. Live
         * resonance control is planned for a future submenu (see
         * README's open questions) where the user can dial it in
         * deliberately; randomization just leaves it off. */
        tone_rand = 0.0;
    } else if (t == DISTROY_POLIVOKS) {
        /* Capped at 20% max -- 40% (v0.9.0) still howled/resonated
         * into a near-constant tone, per direct feedback. Lowered
         * further. */
        tone_rand *= 0.2;
    } else if (t == DISTROY_SEM) {
        /* Always randomizes to 50-100% -- this is the resonance
         * CEILING used at knob=0 (see type_process: knob sweeps
         * cutoff up while sweeping resonance down from this
         * ceiling toward 0), so it's deliberately always
         * substantial, per spec ("should always randomize to have
         * resonance above 50%"). */
        tone_rand = 0.5 + tone_rand * 0.5;
    }
    slot->sub_tone = tone_rand;

    slot->sub_level = (double)xorshift_next(state) / (double)UINT32_MAX;
    slot->tone_stage.tone = slot->sub_tone;
}

/* Shared implementation: randomizes DISTROY_NUM_SLOTS slots (type +
 * all sub-parameters), drawing distinct types from the given pool
 * (which may be a subset of all DISTROY_TYPE_COUNT types -- used by
 * db-cell's restricted-type-pool randomization; the full-pool case
 * just passes every type). poolCount must be >= DISTROY_NUM_SLOTS. */
static void randomize_all_from_pool(DistroyChain *c, unsigned int seed, DistroyType *pool, int poolCount) {
    unsigned int state = seed;
    shuffle_pool(pool, poolCount, &state);
    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
        distroy_randomize_slot_as_type(&c->slots[i], pool[i], &state);
    }
}

void distroy_chain_randomize_all(DistroyChain *c, unsigned int seed) {
    DistroyType pool[DISTROY_TYPE_COUNT];
    for (int i = 0; i < DISTROY_TYPE_COUNT; i++) pool[i] = (DistroyType)i;
    randomize_all_from_pool(c, seed, pool, DISTROY_TYPE_COUNT);
}

/* Same as distroy_chain_randomize_all, but draws distinct types only
 * from the given allowed-types list (must have at least
 * DISTROY_NUM_SLOTS entries, no duplicates). Used by db-cell to
 * restrict which pedal types can appear (auto-wahs, Whammy, and most
 * resonant filters excluded from its randomization pool). */
void distroy_chain_randomize_all_restricted(DistroyChain *c, unsigned int seed, const DistroyType *allowedTypes, int allowedCount) {
    DistroyType pool[DISTROY_TYPE_COUNT];
    int n = allowedCount < DISTROY_TYPE_COUNT ? allowedCount : DISTROY_TYPE_COUNT;
    for (int i = 0; i < n; i++) pool[i] = allowedTypes[i];
    randomize_all_from_pool(c, seed, pool, n);
}

/* Nudges every slot's non-knob-controlled sub-parameters (sub_drive,
 * sub_tone, sub_level) by a small random amount, leaving the actual
 * knob value and pedal type completely untouched. Used by the VST3's
 * clickable corner screws -- clicking a screw both re-rolls that
 * corner's decorative screw type AND gives the whole chain's internal
 * "character" a small random jostle, like tapping a well-used
 * pedalboard slightly shifts component tolerances/drift, without
 * disturbing anything the user has actually dialed in. amount01 is the
 * MAXIMUM perturbation magnitude (e.g. 0.1 for +-10%); each parameter
 * gets an independent random delta in [-amount01, +amount01], clamped
 * back into the valid 0.0-1.0 range. Deliberately does NOT re-run any
 * of the type-specific randomization caps (Moog/Korg resonance-zero,
 * Polivoks 20% cap, SEM 50-100% ceiling, etc.) -- this is a SMALL
 * nudge from wherever the parameter currently sits, not a fresh
 * randomization, so those safety caps remain implicitly respected as
 * long as the starting point already respected them (which it always
 * will, since it can only be reached via distroy_block_set_type() or a
 * previous nudge). */
void distroy_chain_nudge_subparams(DistroyChain *c, double amount01, unsigned int seed) {
    unsigned int state = seed | 1u;
    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
        DistroyBlock *b = &c->slots[i];
        double d1 = ((double)xorshift_next(&state) / (double)UINT32_MAX * 2.0 - 1.0) * amount01;
        double d2 = ((double)xorshift_next(&state) / (double)UINT32_MAX * 2.0 - 1.0) * amount01;
        double d3 = ((double)xorshift_next(&state) / (double)UINT32_MAX * 2.0 - 1.0) * amount01;

        double newDrive = b->sub_drive + d1;
        double newTone = b->sub_tone + d2;
        double newLevel = b->sub_level + d3;
        b->sub_drive = (newDrive < 0.0) ? 0.0 : (newDrive > 1.0 ? 1.0 : newDrive);
        b->sub_tone = (newTone < 0.0) ? 0.0 : (newTone > 1.0 ? 1.0 : newTone);
        b->sub_level = (newLevel < 0.0) ? 0.0 : (newLevel > 1.0 ? 1.0 : newLevel);
        b->tone_stage.tone = b->sub_tone;
    }
}

static double sanitize_finite(double y) {
    /* Safety net against non-finite output (NaN/Inf) -- can arise from
     * genuine marginal numerical instability in the older resonant
     * filter types (SEM/Polivoks/Moog Ladder/Korg MS-20 are all
     * capable of self-oscillation under extreme parameter
     * combinations, a well-known real-world DSP hazard) under
     * worst-case stress (all knobs maxed, particular random pedal
     * combinations) -- observed to trigger on some platforms/compilers
     * and not others, consistent with a marginal-stability issue whose
     * exact tipping point is sensitive to small floating-point
     * differences rather than a deterministic logic bug. Silence
     * (0.0) rather than clamping to some large finite value, since a
     * "fail gracefully to silence" is safer than a loud clamped
     * transient. Critically applied HERE (right after each pedal's own
     * processing, before anything stateful downstream sees it) rather
     * than only at the very end -- a NaN reaching a stateful filter
     * (the zero-crossing smoother's onepole, or the master tone tilt
     * EQ's two onepoles) would permanently corrupt that filter's
     * internal state for every future sample, not just the one bad
     * sample, since NaN propagates through all further arithmetic
     * using that state. */
    return isfinite(y) ? y : 0.0;
}

static double apply_gap_effects(DistroyChain *c, int gapIndex, double y) {
    /* gapIndex is a VISUAL position (0-6, between visual slots gapIndex
     * and gapIndex+1) -- called from distroy_chain_process() at the
     * point in the ACTUAL processing order where that visual boundary
     * falls, which depends on direction (see call sites below). Both
     * effects are direction-agnostic (inverting phase or smoothing
     * doesn't care which way the signal conceptually "flows" through
     * that boundary), so this same helper covers both cases. */
    if (c->phaseInvertGap[gapIndex]) {
        y = -y;
    }
    if (c->zcSmoothGap[gapIndex]) {
        y = sanitize_finite(zcsmoother_process(&c->zcSmoothers[gapIndex], y, c->sample_rate));
    }
    /* Master Tone -- always runs (not gated behind a toggle like the
     * two above), since tone=0.5 is already a genuine no-op on its
     * own; no separate "off" state needed. */
    y = sanitize_finite(tilteq_process(&c->masterToneGaps[gapIndex], y));
    return y;
}

double distroy_chain_process(DistroyChain *c, double x) {
    /* Sync the chain-level battery amount into each slot -- cheap to do
     * every sample (a single double assignment x8), keeps this simple
     * rather than needing a separate "did it change" dirty-check. Move
     * never sets c->battery_amount away from 0, so this is always inert
     * there. */
    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
        c->slots[i].battery_amount = c->battery_amount;
    }

    double y = x;
    if (c->reverse) {
        /* Right to left: slot 7 processes first, slot 0 last. Gap i
         * (between visual slots i and i+1) is crossed right after
         * processing visual slot i+1, before continuing to visual
         * slot i. */
        for (int i = DISTROY_NUM_SLOTS - 1; i >= 0; i--) {
            y = sanitize_finite(distroy_block_process(&c->slots[i], y));
            if (i > 0) {
                y = apply_gap_effects(c, i - 1, y);
            }
        }
    } else {
        /* Left to right (default): slot 0 processes first, slot 7 last.
         * Gap i is crossed right after processing visual slot i, before
         * continuing to visual slot i+1. */
        for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
            y = sanitize_finite(distroy_block_process(&c->slots[i], y));
            if (i < DISTROY_NUM_SLOTS - 1) {
                y = apply_gap_effects(c, i, y);
            }
        }
    }

    return y;
}

void distroy_chain_set_master_tone(DistroyChain *c, double tone01) {
    if (tone01 < 0.0) tone01 = 0.0;
    if (tone01 > 1.0) tone01 = 1.0;
    for (int i = 0; i < DISTROY_NUM_GAPS; i++) {
        c->masterToneGaps[i].tone = tone01;
    }
}

void distroy_chain_set_phase_invert(DistroyChain *c, int gapIndex, int enabled) {
    if (gapIndex < 0 || gapIndex >= DISTROY_NUM_GAPS) return;
    c->phaseInvertGap[gapIndex] = enabled ? 1 : 0;
}

void distroy_chain_set_zc_smooth(DistroyChain *c, int gapIndex, int enabled) {
    if (gapIndex < 0 || gapIndex >= DISTROY_NUM_GAPS) return;
    c->zcSmoothGap[gapIndex] = enabled ? 1 : 0;
}

int distroy_chain_get_phase_invert(const DistroyChain *c, int gapIndex) {
    if (gapIndex < 0 || gapIndex >= DISTROY_NUM_GAPS) return 0;
    return c->phaseInvertGap[gapIndex];
}

int distroy_chain_get_zc_smooth(const DistroyChain *c, int gapIndex) {
    if (gapIndex < 0 || gapIndex >= DISTROY_NUM_GAPS) return 0;
    return c->zcSmoothGap[gapIndex];
}

void distroy_chain_set_slew_ms(DistroyChain *c, int gapIndex, double maxMs) {
    if (gapIndex < 0 || gapIndex >= DISTROY_NUM_GAPS) return;
    zcsmoother_set_max_slew_ms(&c->zcSmoothers[gapIndex], maxMs);
}

double distroy_chain_get_slew_ms(const DistroyChain *c, int gapIndex) {
    if (gapIndex < 0 || gapIndex >= DISTROY_NUM_GAPS) return 0.0;
    return c->zcSmoothers[gapIndex].maxSlewMs;
}

