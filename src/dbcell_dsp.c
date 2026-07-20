#include "dbcell_dsp.h"
#include <math.h>
#include <string.h>
#include <stdint.h>

static double dbcell_clampd(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static unsigned int dbcell_xorshift_next(unsigned int *state) {
    unsigned int s = *state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *state = s;
    return s;
}

static double dbcell_rand01(unsigned int *state) {
    return (double)dbcell_xorshift_next(state) / (double)UINT32_MAX;
}

/* Cheap approximate standard-normal sample -- same construction as
 * db-cell's own gaussianApprox(): sum of 3 uniform draws, recentred
 * and scaled for unit variance (verified via the variance algebra in
 * db-cell's own development, not re-derived here). */
static double dbcell_gaussian_approx(unsigned int *state) {
    double sum = dbcell_rand01(state) + dbcell_rand01(state) + dbcell_rand01(state);
    return (sum - 1.5) * 2.0;
}

static int dbcell_instant_samples(unsigned int *state) {
    return 1 + (int)(dbcell_rand01(state) * 3.0);
}

static int dbcell_fast_ramp_samples(unsigned int *state) {
    return (int)(735.0 + dbcell_rand01(state) * 1275.0);
}

static int dbcell_slow_ramp_samples(unsigned int *state) {
    return (int)(20000.0 + dbcell_rand01(state) * 150000.0);
}

/* ---------------------------------------------------------------------
 * ChaosModulator -- ported from db-cell's Source/PluginProcessor.h.
 * ------------------------------------------------------------------- */

void dbcell_chaos_reset(DbCellChaosModulator *c, unsigned int seed) {
    memset(c, 0, sizeof(*c));
    c->state = seed;
    c->currentGain = 1.0;
    c->startGain = 1.0;
    c->targetGain = 1.0;
    c->rampSamples = 1;
    c->samplesIntoRamp = 1;
    c->intensity = 1.0;
    c->ouValue = 1.0;
    c->eventMultiplier = 1.0;
    c->eventStart = 1.0;
    c->eventTarget = 1.0;
    c->eventRampSamples = 1;
    c->eventSamplesIntoRamp = 1;
    c->samplesUntilNextEventCheck = 1;
    c->sampleRate = 44100.0;
}

void dbcell_chaos_set_intensity(DbCellChaosModulator *c, double intensity01) {
    c->intensity = dbcell_clampd(intensity01, 0.0, 1.0);
}

void dbcell_chaos_set_sample_rate(DbCellChaosModulator *c, double sampleRate) {
    c->sampleRate = sampleRate;
}

static double dbcell_chaos_next_legacy(DbCellChaosModulator *c) {
    if (c->samplesIntoRamp >= c->rampSamples) {
        c->startGain = c->currentGain;

        if (c->popStage == 1) {
            c->targetGain = c->currentGain;
            c->rampSamples = (int)(20.0 + dbcell_rand01(&c->state) * 150.0);
            c->popStage = 2;
            c->legacyToneLinked = 0;
        } else if (c->popStage == 2) {
            c->targetGain = 1.0;
            c->rampSamples = dbcell_instant_samples(&c->state);
            c->popStage = 0;
        } else {
            const double roll = dbcell_rand01(&c->state);
            const double restChance = 0.55 + (1.0 - c->intensity) * 0.35;
            if (roll < restChance) {
                c->targetGain = 1.0;
                c->rampSamples = dbcell_slow_ramp_samples(&c->state);
                c->legacyToneLinked = 0;
            } else {
                const double eventRoll = dbcell_rand01(&c->state);
                if (eventRoll < 0.30) {
                    c->targetGain = dbcell_rand01(&c->state) * 0.04;
                    c->rampSamples = dbcell_instant_samples(&c->state);
                    c->popStage = 1;
                    c->legacyToneLinked = 0;
                } else if (eventRoll < 0.30 + 0.20) {
                    c->targetGain = dbcell_rand01(&c->state) * 0.04;
                    c->rampSamples = dbcell_fast_ramp_samples(&c->state);
                    c->legacyToneLinked = dbcell_rand01(&c->state) < 0.5;
                } else if (eventRoll < 0.30 + 0.20 + 0.20) {
                    c->targetGain = 1.0 - c->intensity * (0.775 + dbcell_rand01(&c->state) * 0.125);
                    c->rampSamples = dbcell_rand01(&c->state) < 0.5 ? dbcell_fast_ramp_samples(&c->state) : dbcell_slow_ramp_samples(&c->state);
                    c->legacyToneLinked = dbcell_rand01(&c->state) < 0.5;
                } else {
                    c->targetGain = 1.0 - c->intensity * dbcell_rand01(&c->state) * 0.225;
                    c->rampSamples = dbcell_rand01(&c->state) < 0.4 ? dbcell_fast_ramp_samples(&c->state) : dbcell_slow_ramp_samples(&c->state);
                    c->legacyToneLinked = dbcell_rand01(&c->state) < 0.5;
                }
            }
        }
        c->samplesIntoRamp = 0;
    }
    const double t = (double)c->samplesIntoRamp / (double)c->rampSamples;
    c->currentGain = c->startGain + (c->targetGain - c->startGain) * t;
    c->samplesIntoRamp++;
    return c->currentGain;
}

static double dbcell_chaos_next_hybrid(DbCellChaosModulator *c) {
    const double dt = 1.0 / c->sampleRate;
    const double theta = 1.0 + c->intensity * 2.0;
    const double sigma = 0.042 + c->intensity * 0.448;
    const double gaussian = dbcell_gaussian_approx(&c->state);
    c->ouValue += theta * (1.0 - c->ouValue) * dt + sigma * gaussian * sqrt(dt);
    c->ouValue = dbcell_clampd(c->ouValue, 0.0, 1.6);

    c->samplesUntilNextEventCheck--;
    if (c->samplesUntilNextEventCheck <= 0) {
        if (c->eventSamplesIntoRamp >= c->eventRampSamples) {
            c->eventStart = c->eventMultiplier;
            if (c->eventPopStage == 1) {
                c->eventTarget = c->eventMultiplier;
                c->eventRampSamples = (int)(20.0 + dbcell_rand01(&c->state) * 150.0);
                c->eventPopStage = 2;
            } else if (c->eventPopStage == 2) {
                c->eventTarget = 1.0;
                c->eventRampSamples = dbcell_instant_samples(&c->state);
                c->eventPopStage = 0;
            } else {
                const double eventRoll = dbcell_rand01(&c->state);
                if (eventRoll < 0.5) {
                    c->eventTarget = dbcell_rand01(&c->state) * 0.04;
                    c->eventRampSamples = dbcell_instant_samples(&c->state);
                    c->eventPopStage = 1;
                } else {
                    c->eventTarget = dbcell_rand01(&c->state) * 0.04;
                    c->eventRampSamples = dbcell_fast_ramp_samples(&c->state);
                }
            }
            c->eventSamplesIntoRamp = 0;
        }
        const double meanWaitSamples = c->sampleRate * (4.0 - c->intensity * 3.7);
        const double u = dbcell_rand01(&c->state);
        const double uClamped = u > 1e-6 ? u : 1e-6;
        int waitSamples = (int)(-log(uClamped) * meanWaitSamples);
        c->samplesUntilNextEventCheck = waitSamples > 1 ? waitSamples : 1;
    }

    if (c->eventSamplesIntoRamp < c->eventRampSamples) {
        const double t = (double)c->eventSamplesIntoRamp / (double)c->eventRampSamples;
        c->eventMultiplier = c->eventStart + (c->eventTarget - c->eventStart) * t;
        c->eventSamplesIntoRamp++;
    }

    c->hybridToneLinked = (c->eventPopStage == 0) && (dbcell_rand01(&c->state) < 0.002);

    c->currentGain = dbcell_clampd(c->ouValue * c->eventMultiplier, 0.0, 1.6);
    return c->currentGain;
}

double dbcell_chaos_next_sample(DbCellChaosModulator *c) {
    const double legacyGain = dbcell_chaos_next_legacy(c);
    const double hybridGain = dbcell_chaos_next_hybrid(c);
    c->currentGain = (legacyGain + hybridGain) * 0.5;
    c->toneLinkedThisSegment = c->legacyToneLinked || c->hybridToneLinked;
    return c->currentGain;
}

int dbcell_chaos_is_tone_linked(const DbCellChaosModulator *c) {
    return c->toneLinkedThisSegment;
}

double dbcell_chaos_get_gain_deviation(const DbCellChaosModulator *c) {
    return c->currentGain - 1.0;
}

/* ---------------------------------------------------------------------
 * DbCellEngine -- ported from db-cell's PluginProcessor.cpp.
 * ------------------------------------------------------------------- */

/* Restricted randomization pool for the 7 non-forced slots -- same 25
 * of DISTROYBOY's 31 pedal types as db-cell's own kAllowedTypes. */
static const DistroyType dbcellAllowedTypes[] = {
    DISTROY_BOSS_OD, DISTROY_FUZZ, DISTROY_METAL, DISTROY_TUBESCREAMER,
    DISTROY_BIG_MUFF, DISTROY_SANSAMP, DISTROY_RAT, DISTROY_GEIGER_COUNTER,
    DISTROY_MOOG_LADDER, DISTROY_KORG_MS20, DISTROY_JENSEN, DISTROY_LUNDAHL,
    DISTROY_LOFI, DISTROY_FZ1W, DISTROY_CLIP, DISTROY_REKT,
    DISTROY_TAPE, DISTROY_SPKR, DISTROY_TUBE,
    DISTROY_CABL, DISTROY_OCTAVE, DISTROY_BASS_MUFF, DISTROY_MXR_BASS,
    DISTROY_BOSS_ODB3, DISTROY_BASS_EQ
};
static const int dbcellAllowedTypeCount = (int)(sizeof(dbcellAllowedTypes) / sizeof(dbcellAllowedTypes[0]));

void dbcell_engine_init(DbCellEngine *e, double sampleRate, unsigned int seed) {
    memset(e, 0, sizeof(*e));
    e->sampleRate = sampleRate;
    e->wetMix = 0.03f;
    e->baseTone = 0.5;
    e->randomizeSeedCounter = 0;

    distroy_chain_init(&e->chainLeft, sampleRate);
    distroy_chain_init(&e->chainRight, sampleRate);
    brickwall_limiter_init(&e->limiter, -1.0, 80.0, sampleRate);
    powerstarve_init(&e->powerStarve, 0xCE11B00Eu);
    dbcell_chaos_reset(&e->chaos, 0x0C1A0517u);
    dbcell_chaos_set_sample_rate(&e->chaos, sampleRate);

    dbcell_randomize(e, seed);
}

void dbcell_randomize(DbCellEngine *e, unsigned int seed) {
    unsigned int state = seed;

    distroy_chain_randomize_all_restricted(&e->chainLeft, seed, dbcellAllowedTypes, dbcellAllowedTypeCount);
    distroy_chain_randomize_all_restricted(&e->chainRight, seed, dbcellAllowedTypes, dbcellAllowedTypeCount);

    /* Force one slot to always be DISTROY_NOIZ, capped to 33% effective
     * blend (0.66 knob rescale on top of the shared 50% cap), same as
     * db-cell's own randomizeCell. */
    int noizSlotIndex = (int)(dbcell_rand01(&state) * DISTROY_NUM_SLOTS);
    if (noizSlotIndex < 0) noizSlotIndex = 0;
    if (noizSlotIndex > DISTROY_NUM_SLOTS - 1) noizSlotIndex = DISTROY_NUM_SLOTS - 1;
    unsigned int noizStateSnapshotL = state;
    unsigned int noizStateSnapshotR = state;
    distroy_randomize_slot_as_type(&e->chainLeft.slots[noizSlotIndex], DISTROY_NOIZ, &noizStateSnapshotL);
    distroy_randomize_slot_as_type(&e->chainRight.slots[noizSlotIndex], DISTROY_NOIZ, &noizStateSnapshotR);
    e->chainLeft.slots[noizSlotIndex].knob *= 0.66;
    e->chainRight.slots[noizSlotIndex].knob *= 0.66;
    e->noizSlot = noizSlotIndex;
    e->noizBaseKnobL = e->chainLeft.slots[noizSlotIndex].knob;
    e->noizBaseKnobR = e->chainRight.slots[noizSlotIndex].knob;

    dbcell_chaos_reset(&e->chaos, seed ^ 0x0C1A0517u);
    dbcell_chaos_set_sample_rate(&e->chaos, e->sampleRate);
    e->wasNoizDipped = 0;
    e->wasToneLinked = 0;

    const int rev = (dbcell_rand01(&state) < 0.5) ? 0 : 1;
    e->chainLeft.reverse = rev;
    e->chainRight.reverse = rev;

    const double tone = dbcell_rand01(&state);
    e->baseTone = tone;
    distroy_chain_set_master_tone(&e->chainLeft, tone);
    distroy_chain_set_master_tone(&e->chainRight, tone);

    /* Battery never at the boring 100%-healthy pass-through value --
     * same 0.2 floor as db-cell's own verified-against-powerstarve_process
     * reasoning. */
    const double batteryLevel = 0.2 + dbcell_rand01(&state) * 0.8;
    powerstarve_set_amount(&e->powerStarve, batteryLevel);
    dbcell_chaos_set_intensity(&e->chaos, 0.15 + batteryLevel * 0.85);

    for (int gap = 0; gap < DISTROY_NUM_GAPS; gap++) {
        const int phaseOn = dbcell_rand01(&state) < 0.5 ? 1 : 0;
        const int zcOn = dbcell_rand01(&state) < 0.5 ? 1 : 0;
        const double slewMs = dbcell_rand01(&state) * 20.0;
        distroy_chain_set_phase_invert(&e->chainLeft, gap, phaseOn);
        distroy_chain_set_phase_invert(&e->chainRight, gap, phaseOn);
        distroy_chain_set_zc_smooth(&e->chainLeft, gap, zcOn);
        distroy_chain_set_zc_smooth(&e->chainRight, gap, zcOn);
        distroy_chain_set_slew_ms(&e->chainLeft, gap, slewMs);
        distroy_chain_set_slew_ms(&e->chainRight, gap, slewMs);
    }

    e->wetMix = (float)(0.01 + dbcell_rand01(&state) * 0.02);
}

void dbcell_process(DbCellEngine *e, double *l_io, double *r_io) {
    const double inL = *l_io;
    const double inR = *r_io;
    const float wet = e->wetMix;
    const float dry = 1.0f - wet;

    double l = distroy_chain_process(&e->chainLeft, inL);
    double r = distroy_chain_process(&e->chainRight, inR);

    l = powerstarve_process(&e->powerStarve, l, e->sampleRate);
    r = powerstarve_process(&e->powerStarve, r, e->sampleRate);

    brickwall_limiter_process(&e->limiter, &l, &r);

    const double chaosGain = dbcell_chaos_next_sample(&e->chaos);
    l *= chaosGain;
    r *= chaosGain;

    const int isDipped = chaosGain < 0.3;
    if (isDipped) {
        const double noizPull = chaosGain / 0.3;
        e->chainLeft.slots[e->noizSlot].knob = e->noizBaseKnobL * noizPull;
        e->chainRight.slots[e->noizSlot].knob = e->noizBaseKnobR * noizPull;
        e->wasNoizDipped = 1;
    } else if (e->wasNoizDipped) {
        e->chainLeft.slots[e->noizSlot].knob = e->noizBaseKnobL;
        e->chainRight.slots[e->noizSlot].knob = e->noizBaseKnobR;
        e->wasNoizDipped = 0;
    }

    if (dbcell_chaos_is_tone_linked(&e->chaos)) {
        const double toneMod = dbcell_clampd(e->baseTone + dbcell_chaos_get_gain_deviation(&e->chaos) * 0.6, 0.0, 1.0);
        distroy_chain_set_master_tone(&e->chainLeft, toneMod);
        distroy_chain_set_master_tone(&e->chainRight, toneMod);
        e->wasToneLinked = 1;
    } else if (e->wasToneLinked) {
        distroy_chain_set_master_tone(&e->chainLeft, e->baseTone);
        distroy_chain_set_master_tone(&e->chainRight, e->baseTone);
        e->wasToneLinked = 0;
    }

    *l_io = inL * dry + l * wet;
    *r_io = inR * dry + r * wet;
}
