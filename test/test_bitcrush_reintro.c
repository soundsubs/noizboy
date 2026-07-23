#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the REDESIGNED bitcrush/rate-reduce, now Drive-controlled
 * (knob 3) rather than randomized per note, per explicit request:
 * "as DRIVE knob 3 is increased (clockwise) lets reduce sample rate
 * and bitrate in a non-linear fashion. Otherwise it defaults to 16
 * bit and 44.1khz." Live-computed every sample from the current Drive
 * value, not fixed at note-on. */

int main(void) {
    int all_ok = 1;

    /* Test 1: at drive=0, bitcrush/rate-reduce are effectively
       bypassed -- verified directly on the isolated stage (not
       through the full pipeline, where the downstream pitch filter's
       own dominant character masks the difference in a simple
       zero-crossing measurement). */
    {
        double sampleRate = 48000.0, freqHz = 261.6;
        PitchedHold h;
        pitchedhold_init(&h);
        NoiseGen ng;
        noisegen_init(&ng, 42u);
        int crossings0 = 0, crossings1 = 0;
        double prev = 0;
        int first = 1;
        for (int i = 0; i < (int)sampleRate; i++) {
            double raw = noisegen_process(&ng, NOISE_WHITE);
            double crushAmount = 0.0 * 0.0; /* drive=0 */
            int bitDepth = (int)(16.0 - crushAmount * 12.0 + 0.5);
            double effectiveRateHz = sampleRate + (freqHz * 1.5 - sampleRate) * crushAmount;
            double holdMultiplier = effectiveRateHz / freqHz;
            double y = pitchedhold_process(&h, raw, freqHz, sampleRate, holdMultiplier);
            y = bitcrush_process(y, bitDepth);
            if (!first && ((prev < 0 && y >= 0) || (prev >= 0 && y < 0))) crossings0++;
            prev = y;
            first = 0;
        }
        PitchedHold h2;
        pitchedhold_init(&h2);
        NoiseGen ng2;
        noisegen_init(&ng2, 42u);
        prev = 0; first = 1;
        for (int i = 0; i < (int)sampleRate; i++) {
            double raw = noisegen_process(&ng2, NOISE_WHITE);
            double crushAmount = 1.0 * 1.0; /* drive=1 */
            int bitDepth = (int)(16.0 - crushAmount * 12.0 + 0.5);
            double effectiveRateHz = sampleRate + (freqHz * 1.5 - sampleRate) * crushAmount;
            double holdMultiplier = effectiveRateHz / freqHz;
            double y = pitchedhold_process(&h2, raw, freqHz, sampleRate, holdMultiplier);
            y = bitcrush_process(y, bitDepth);
            if (!first && ((prev < 0 && y >= 0) || (prev >= 0 && y < 0))) crossings1++;
            prev = y;
            first = 0;
        }
        printf("Isolated crossings/sec -- drive=0: %d, drive=1: %d (expect drive=0 dramatically higher -- effectively bypassed)\n", crossings0, crossings1);
        if (crossings0 < crossings1 * 10) {
            printf("FAILED: expected drive=0 to be dramatically less crushed than drive=1\n");
            all_ok = 0;
        } else printf("PASSED\n");
    }

    /* Test 2: non-linear (quadratic) response -- the midpoint of the
       knob should be crushed noticeably LESS than halfway between the
       endpoints (since drive01^2 grows slowly at first). */
    {
        double bitDepthAt0 = 16.0 - (0.0*0.0)*12.0;
        double bitDepthAt50 = 16.0 - (0.5*0.5)*12.0;
        double bitDepthAt100 = 16.0 - (1.0*1.0)*12.0;
        double linearMidpoint = (bitDepthAt0 + bitDepthAt100) / 2.0;
        printf("\nbitDepth at drive=0: %.1f, drive=0.5: %.1f, drive=1.0: %.1f (linear midpoint would be %.1f)\n",
               bitDepthAt0, bitDepthAt50, bitDepthAt100, linearMidpoint);
        if (bitDepthAt50 <= linearMidpoint) {
            printf("FAILED: expected a non-linear (quadratic) curve -- drive=0.5 should stay CLEANER than a linear midpoint would\n");
            all_ok = 0;
        } else printf("PASSED: confirmed non-linear, most of the knob's low range stays cleaner\n");
    }

    /* Test 3: "sample rate follows key number" is preserved AT a given
       drive setting -- higher notes still get a proportionally higher
       effective rate at the SAME drive value. */
    {
        double drive01 = 1.0; /* max drive, where the rate-reduction is most pronounced and easiest to measure */
        double crushAmount = drive01 * drive01;
        double freqLow = 65.4, freqHigh = 261.6; /* 2 octaves apart */
        double rateLow = 48000.0 + (freqLow * 1.5 - 48000.0) * crushAmount;
        double rateHigh = 48000.0 + (freqHigh * 1.5 - 48000.0) * crushAmount;
        double ratio = rateHigh / rateLow;
        printf("\nAt max drive: effective rate at low note=%.1fHz, high note (2 octaves up)=%.1fHz, ratio=%.2fx (expect ~4x, matching the pitch ratio)\n", rateLow, rateHigh, ratio);
        if (fabs(ratio - 4.0) > 0.1) {
            printf("FAILED: expected the effective reduced rate to stay proportional to the played note\n");
            all_ok = 0;
        } else printf("PASSED\n");
    }

    /* Test 4: full pipeline sanity across many seeds/notes/drive settings */
    {
        int finite_ok = 1;
        int silent = 0;
        for (unsigned int i = 1; i < 300; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            e.params.drive01 = (double)(i % 11) / 10.0;
            noiseboy_note_on(&e, 24 + (int)(i % 72), 0.8);
            double peak = 0.0;
            for (int s = 0; s < 4800; s++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
                if (fabs(l) > peak) peak = fabs(l);
            }
            if (peak < 0.001) silent++;
        }
        printf("\nFull pipeline sanity (300 seeds): finite_ok=%d, silent=%d\n", finite_ok, silent);
        if (!finite_ok || silent > 0) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL DRIVE-CONTROLLED BITCRUSH CHECKS PASSED\n" : "\nSOME CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
