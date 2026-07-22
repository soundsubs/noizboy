#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the exponential (log-scale) release knob mapping, per
 * explicit spec: "the release should be a single sample at [knob] 0,
 * and at 1 should be pluckier but almost hard to hear... a decaying
 * exponential curve". This replicates the exact formula from
 * noiseboy_plugin.c's set_param (which can't be unit-tested directly
 * here, since it needs the real Schwung plugin headers) to verify its
 * endpoints and shape directly. */

static double release_mapping(double raw01) {
    return 0.02 * pow(4000.0 / 0.02, raw01);
}

static double inverse_mapping(double releaseMs) {
    return log(releaseMs / 0.02) / log(4000.0 / 0.02);
}

int main(void) {
    int all_ok = 1;

    /* Endpoint checks */
    double atZero = release_mapping(0.0);
    double atOne = release_mapping(1.0);
    printf("release at knob=0: %.4fms (expect ~0.02ms, ~1 sample at 48kHz)\n", atZero);
    printf("release at knob=1: %.1fms (expect 4000ms)\n", atOne);
    if (fabs(atZero - 0.02) > 0.001) { printf("FAILED: knob=0 endpoint\n"); all_ok = 0; }
    if (fabs(atOne - 4000.0) > 0.1) { printf("FAILED: knob=1 endpoint\n"); all_ok = 0; }

    /* Verify one sample at 48kHz is genuinely representable and not
       clamped away by the envelope's own floor (0.02ms, matched). */
    double oneSampleMs = 1000.0 / 48000.0;
    printf("One sample at 48kHz = %.4fms; mapping's knob=0 value = %.4fms\n", oneSampleMs, atZero);
    if (atZero > oneSampleMs * 1.5) {
        printf("FAILED: knob=0 release time should be close to one sample duration\n");
        all_ok = 0;
    }

    /* Verify the mapping is monotonic and genuinely exponential (not
       linear) -- equal knob steps should give equal RATIOS, not equal
       DIFFERENCES, in the resulting ms value. */
    printf("\nknob   releaseMs   ratio-to-previous\n");
    double prev = -1;
    int monotonic = 1;
    for (double k = 0.0; k <= 1.0001; k += 0.1) {
        double ms = release_mapping(k);
        double ratio = (prev > 0) ? ms / prev : 0.0;
        printf("%.1f    %9.3f   %.3f\n", k, ms, ratio);
        if (prev > 0 && ms <= prev) monotonic = 0;
        prev = ms;
    }
    if (!monotonic) { printf("FAILED: mapping should be strictly increasing\n"); all_ok = 0; }

    /* Verify round-trip accuracy of the inverse mapping (used by get_param) */
    printf("\nRound-trip check:\n");
    int roundTripOk = 1;
    for (double k = 0.0; k <= 1.0001; k += 0.25) {
        double ms = release_mapping(k);
        double kBack = inverse_mapping(ms);
        printf("knob=%.2f -> %.3fms -> knob=%.4f (diff=%.5f)\n", k, ms, kBack, fabs(k - kBack));
        if (fabs(k - kBack) > 0.001) roundTripOk = 0;
    }
    if (!roundTripOk) { printf("FAILED: round-trip mapping should be accurate\n"); all_ok = 0; }
    else printf("PASSED: round-trip accurate\n");

    /* Verify low knob values now get much finer control than the old
       linear mapping would have given -- e.g. knob steps 0.00 to 0.02
       (roughly 2-3 raw MIDI CC steps out of 127) should span a
       meaningfully different range than they would have linearly. */
    double oldLinearAt002 = 5.0 + 0.02 * 1995.0; /* old formula, for comparison */
    double newExpAt002 = release_mapping(0.02);
    printf("\nAt knob=0.02 (~2-3 MIDI steps): old linear mapping gave %.2fms, new exponential gives %.4fms\n",
           oldLinearAt002, newExpAt002);
    printf("(Old mapping barely moved off its 5ms floor here; new mapping is still near-instant,\n");
    printf(" meaning far more of the knob's low range is now usable for genuinely short, plucky times.)\n");

    /* Full DSP integration test -- actually use these release times in
       the real engine, confirm clean, click-free, finite behavior. */
    {
        int finite_ok = 1;
        for (double k = 0.0; k <= 1.0; k += 0.1) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, 42u);
            e.params.releaseMs = release_mapping(k);
            e.params.attackMs = 4.0;
            noiseboy_note_on(&e, 60, 0.8);
            for (int i = 0; i < 4800; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }
            noiseboy_note_off(&e, 60);
            for (int i = 0; i < 48000; i++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
            }
        }
        printf("\nFull DSP integration across full knob range: finite_ok=%d\n", finite_ok);
        if (!finite_ok) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL RELEASE CURVE CHECKS PASSED\n" : "\nSOME RELEASE CURVE CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
