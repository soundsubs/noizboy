#ifndef DBCELL_DSP_H
#define DBCELL_DSP_H

/* -----------------------------------------------------------------------
 * DBCELL -- C port of DISTROYBOY-CELL ("db-cell"), the JUCE VST3
 * plugin built earlier in this same project family, for use as an
 * always-on post-processing stage baked directly into NOISEBOY's
 * output ("add db-cell on the output, always"). Ported from
 * db-cell's Source/PluginProcessor.h/.cpp (C++) to plain C so it can
 * link directly against NOISEBOY's C-based Schwung module -- the
 * shared distroy_dsp.c/h core underneath both needed no porting (it
 * was already plain C), only db-cell's own ChaosModulator and
 * randomize/process logic did.
 *
 * Faithful port, not a redesign: same 26-type restricted pedal pool,
 * same forced-Noiz-slot-at-33%-cap behaviour, same battery-never-at-
 * 100% floor, same combined legacy+hybrid chaos modulator (always
 * combined now, matching db-cell's own "Combine Modes" permanent
 * decision), same brickwall limiter placement, same 1%-3% wet/dry
 * blend. The one thing NOT ported is db-cell's own UI (wet-trim
 * slider, tube/bulb indicators, Combine Modes toggle) -- NOISEBOY has
 * no equivalent UI surface for those, and "always on" per this
 * request means there's nothing for a toggle to do anyway. The manual
 * wet-trim control is also omitted for the same reason (no UI to put
 * it on) -- db-cell's own randomized 1%-3% wet amount is used as-is.
 * ---------------------------------------------------------------------*/

#include "distroy_dsp.h"

typedef struct {
    unsigned int state;
    double currentGain, startGain, targetGain;
    int rampSamples, samplesIntoRamp;
    int toneLinkedThisSegment;
    int legacyToneLinked;
    int hybridToneLinked;
    int popStage;
    double intensity;

    /* Hybrid-mode state. */
    double sampleRate;
    double ouValue;
    double eventMultiplier, eventStart, eventTarget;
    int eventRampSamples, eventSamplesIntoRamp;
    int eventPopStage;
    int samplesUntilNextEventCheck;
} DbCellChaosModulator;

void dbcell_chaos_reset(DbCellChaosModulator *c, unsigned int seed);
void dbcell_chaos_set_intensity(DbCellChaosModulator *c, double intensity01);
void dbcell_chaos_set_sample_rate(DbCellChaosModulator *c, double sampleRate);
/* Always runs both legacy and hybrid modes and blends them (a 50/50
 * average, not a multiply), matching db-cell's own permanent "Combine
 * Modes" decision -- there's no toggle here since NOISEBOY has no UI
 * surface for one and "always on" was the explicit request anyway. */
double dbcell_chaos_next_sample(DbCellChaosModulator *c);
int dbcell_chaos_is_tone_linked(const DbCellChaosModulator *c);
double dbcell_chaos_get_gain_deviation(const DbCellChaosModulator *c);

typedef struct {
    DistroyChain chainLeft;
    DistroyChain chainRight;
    PowerStarve powerStarve;
    BrickwallLimiter limiter;
    DbCellChaosModulator chaos;
    double sampleRate;

    int noizSlot;
    double noizBaseKnobL, noizBaseKnobR;
    double baseTone;
    int wasNoizDipped;
    int wasToneLinked;

    float wetMix;
    unsigned int randomizeSeedCounter;
} DbCellEngine;

void dbcell_engine_init(DbCellEngine *e, double sampleRate, unsigned int seed);
/* Re-rolls the full recipe -- same thing db-cell's own randomize
 * button does. NOISEBOY wires this to fire alongside its own
 * noiseboy_randomize_recipe(), so one "Randomize" gesture refreshes
 * both layers of the sound together. */
void dbcell_randomize(DbCellEngine *e, unsigned int seed);
/* Processes one stereo sample pair in place. Takes NOISEBOY's mono
 * output (duplicated to both channels by the caller before this call)
 * as the "dry" input -- db-cell's independent per-channel chain state
 * will naturally diverge the two channels over time even though they
 * start identical each sample, giving the combined output some real
 * stereo width NOISEBOY's own mono synthesis doesn't have on its
 * own. */
void dbcell_process(DbCellEngine *e, double *l, double *r);

#endif /* DBCELL_DSP_H */
