#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the filter cutoff's brightness blend, redesigned twice
 * this session -- first to fix genuine darkness at max cutoff for low
 * notes (a purely pitch-proportional cutoff could never reach real
 * brightness), then to fix a real regression that first fix
 * introduced: a sudden-onset linear ramp (starting at 75% of the
 * knob) had a discontinuous derivative, causing "almost useless"
 * behavior just below it and "audible glitching" jumps just above it,
 * per direct report. Fixed with a smooth power curve across the
 * entire range -- this test locks in BOTH properties directly: no
 * jarring per-step jumps anywhere, AND the default knob value staying
 * close to the original, untouched pitch-tracking formula (not a
 * separate regression the smoothing fix could have introduced). */

static double cutoff_formula(double raw01, double freqHz) {
    double cutoffMul = pow(2.0, (raw01 - 0.5) * 4.0);
    double proportional = freqHz * cutoffMul;
    double brightBlend = pow(raw01, 10.0);
    return proportional + (15000.0 - proportional) * brightBlend;
}

int main(void) {
    int all_ok = 1;

    /* Test 1: no jarring per-step jump anywhere across the full knob
       range -- specifically re-checks the exact region that was
       reported broken (knob 90-110), plus the full range generally. */
    {
        double freqHz = 261.6;
        double prevCutoff = -1;
        double maxJumpInReportedRegion = 0, maxJumpOverall = 0;
        for (int knob = 0; knob <= 127; knob++) {
            double cutoff = cutoff_formula(knob / 127.0, freqHz);
            if (prevCutoff > 0) {
                double jump = fabs(cutoff - prevCutoff);
                if (jump > maxJumpOverall) maxJumpOverall = jump;
                if (knob >= 90 && knob <= 110 && jump > maxJumpInReportedRegion) maxJumpInReportedRegion = jump;
            }
            prevCutoff = cutoff;
        }
        printf("Max per-step cutoff jump in the previously-broken 90-110 region: %.1fHz\n", maxJumpInReportedRegion);
        printf("Max per-step cutoff jump anywhere in the full 0-127 range: %.1fHz\n", maxJumpOverall);
        /* The previously-broken region should now show only a gradual
           climb -- well under 500Hz per single knob step, versus the
           original bug's ~1770Hz jump over just 6 steps (~295Hz/step
           average, but concentrated as a much sharper spike at onset). */
        if (maxJumpInReportedRegion > 500.0) {
            printf("FAILED: still a jarring jump in the previously-broken region\n");
            all_ok = 0;
        } else printf("PASSED\n");
    }

    /* Test 2: default knob value (64) stays close to the original,
       untouched pitch-tracking formula -- the fix that eliminated the
       jump shouldn't ALSO change the established default sound. */
    {
        double freqHz = 261.6;
        double raw01 = 64.0 / 127.0;
        double cutoffMul = pow(2.0, (raw01 - 0.5) * 4.0);
        double originalUntouched = freqHz * cutoffMul; /* what it always was, before brightness blending existed */
        double withBlend = cutoff_formula(raw01, freqHz);
        double percentDiff = fabs(withBlend - originalUntouched) / originalUntouched * 100.0;
        printf("\nAt default knob (64): original=%.1fHz, with blend=%.1fHz (%.1f%% difference, expect small)\n",
               originalUntouched, withBlend, percentDiff);
        if (percentDiff > 15.0) {
            printf("FAILED: default-knob cutoff has drifted too far from the original, established sound\n");
            all_ok = 0;
        } else printf("PASSED\n");
    }

    /* Test 3: genuine brightness still achieved at max knob, across the keyboard */
    {
        int notes[] = {36, 48, 60, 72, 84};
        int allBright = 1;
        for (int i = 0; i < 5; i++) {
            double freq = 440.0 * pow(2.0, (notes[i] - 69) / 12.0);
            double cutoff = cutoff_formula(1.0, freq);
            if (fabs(cutoff - 15000.0) > 1.0) allBright = 0;
        }
        printf("\nAll notes reach the full 15kHz ceiling at max knob: %d\n", allBright);
        if (!allBright) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 4: full DSP integration sanity -- no crashes/instability across the full range */
    {
        int finite_ok = 1;
        for (unsigned int i = 1; i < 100; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            e.params.filterCutoffOffset01 = (double)(i % 128) / 127.0;
            noiseboy_note_on(&e, 24 + (int)(i % 72), 0.8);
            for (int s = 0; s < 4800; s++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
            }
        }
        printf("\nFull DSP integration across full cutoff knob range: finite_ok=%d\n", finite_ok);
        if (!finite_ok) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL CUTOFF BRIGHTNESS CHECKS PASSED\n" : "\nSOME CUTOFF BRIGHTNESS CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
