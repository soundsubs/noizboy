#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

int main(void) {
    int all_ok = 1;
    const unsigned int seed = 7919u; /* verified: 2-layer recipe, layer 1 has nonzero detuneCents */

    /* Test 1: Detune=0 should collapse to mono (L == R exactly) */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, seed);
        e.params.detuneSpread01 = 0.0;
        noiseboy_note_on(&e, 60, 0.8);
        noiseboy_note_on(&e, 64, 0.8);
        double maxDiff = 0.0;
        int finite_ok = 1;
        for (int i = 0; i < 48000; i++) {
            double l, r;
            noiseboy_process_stereo(&e, &l, &r);
            if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
            double diff = fabs(l - r);
            if (diff > maxDiff) maxDiff = diff;
        }
        printf("Detune=0: max L/R difference = %.8f (should be ~0, mono)\n", maxDiff);
        if (!finite_ok) { printf("  FAILED: non-finite\n"); all_ok = 0; }
        if (maxDiff > 1e-9) { printf("  FAILED: should be exactly mono at Detune=0\n"); all_ok = 0; }
    }

    /* Test 2: Detune=1 should produce a real, substantially LARGER stereo difference than Detune=0 */
    double diffAtZero, diffAtOne;
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, seed);
        e.params.detuneSpread01 = 0.0;
        noiseboy_note_on(&e, 60, 0.8);
        noiseboy_note_on(&e, 64, 0.8);
        double maxDiff = 0.0;
        for (int i = 0; i < 48000; i++) {
            double l, r;
            noiseboy_process_stereo(&e, &l, &r);
            double diff = fabs(l - r);
            if (diff > maxDiff) maxDiff = diff;
        }
        diffAtZero = maxDiff;
    }
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, seed);
        e.params.detuneSpread01 = 1.0;
        noiseboy_note_on(&e, 60, 0.8);
        noiseboy_note_on(&e, 64, 0.8);
        double maxDiff = 0.0;
        int finite_ok = 1;
        for (int i = 0; i < 48000; i++) {
            double l, r;
            noiseboy_process_stereo(&e, &l, &r);
            if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
            double diff = fabs(l - r);
            if (diff > maxDiff) maxDiff = diff;
        }
        diffAtOne = maxDiff;
        if (!finite_ok) { printf("Test 2 FAILED: non-finite\n"); all_ok = 0; }
    }
    printf("Max L/R diff -- Detune=0: %.6f, Detune=1: %.6f\n", diffAtZero, diffAtOne);
    if (diffAtOne < diffAtZero * 5.0 || diffAtOne < 0.01) {
        printf("  FAILED: Detune=1 should show a substantially larger stereo spread than Detune=0\n");
        all_ok = 0;
    } else {
        printf("  PASSED: stereo spread scales with Detune as expected\n");
    }

    /* Test 3: Karplus layer should have a TIME-VARYING pan (auto-pan at AM rate),
       not a static one -- verify by checking the L/R balance actually flips sign over time. */
    {
        NoiseboyEngine e;
        unsigned int karplusSeed = 0;
        for (unsigned int s = 1; s < 5000; s++) {
            noiseboy_engine_init(&e, 48000.0, s * 7919u);
            int allKarplus = 1;
            for (int i = 0; i < e.numRecipeLayers; i++) {
                if (e.recipe[i].type != LAYER_KARPLUS_STRONG) allKarplus = 0;
            }
            if (allKarplus) { karplusSeed = s; break; }
        }
        if (karplusSeed == 0) {
            printf("Could not find an all-Karplus recipe for auto-pan test, skipping\n");
        } else {
            noiseboy_engine_init(&e, 48000.0, karplusSeed * 7919u);
            e.params.detuneSpread01 = 1.0;
            e.params.amRateHz = 4.0; /* a few Hz, easy to observe over 1 second */
            noiseboy_note_on(&e, 60, 0.8);

            int wentPositive = 0, wentNegative = 0;
            int finite_ok = 1;
            for (int i = 0; i < 48000; i++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
                double balance = l - r;
                if (balance > 0.02) wentPositive = 1;
                if (balance < -0.02) wentNegative = 1;
            }
            printf("Karplus auto-pan (seed %u): wentPositive=%d wentNegative=%d (both should be 1 -- pan oscillates)\n",
                   karplusSeed, wentPositive, wentNegative);
            if (!finite_ok) { printf("  FAILED: non-finite\n"); all_ok = 0; }
            if (!wentPositive || !wentNegative) { printf("  FAILED: pan should swing both directions over 1 second at 4Hz AM rate\n"); all_ok = 0; }
        }
    }

    /* Test 4: sweep detuneSpread01 across its full range, confirm always finite */
    {
        int finite_ok = 1;
        for (int step = 0; step <= 10; step++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, seed);
            e.params.detuneSpread01 = step / 10.0;
            noiseboy_note_on(&e, 60, 0.8);
            for (int i = 0; i < 4800; i++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
            }
        }
        printf("Detune sweep (0.0-1.0, 11 steps): finite_ok=%d\n", finite_ok);
        if (!finite_ok) all_ok = 0;
    }

    printf(all_ok ? "\nALL STEREO PAN CHECKS PASSED\n" : "\nSOME STEREO PAN CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
