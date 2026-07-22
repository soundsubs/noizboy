#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies Mellotron-style tape wobble, per explicit request:
 * "modulate the filter frequency, resonance, and output level by
 * noise, but only about 1%-5% so that it's not overwhelming and
 * disturbs pitch too much." */

int main(void) {
    int all_ok = 1;

    /* Test 1: TapeWobble output stays bounded within [-1, 1] */
    {
        TapeWobble w;
        tape_wobble_init(&w, 42u);
        double minVal = 2.0, maxVal = -2.0;
        for (int i = 0; i < 480000; i++) { /* 10s */
            double v = tape_wobble_process(&w, 1.0, 48000.0);
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
        }
        printf("TapeWobble range over 10s: %.4f to %.4f (expect within [-1, 1])\n", minVal, maxVal);
        if (minVal < -1.001 || maxVal > 1.001) { printf("FAILED: wobble exceeded expected bounds\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 2: wobble changes SLOWLY, not sample-to-sample -- verify
       max per-sample change is small RELATIVE to the signal's own
       observed amplitude range (a scale-invariant check -- absolute
       jump size naturally scales with amplitude, so comparing against
       a fixed absolute threshold isn't meaningful on its own; a true
       audio-rate signal would show large jumps RELATIVE to its own
       range, a slow wobble should not). */
    {
        TapeWobble w;
        tape_wobble_init(&w, 42u);
        double prev = tape_wobble_process(&w, 1.0, 48000.0);
        double maxJump = 0.0, minVal = prev, maxVal = prev;
        for (int i = 0; i < 48000; i++) {
            double v = tape_wobble_process(&w, 1.0, 48000.0);
            double jump = fabs(v - prev);
            if (jump > maxJump) maxJump = jump;
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
            prev = v;
        }
        double range = maxVal - minVal;
        double relativeJump = (range > 1e-9) ? maxJump / range : 0.0;
        printf("\nMax per-sample change over 1s at 1.0Hz wobble rate: %.6f, observed range: %.4f, relative jump: %.4f%% (expect small -- a slow wander, not audio-rate texture)\n",
               maxJump, range, relativeJump * 100.0);
        if (relativeJump > 0.02) { printf("FAILED: expected a slow-changing signal relative to its own range, not audio-rate jumps\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 3: mellotronDepth01 randomizes within 1%-5% */
    {
        double minDepth = 1.0, maxDepth = 0.0;
        for (unsigned int i = 1; i < 300; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            if (e.mellotronDepth01 < minDepth) minDepth = e.mellotronDepth01;
            if (e.mellotronDepth01 > maxDepth) maxDepth = e.mellotronDepth01;
            if (e.mellotronDepth01 < 0.0099 || e.mellotronDepth01 > 0.0501) {
                printf("FAILED: mellotronDepth01 out of 1%%-5%% range (seed %u, got %.4f)\n", i, e.mellotronDepth01);
                all_ok = 0;
            }
        }
        printf("\nmellotronDepth01 range across 300 seeds: %.4f to %.4f (expect close to 0.01-0.05)\n", minDepth, maxDepth);
        if (maxDepth - minDepth < 0.02) { printf("FAILED: not enough variety\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 4: pitch is NOT disturbed too much -- verify the wobble's
       effect on actual pitch (via zero-crossing rate as a proxy)
       stays small, even at the maximum 5% depth. */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, 42u);
        e.mellotronDepth01 = 0.05; /* max depth */
        noiseboy_note_on(&e, 69, 0.8); /* A4, 440Hz */
        for (int i = 0; i < 4800; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }

        /* Measure zero-crossing rate over several successive 100ms
           windows -- if pitch were being "disturbed too much", these
           would vary wildly between windows. */
        int crossingCounts[5];
        for (int w = 0; w < 5; w++) {
            int crossings = 0;
            double prev = 0.0;
            int first = 1;
            for (int i = 0; i < 4800; i++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (!first && ((prev < 0 && l >= 0) || (prev >= 0 && l < 0))) crossings++;
                prev = l;
                first = 0;
            }
            crossingCounts[w] = crossings;
        }
        int minC = crossingCounts[0], maxC = crossingCounts[0];
        for (int w = 1; w < 5; w++) {
            if (crossingCounts[w] < minC) minC = crossingCounts[w];
            if (crossingCounts[w] > maxC) maxC = crossingCounts[w];
        }
        printf("\nZero-crossing counts across 5 windows at max wobble depth (5%%): %d %d %d %d %d (min=%d max=%d)\n",
               crossingCounts[0], crossingCounts[1], crossingCounts[2], crossingCounts[3], crossingCounts[4], minC, maxC);
        double variationPct = (double)(maxC - minC) / (double)minC * 100.0;
        printf("Variation: %.1f%% (expect modest -- pitch shouldn't swing wildly even at max wobble depth)\n", variationPct);
        if (variationPct > 25.0) {
            printf("FAILED: pitch variation seems too large for a subtle 1-5%% wobble\n");
            all_ok = 0;
        } else {
            printf("PASSED\n");
        }
    }

    /* Test 5: full pipeline sanity */
    {
        int finite_ok = 1;
        for (unsigned int i = 1; i < 200; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            noiseboy_note_on(&e, 30 + (int)(i % 60), 0.8);
            for (int s = 0; s < 4800; s++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
            }
        }
        printf("\nFull pipeline sanity across 200 seeds: %d\n", finite_ok);
        if (!finite_ok) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL MELLOTRON WOBBLE CHECKS PASSED\n" : "\nSOME MELLOTRON WOBBLE CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
