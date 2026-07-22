#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the fix for a real reported bug: "with release longer than
 * [a certain amount], it starts to click and glitch... increasing
 * release amount increases glitching." Root cause: longer release
 * times mean voices stay "active" (and therefore ineligible to be
 * picked as a free voice) for longer, so the 8-voice pool runs out
 * more often, forcing voice-stealing -- and the old stealing logic
 * immediately reset a still-sounding voice's ENTIRE state (envelope,
 * Karplus pluck, filters) in one step, producing a real, measured
 * discontinuity (confirmed directly: up to a 0.17 single-sample jump
 * against a signal where anything past a few times the normal
 * waveform's own sample-to-sample variation is audible).
 *
 * IMPORTANT METHODOLOGY NOTE: a naive "max jump over a window" test
 * is NOT a valid way to measure this -- high-resonance tonal content
 * (this project's default filter resonance is 0.82, near self-
 * oscillation) legitimately produces large sample-to-sample jumps on
 * its own, confirmed here via a no-stealing-involved baseline
 * measurement. The only valid check is comparing the jump specifically
 * AT the moment of a voice reset against the BASELINE jump magnitude
 * of the same signal without any stealing -- a ratio close to 1x means
 * the reset is inaudible against the signal's own normal variation; a
 * ratio of many multiples (like the original bug's 15-20x) means a
 * real, audible click. */

static double measure_baseline_jump(void) {
    NoiseboyEngine e;
    noiseboy_engine_init(&e, 48000.0, 42u);
    e.params.releaseMs = 2000.0;
    e.params.attackMs = 4.0;
    for (int i = 0; i < 8; i++) noiseboy_note_on(&e, 40 + i * 3, 0.9);
    for (int i = 0; i < 4800; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }
    for (int i = 0; i < 8; i++) noiseboy_note_off(&e, 40 + i * 3);

    double lastL = 0.0, sumJump = 0.0;
    int count = 0;
    for (int i = 0; i < 3000; i++) {
        double l, r;
        noiseboy_process_stereo(&e, &l, &r);
        if (i > 0) { sumJump += fabs(l - lastL); count++; }
        lastL = l;
    }
    return sumJump / count;
}

int main(void) {
    int all_ok = 1;

    const double baselineJump = measure_baseline_jump();
    printf("Baseline (no stealing) average sample-to-sample jump: %.5f\n", baselineJump);

    /* Now trigger an actual steal under the reported conditions (long
     * release, all voices occupied) and measure the jump right at the
     * immediate steal-request moment, and right at the eventual
     * voice_start reset once the forced fast release has decayed it. */
    NoiseboyEngine e;
    noiseboy_engine_init(&e, 48000.0, 42u);
    e.params.releaseMs = 2000.0; /* long release -- the reported trigger condition */
    e.params.attackMs = 4.0;

    for (int i = 0; i < 8; i++) noiseboy_note_on(&e, 40 + i * 3, 0.9);
    for (int i = 0; i < 4800; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }
    for (int i = 0; i < 8; i++) noiseboy_note_off(&e, 40 + i * 3);
    for (int i = 0; i < 480; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }

    double lastL;
    { double l, r; noiseboy_process_stereo(&e, &l, &r); lastL = l; }

    noiseboy_note_on(&e, 72, 0.9); /* forces a steal -- all 8 voices still occupied */

    double immediateJump = -1.0, transitionJump = -1.0;
    int transitionSample = -1;
    int prevPendingSteal = -1;
    int finite_ok = 1;

    for (int i = 0; i < 8000; i++) {
        double l, r;
        noiseboy_process_stereo(&e, &l, &r);
        if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
        double jump = fabs(l - lastL);

        if (i == 0) immediateJump = jump;

        int curPendingSteal = e.voices[0].pendingSteal;
        if (prevPendingSteal == 1 && curPendingSteal == 0) {
            transitionJump = jump;
            transitionSample = i;
        }
        prevPendingSteal = curPendingSteal;
        lastL = l;
    }

    if (!finite_ok) { printf("FAILED: non-finite output\n"); all_ok = 0; }

    printf("Immediate jump at steal request: %.5f (ratio vs baseline: %.2fx)\n",
           immediateJump, immediateJump / baselineJump);
    if (immediateJump / baselineJump > 5.0) {
        printf("  FAILED: immediate transition should be smooth, not a spike\n");
        all_ok = 0;
    } else {
        printf("  PASSED\n");
    }

    if (transitionSample < 0) {
        printf("FAILED: the deferred voice_start transition never happened within the test window\n");
        all_ok = 0;
    } else {
        printf("Transition jump at actual voice_start (sample %d, %.1fms after steal request): %.5f (ratio vs baseline: %.2fx)\n",
               transitionSample, transitionSample / 48.0, transitionJump, transitionJump / baselineJump);
        if (transitionJump / baselineJump > 5.0) {
            printf("  FAILED: deferred reset should happen once the old sound is inaudible, not produce a spike\n");
            all_ok = 0;
        } else {
            printf("  PASSED\n");
        }
    }

    printf(all_ok ? "\nALL VOICE-STEAL CLICK CHECKS PASSED\n" : "\nSOME VOICE-STEAL CLICK CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
