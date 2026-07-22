#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies Karplus decay variation's CORE LOGIC directly via
 * karplus_pluck/karplus_process (sustainAmount=0, matching a released
 * note, avoiding the sustain-feed confound that masked everything in
 * an earlier version of this test -- sustain feed keeps injecting
 * energy while a note is held, completely independent of damping,
 * so testing with the note held measures the wrong thing entirely). */

/* Replicates voice_start's own dampingAmount computation for
   verification (not exposed as a separate function). */
static double compute_string_mode_damping_amount(double releaseMs, double randSample) {
    double releaseNorm01 = log(releaseMs / 0.02) / log(4000.0 / 0.02);
    if (releaseNorm01 < 0.0) releaseNorm01 = 0.0;
    if (releaseNorm01 > 1.0) releaseNorm01 = 1.0;
    double targetKDamping = 0.95 + releaseNorm01 * 0.048 + randSample * 0.001;
    if (targetKDamping < 0.90) targetKDamping = 0.90;
    if (targetKDamping > 0.999) targetKDamping = 0.999;
    double dampingAmount = (targetKDamping - 0.90) / 0.099;
    if (dampingAmount < 0.0) dampingAmount = 0.0;
    if (dampingAmount > 1.0) dampingAmount = 1.0;
    return dampingAmount;
}

static double measure_rms_at_1s(double dampingAmount) {
    unsigned int rng = 42u;
    NoiseColour colours[1] = { NOISE_WHITE };
    KarplusString k;
    karplus_init(&k);
    karplus_pluck(&k, 220.0, 48000.0, &rng, colours, 1, dampingAmount);
    for (int i = 0; i < 48000; i++) { karplus_process(&k, 0.0, 0.0); } /* 1s, sustain=0 (released) */
    double sumSq = 0.0;
    int n = 4800;
    for (int i = 0; i < n; i++) {
        double y = karplus_process(&k, 0.0, 0.0);
        sumSq += y * y;
    }
    return sqrt(sumSq / n);
}

int main(void) {
    int all_ok = 1;

    /* Test 1: Release knob tie-in -- short vs long Release should
       produce clearly different string-mode dampingAmount, and
       therefore clearly different RMS remaining at 1s post-release. */
    {
        double dampingShort = compute_string_mode_damping_amount(20.0, 0.5);
        double dampingLong = compute_string_mode_damping_amount(3000.0, 0.5);
        double rmsShort = measure_rms_at_1s(dampingShort);
        double rmsLong = measure_rms_at_1s(dampingLong);
        printf("String mode: Release=20ms -> dampingAmount=%.4f, RMS@1s=%.6f\n", dampingShort, rmsShort);
        printf("String mode: Release=3000ms -> dampingAmount=%.4f, RMS@1s=%.6f\n", dampingLong, rmsLong);
        if (rmsLong <= rmsShort * 1.5) {
            printf("FAILED: longer Release should leave clearly more energy remaining\n");
            all_ok = 0;
        } else {
            printf("PASSED: Release knob measurably affects string-mode decay\n");
        }
    }

    /* Test 2: string mode decays much slower than typical plucky mode */
    {
        double dampingString = compute_string_mode_damping_amount(1000.0, 0.5);
        double dampingPlucky = 0.5; /* typical plucky-mode dampingAmount */
        double rmsString = measure_rms_at_1s(dampingString);
        double rmsPlucky = measure_rms_at_1s(dampingPlucky);
        printf("\nAt 1s post-release: string mode RMS=%.6f, plucky mode RMS=%.6f\n", rmsString, rmsPlucky);
        if (rmsString <= rmsPlucky * 2.0) {
            printf("FAILED: string mode should retain clearly more energy than plucky mode at 1s\n");
            all_ok = 0;
        } else {
            printf("PASSED: string mode clearly rings longer than plucky mode\n");
        }
    }

    /* Test 3: full pipeline sanity (the actual integration, not isolated logic) */
    {
        int finite_ok = 1;
        for (unsigned int i = 1; i < 150; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            e.params.releaseMs = 0.02 * pow(4000.0 / 0.02, (double)(i % 10) / 10.0);
            e.params.attackMs = 0.5 + (double)(i % 5) / 5.0 * 199.5;
            noiseboy_note_on(&e, 30 + (int)(i % 50), 0.8);
            for (int s = 0; s < 4800; s++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }
            noiseboy_note_off(&e, 30 + (int)(i % 50));
            for (int s = 0; s < 9600; s++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
            }
        }
        printf("\nFull pipeline sanity across 150 seeds/settings (held then released): %d\n", finite_ok);
        if (!finite_ok) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL KARPLUS DECAY VARIETY CHECKS PASSED\n" : "\nSOME KARPLUS DECAY VARIETY CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
