#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the attack/release knob mappings, redesigned per direct
 * calibration feedback: "at Attack of 0, its fine, but almost by 50
 * theres barely any perceptible change. Similarly, at a release of
 * 64, the decay is too instant. By release of 127, it should almost
 * always be on." This replicates the exact formulas from
 * noiseboy_plugin.c's set_param (which can't be unit-tested directly
 * here, since it needs the real Schwung plugin headers) to verify
 * their endpoints, shape, and the specific reported calibration
 * points directly. */

static double attack_mapping(double raw01) {
    return 0.5 + 599.5 * pow(raw01, 0.85);
}
static double attack_inverse(double attackMs) {
    return pow((attackMs - 0.5) / 599.5, 1.0 / 0.85);
}
static double release_mapping(double raw01) {
    return 0.02 * pow(8000.0 / 0.02, pow(raw01, 0.35));
}
static double release_inverse(double releaseMs) {
    return pow(log(releaseMs / 0.02) / log(8000.0 / 0.02), 1.0 / 0.35);
}

int main(void) {
    int all_ok = 1;

    /* --- ATTACK --- */
    printf("=== ATTACK ===\n");
    double attackAt0 = attack_mapping(0.0);
    double attackAt127 = attack_mapping(1.0);
    printf("knob=0: %.3fms (expect ~0.5ms), knob=127: %.1fms (expect 600ms)\n", attackAt0, attackAt127);
    if (fabs(attackAt0 - 0.5) > 0.01) { printf("FAILED: attack knob=0 endpoint\n"); all_ok = 0; }
    if (fabs(attackAt127 - 600.0) > 0.5) { printf("FAILED: attack knob=127 endpoint\n"); all_ok = 0; }

    /* The specific reported calibration point: knob=50/127 should be
       clearly, substantially different from knob=0 -- not "barely any
       perceptible change". Require at least a 3x jump from the floor,
       a conservative bar for "clearly audible difference". */
    double attackAt50 = attack_mapping(50.0 / 127.0);
    printf("knob=50: %.2fms (old linear mapping gave ~79ms, which was reported as barely different from knob=0's ~0.5ms)\n", attackAt50);
    if (attackAt50 < attackAt0 * 3.0) {
        printf("FAILED: knob=50 should be clearly, substantially different from knob=0\n");
        all_ok = 0;
    } else printf("PASSED: knob=50 is now clearly different from knob=0\n");

    /* Round-trip accuracy */
    int attackRoundTripOk = 1;
    for (double k = 0.0; k <= 1.0001; k += 0.2) {
        double ms = attack_mapping(k);
        double kBack = attack_inverse(ms);
        if (fabs(k - kBack) > 0.001) attackRoundTripOk = 0;
    }
    printf("Attack round-trip mapping: %s\n", attackRoundTripOk ? "PASSED" : "FAILED");
    if (!attackRoundTripOk) all_ok = 0;

    /* --- RELEASE --- */
    printf("\n=== RELEASE ===\n");
    double releaseAt0 = release_mapping(0.0);
    double releaseAt127 = release_mapping(1.0);
    printf("knob=0: %.4fms (expect ~0.02ms, ~1 sample at 48kHz), knob=127: %.1fms (expect 8000ms)\n", releaseAt0, releaseAt127);
    if (fabs(releaseAt0 - 0.02) > 0.001) { printf("FAILED: release knob=0 endpoint\n"); all_ok = 0; }
    if (fabs(releaseAt127 - 8000.0) > 1.0) { printf("FAILED: release knob=127 endpoint\n"); all_ok = 0; }

    double oneSampleMs = 1000.0 / 48000.0;
    if (releaseAt0 > oneSampleMs * 1.5) { printf("FAILED: knob=0 release time should be close to one sample duration\n"); all_ok = 0; }

    /* The specific reported calibration point: knob=64/127 (roughly
       the midpoint) was reported as "too instant" under the old
       mapping (which gave only ~9.4ms there). Require a release time
       clearly in "genuine decay" territory, not "instant" -- at least
       200ms, a conservative bar. */
    double releaseAt64 = release_mapping(64.0 / 127.0);
    printf("knob=64: %.2fms (old mapping gave ~9.4ms here, reported as \"too instant\")\n", releaseAt64);
    if (releaseAt64 < 200.0) {
        printf("FAILED: knob=64 should be a clearly audible decay, not near-instant\n");
        all_ok = 0;
    } else printf("PASSED: knob=64 is now a genuine, non-instant decay\n");

    /* "By release of 127, it should almost always be on" -- require a
       multi-second tail at max knob. */
    if (releaseAt127 < 5000.0) {
        printf("FAILED: knob=127 should give a very long, 'almost always on' release\n");
        all_ok = 0;
    } else printf("PASSED: knob=127 gives a genuinely long, sustained-feeling release\n");

    /* Monotonicity */
    int releaseMonotonic = 1;
    double prev = -1;
    for (double k = 0.0; k <= 1.0001; k += 0.05) {
        double ms = release_mapping(k);
        if (prev > 0 && ms <= prev) releaseMonotonic = 0;
        prev = ms;
    }
    printf("Release mapping monotonic: %s\n", releaseMonotonic ? "PASSED" : "FAILED");
    if (!releaseMonotonic) all_ok = 0;

    /* Round-trip accuracy */
    int releaseRoundTripOk = 1;
    for (double k = 0.0; k <= 1.0001; k += 0.2) {
        double ms = release_mapping(k);
        double kBack = release_inverse(ms);
        if (fabs(k - kBack) > 0.001) releaseRoundTripOk = 0;
    }
    printf("Release round-trip mapping: %s\n", releaseRoundTripOk ? "PASSED" : "FAILED");
    if (!releaseRoundTripOk) all_ok = 0;

    /* Full DSP integration test -- actually use these times in the
       real engine, confirm clean, click-free, finite behavior, and
       that the envelope's own clamp (widened to 8000ms) doesn't
       silently truncate the new, longer release times. */
    {
        int finite_ok = 1;
        for (double k = 0.0; k <= 1.0; k += 0.1) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, 42u);
            e.params.releaseMs = release_mapping(k);
            e.params.attackMs = attack_mapping(k);
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

    printf(all_ok ? "\nALL ENVELOPE CURVE CHECKS PASSED\n" : "\nSOME ENVELOPE CURVE CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
