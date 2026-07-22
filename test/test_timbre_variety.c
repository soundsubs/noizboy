#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the timbre-character variety fix, per direct report ("much
 * less randomness to the Randomize" after last session's resonance-
 * evenness fix removed filter-type variety). See timbreCharacterMul's
 * own header comment for why this ended up varying cutoff, not
 * resonance -- resonance was tried first and measured unreliable. */

/* Zero-crossing rate, not RMS -- see this file's own comment below
   for why RMS is the wrong metric here (tape saturation runs a
   compressor on the entire mix, unconditionally, which normalizes
   overall loudness and specifically works against detecting an
   amplitude-based difference no matter where upstream it originates).
   Zero-crossing rate tracks dominant frequency content instead, which
   survives that compression intact. */
static int measure_zero_crossings(NoiseboyEngine *e, int note) {
    noiseboy_note_on(e, note, 0.8);
    for (int i = 0; i < 2400; i++) { double l, r; noiseboy_process_stereo(e, &l, &r); }
    int crossings = 0;
    double prev = 0.0;
    int first = 1;
    for (int i = 0; i < 9600; i++) {
        double l, r;
        noiseboy_process_stereo(e, &l, &r);
        if (!first && ((prev < 0 && l >= 0) || (prev >= 0 && l < 0))) crossings++;
        prev = l;
        first = 0;
    }
    return crossings;
}

int main(void) {
    int all_ok = 1;

    /* Confirm timbreCharacterMul varies within the documented bound. */
    double minMul = 2.0, maxMul = 0.0;
    for (unsigned int i = 1; i < 500; i++) {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, i * 7919u);
        if (e.timbreCharacterMul < minMul) minMul = e.timbreCharacterMul;
        if (e.timbreCharacterMul > maxMul) maxMul = e.timbreCharacterMul;
        if (e.timbreCharacterMul < 0.84 || e.timbreCharacterMul > 1.16) {
            printf("FAILED: timbreCharacterMul out of bounds: %.4f (seed %u)\n", e.timbreCharacterMul, i);
            all_ok = 0;
        }
    }
    printf("timbreCharacterMul range across 500 seeds: %.4f to %.4f (expect close to 0.85-1.15)\n", minMul, maxMul);

    /* Isolated comparison: same seed/recipe, only timbreCharacterMul differs. */
    unsigned int seed = 0;
    for (unsigned int i = 1; i < 1000; i++) {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, i * 7919u);
        if (e.numRecipeLayers == 1 && e.recipe[0].type == LAYER_FILTERED_NOISE) { seed = i; break; }
    }
    if (seed == 0) { printf("Could not find suitable seed\n"); return 1; }

    NoiseboyEngine eLow, eHigh;
    noiseboy_engine_init(&eLow, 48000.0, seed * 7919u);
    noiseboy_engine_init(&eHigh, 48000.0, seed * 7919u);
    eLow.timbreCharacterMul = 0.85;
    eHigh.timbreCharacterMul = 1.15;

    int crossLow = measure_zero_crossings(&eLow, 60);
    int crossHigh = measure_zero_crossings(&eHigh, 60);
    double pctDiff = fabs((double)(crossHigh - crossLow)) / crossLow * 100.0;
    printf("Isolated comparison: mul=0.85 -> %d crossings, mul=1.15 -> %d crossings (%.1f%% difference)\n",
           crossLow, crossHigh, pctDiff);
    if (pctDiff < 10.0) {
        printf("FAILED: expected a measurable, reliable spectral difference\n");
        all_ok = 0;
    } else {
        printf("PASSED: measurable spectral difference confirmed\n");
    }

    /* Full pipeline sanity check -- no NaN/Inf, no silent voices, across many seeds */
    {
        int finite_ok = 1, silentCount = 0;
        for (unsigned int i = 1; i < 300; i++) {
            NoiseboyEngine e2;
            noiseboy_engine_init(&e2, 48000.0, i * 7919u);
            noiseboy_note_on(&e2, 40 + (int)(i % 48), 0.8);
            double peak = 0.0;
            for (int s = 0; s < 4800; s++) {
                double l, r;
                noiseboy_process_stereo(&e2, &l, &r);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
                if (fabs(l) > peak) peak = fabs(l);
            }
            if (peak < 1e-6) silentCount++;
        }
        printf("Full pipeline sanity (300 seeds): finite_ok=%d, silent=%d\n", finite_ok, silentCount);
        if (!finite_ok || silentCount > 0) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL TIMBRE VARIETY CHECKS PASSED\n" : "\nSOME TIMBRE VARIETY CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
