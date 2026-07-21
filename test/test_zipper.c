#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

int main(void) {
    NoiseboyEngine e;
    noiseboy_engine_init(&e, 48000.0, 42u);
    noiseboy_note_on(&e, 60, 0.8);

    /* Simulate a knob being turned through discrete steps over time,
     * like real MIDI CC messages arriving as a physical knob turns --
     * jump the TARGET in steps every ~50ms, and confirm the SMOOTHED
     * value (what the filter actually uses) changes gradually rather
     * than snapping to each new target instantly. */
    double maxJumpPerSample = 0.0;
    double lastSmoothed = e.outputFilterFreqSmoothed01;

    for (int step = 0; step < 10; step++) {
        e.params.outputFilterFreq01 = step / 10.0; /* discrete jump, like a new MIDI CC value arriving */
        for (int i = 0; i < 2400; i++) { /* 50ms per step at 48kHz */
            noiseboy_process(&e);
            double jump = fabs(e.outputFilterFreqSmoothed01 - lastSmoothed);
            if (jump > maxJumpPerSample) maxJumpPerSample = jump;
            lastSmoothed = e.outputFilterFreqSmoothed01;
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
