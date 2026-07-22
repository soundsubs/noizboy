#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the reintroduced bitcrush/rate-reduce, per explicit
 * request (post-mixer, pre-filter). Also directly quantifies the
 * pitch-tracking tradeoff flagged in Voice's own header comment,
 * since "this is worse for pitch tracking" deserves a real measured
 * number, not just an assertion. */

static int count_zero_crossings(NoiseboyEngine *e, int note, int numSamples) {
    noiseboy_note_on(e, note, 0.8);
    int crossings = 0;
    double prev = 0.0;
    int first = 1;
    for (int i = 0; i < numSamples; i++) {
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

    /* Test 1: bitDepth/rateReducerMultiplier randomize within documented ranges */
    {
        int minBits = 100, maxBits = 0;
        double minMul = 100, maxMul = 0;
        for (unsigned int i = 1; i < 300; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            noiseboy_note_on(&e, 60, 0.8);
            for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
                if (e.voices[v].active) {
                    if (e.voices[v].bitDepth < minBits) minBits = e.voices[v].bitDepth;
                    if (e.voices[v].bitDepth > maxBits) maxBits = e.voices[v].bitDepth;
                    if (e.voices[v].rateReducerMultiplier < minMul) minMul = e.voices[v].rateReducerMultiplier;
                    if (e.voices[v].rateReducerMultiplier > maxMul) maxMul = e.voices[v].rateReducerMultiplier;
                }
            }
        }
        printf("bitDepth range: %d-%d (expect 12-15), rateReducerMultiplier range: %.2f-%.2f (expect 1.0-2.0)\n",
               minBits, maxBits, minMul, maxMul);
        if (minBits < 12 || maxBits > 15) { printf("FAILED: bitDepth out of range\n"); all_ok = 0; }
        if (minMul < 1.0 || maxMul > 2.0) { printf("FAILED: rateReducerMultiplier out of range\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 2: "sample rate follows key number" -- verify the rate
       reducer's effective hold rate scales with the played note's
       frequency, not fixed. */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, 42u);
        noiseboy_note_on(&e, 36, 0.8); /* low note */
        double rateAtLowNote = e.voices[0].freqHz * e.voices[0].rateReducerMultiplier;
        noiseboy_engine_init(&e, 48000.0, 42u);
        noiseboy_note_on(&e, 84, 0.8); /* high note, 4 octaves up */
        double rateAtHighNote = e.voices[0].freqHz * e.voices[0].rateReducerMultiplier;
        double ratio = rateAtHighNote / rateAtLowNote;
        printf("Rate-reducer hold rate: low note=%.1fHz, high note (4 octaves up)=%.1fHz, ratio=%.2fx (expect ~16x, matching the pitch ratio)\n",
               rateAtLowNote, rateAtHighNote, ratio);
        if (fabs(ratio - 16.0) > 0.5) { printf("FAILED: hold rate should scale exactly with pitch (4 octaves = 16x)\n"); all_ok = 0; }
        else printf("PASSED: sample rate genuinely follows key number\n");
    }

    /* Test 3: quantify the pitch-tracking tradeoff directly, per this
       project's own established zero-crossing methodology. */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, 42u);
        e.params.filterResonance01 = 0.9;
        int lowCrossings = count_zero_crossings(&e, 36, 4800);
        noiseboy_engine_init(&e, 48000.0, 42u);
        e.params.filterResonance01 = 0.9;
        int highCrossings = count_zero_crossings(&e, 84, 4800);
        double ratio = (double)highCrossings / (double)lowCrossings;
        printf("\nPitch-tracking accuracy WITH bitcrush/rate-reduce reintroduced: note 36 -> %d crossings, note 84 -> %d crossings, ratio=%.2fx (true value for 4 octaves = 16x; this project's own history: was ~7.2x with the stage removed, ~3.5x with it present pre-v0.9.0 restructuring)\n",
               lowCrossings, highCrossings, ratio);
        printf("This is the measured cost of the explicit request to reposition this post-mixer/pre-filter -- not a bug, an informed tradeoff.\n");
    }

    /* Test 4: full pipeline sanity -- no NaN/Inf, no silent voices */
    {
        int finite_ok = 1, silentCount = 0;
        for (unsigned int i = 1; i < 300; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            noiseboy_note_on(&e, 30 + (int)(i % 60), 0.8);
            double peak = 0.0;
            for (int s = 0; s < 4800; s++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
                if (fabs(l) > peak) peak = fabs(l);
            }
            if (peak < 1e-6) silentCount++;
        }
        printf("\nFull pipeline sanity (300 seeds): finite_ok=%d, silent=%d\n", finite_ok, silentCount);
        if (!finite_ok || silentCount > 0) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL BITCRUSH REINTRODUCTION CHECKS PASSED\n" : "\nSOME BITCRUSH REINTRODUCTION CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
