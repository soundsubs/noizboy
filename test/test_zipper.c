#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

int main(void) {
    /* Per the TILT redesign (see TiltFilter's own comment), knob
     * smoothing now lives in TiltFilter itself, not NoiseboyEngine --
     * test it directly there instead. */
    TiltFilter t;
    tilt_filter_init(&t, 48000.0);

    /* Simulate a knob being turned through discrete steps over time,
     * like real MIDI CC messages arriving as a physical knob turns --
     * jump the TARGET in steps every ~50ms, and confirm the SMOOTHED
     * value (what the filter actually uses) changes gradually rather
     * than snapping to each new target instantly. */
    double maxJumpPerSample = 0.0;
    double lastSmoothed = t.smoothedTilt01;

    for (int step = 0; step < 10; step++) {
        double target = step / 10.0; /* discrete jump, like a new MIDI CC value arriving */
        for (int i = 0; i < 2400; i++) { /* 50ms per step at 48kHz */
            double l = 0.0, r = 0.0;
            tilt_filter_process(&t, &l, &r, target, 48000.0);
            double jump = fabs(t.smoothedTilt01 - lastSmoothed);
            if (jump > maxJumpPerSample) maxJumpPerSample = jump;
            lastSmoothed = t.smoothedTilt01;
        }
    }

    printf("Max per-sample change in smoothed value across 10 discrete knob steps: %.6f\n", maxJumpPerSample);
    /* A raw instant-jump (no smoothing) would show a jump of up to 0.1 in a single sample (10 steps across 0-1 range).
       With smoothing, the largest single-sample change should be tiny. */
    if (maxJumpPerSample > 0.01) {
        printf("FAILED: still jumping abruptly, smoothing not effective\n");
        return 1;
    }
    printf("PASSED: smoothing confirmed effective, no abrupt jumps\n");
    return 0;
}
