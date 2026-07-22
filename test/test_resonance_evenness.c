#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the fix for a real reported bug: "some notes on the pads
 * I play are too resonant, moreso than notes around it." Two root
 * causes found and fixed:
 * 1. Filter TYPE was randomized per note (Moog/Korg35LP 50/50), and
 *    Korg35LP's resonance formula was measured to be up to ~79x
 *    weaker than Moog's at the same knob value -- adjacent notes
 *    landing on different filter types produced wildly inconsistent
 *    resonance. Fixed: always Moog now.
 * 2. Even within Moog alone, resonant peak varies substantially with
 *    cutoff frequency for a fixed resonance knob value (a real
 *    property of this filter topology). Fixed: frequency-dependent
 *    compensation boost for lower notes, capped at a measured
 *    physical ceiling. */

static double measure_peak(double freq, double resonance01) {
    MoogLadder f;
    moog_ladder_init(&f, 48000.0);
    moog_ladder_set(&f, freq, resonance01, 0.2);
    double peak = 0.0;
    double y = moog_ladder_process(&f, 1.0);
    if (fabs(y) > peak) peak = fabs(y);
    for (int i = 0; i < 4800; i++) {
        y = moog_ladder_process(&f, 0.0);
        if (fabs(y) > peak) peak = fabs(y);
    }
    return peak;
}

int main(void) {
    int all_ok = 1;

    /* Test 1: filter type is never randomly Korg35LP for the pitch
     * filter anymore -- check across many seeds/notes. */
    {
        int allMoog = 1;
        for (unsigned int i = 1; i < 200; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            noiseboy_note_on(&e, 60, 0.8);
            for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
                if (e.voices[v].active && e.voices[v].pitchFilterKind != FILTER_MOOG) {
                    allMoog = 0;
                }
            }
        }
        printf("Pitch filter always Moog across 200 seeds: %d\n", allMoog);
        if (!allMoog) { printf("  FAILED\n"); all_ok = 0; }
        else printf("  PASSED\n");
    }

    /* Test 2: resonance evenness -- with the same knob value, the
     * spread between the lowest and highest measured peak across the
     * playable range should be substantially narrower than the
     * original ~3.4x (0.28 to 0.94). Replicate the actual compensation
     * formula used in noiseboy_process_stereo. */
    {
        double baseResonance = 0.82;
        double minPeak = 2.0, maxPeak = 0.0;
        int finite_ok = 1;
        for (int note = 24; note <= 96; note += 3) {
            double freq = 440.0 * pow(2.0, (note - 69) / 12.0);
            double boostMul = 1.0 + fmax(0.0, log2(1000.0 / freq)) * 0.5;
            double resonance = fmin(baseResonance * boostMul, 2.0);
            double peak = measure_peak(freq, resonance);
            if (isnan(peak) || isinf(peak)) finite_ok = 0;
            if (peak < minPeak) minPeak = peak;
            if (peak > maxPeak) maxPeak = peak;
        }
        double ratio = maxPeak / minPeak;
        printf("Resonance evenness: min=%.4f max=%.4f ratio=%.2fx (was ~3.4x uncompensated)\n", minPeak, maxPeak, ratio);
        if (!finite_ok) { printf("  FAILED: non-finite\n"); all_ok = 0; }
        if (ratio > 2.5) { printf("  FAILED: spread should be substantially narrower than before\n"); all_ok = 0; }
        else printf("  PASSED\n");
    }

    printf(all_ok ? "\nALL RESONANCE EVENNESS CHECKS PASSED\n" : "\nSOME RESONANCE EVENNESS CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
