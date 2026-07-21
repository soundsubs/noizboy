#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

static double measure_peak(unsigned int seed, double knobValue, int notesToPlay) {
    NoiseboyEngine e;
    noiseboy_engine_init(&e, 48000.0, seed);
    e.params.outputFilterFreq01 = knobValue;
    for (int i = 0; i < notesToPlay; i++) {
        noiseboy_note_on(&e, 48 + i * 5, 0.8);
    }
    double peak = 0.0;
    for (int i = 0; i < (int)(48000 * 0.5); i++) {
        double y = noiseboy_process(&e);
        if (isnan(y) || isinf(y)) return -1.0; /* signal non-finite */
        if (fabs(y) > peak) peak = fabs(y);
    }
    return peak;
}

int main(void) {
    int all_ok = 1;
    unsigned int seed = 777u;

    double peakCenter = measure_peak(seed, 0.5, 3);
    double peakFullLeft = measure_peak(seed, 0.0, 3);
    double peakFullRight = measure_peak(seed, 1.0, 3);

    printf("Tilt filter peaks -- centre (bypass): %.5f, full left (lowpass->silence): %.5f, full right (highpass->silence): %.5f\n",
           peakCenter, peakFullLeft, peakFullRight);

    if (peakCenter < 0 || peakFullLeft < 0 || peakFullRight < 0) {
        printf("FAILED: non-finite output somewhere\n");
        all_ok = 0;
    }
    if (peakCenter < 0.01) {
        printf("FAILED: centre position should NOT be silent (bypass), but peak is near-zero\n");
        all_ok = 0;
    }
    if (peakFullLeft > peakCenter * 0.15) {
        printf("FAILED: full-left lowpass should silence far more than this (got %.5f vs centre %.5f)\n", peakFullLeft, peakCenter);
        all_ok = 0;
    }
    if (peakFullRight > peakCenter * 0.15) {
        printf("FAILED: full-right highpass should silence far more than this (got %.5f vs centre %.5f)\n", peakFullRight, peakCenter);
        all_ok = 0;
    }

    /* Sweep across the full knob range, confirm always finite */
    int finite_ok = 1;
    for (int i = 0; i <= 20; i++) {
        double knob = i / 20.0;
        double peak = measure_peak(seed, knob, 2);
        if (peak < 0) finite_ok = 0;
    }
    printf("Full knob sweep (0.0 to 1.0, 21 steps): finite_ok=%d\n", finite_ok);
    if (!finite_ok) all_ok = 0;

    printf(all_ok ? "\nALL TILT FILTER CHECKS PASSED\n" : "\nSOME TILT FILTER CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
