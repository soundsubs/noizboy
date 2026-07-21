#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

static int check_finite(double x) { return !(isnan(x) || isinf(x)); }

int main(void) {
    int all_ok = 1;

    /* Find a seed that gives an all-Karplus recipe, to isolate the effect. */
    NoiseboyEngine e;
    unsigned int seed = 0;
    for (unsigned int s = 1; s < 10000; s++) {
        noiseboy_engine_init(&e, 48000.0, s * 7919u);
        int allKarplus = 1;
        for (int i = 0; i < e.numRecipeLayers; i++) {
            if (e.recipe[i].type != LAYER_KARPLUS_STRONG) allKarplus = 0;
        }
        if (allKarplus) { seed = s; break; }
    }
    if (seed == 0) { printf("Could not find an all-Karplus recipe in 10000 tries\n"); return 1; }
    printf("Using seed %u (%d all-Karplus layers)\n", seed, e.numRecipeLayers);

    /* Test: MAX attack time, hold the note, check the signal is still
     * clearly audible well after the envelope has risen -- this is
     * exactly the "with maximized Attack, you almost can't hear the
     * Karplus pluck" complaint. */
    noiseboy_engine_init(&e, 48000.0, seed * 7919u);
    e.params.attackMs = 200.0; /* max per NoiseboyParams range */
    e.params.releaseMs = 80.0;
    noiseboy_note_on(&e, 48, 1.0);

    double sr = 48000.0;
    int finite_ok = 1;
    double peakDuring0to200ms = 0.0;
    double peakDuring500to1000ms = 0.0;

    for (int i = 0; i < (int)(sr * 0.2); i++) {
        double y = noiseboy_process(&e);
        if (!check_finite(y)) finite_ok = 0;
        if (fabs(y) > peakDuring0to200ms) peakDuring0to200ms = fabs(y);
    }
    for (int i = 0; i < (int)(sr * 0.3); i++) noiseboy_process(&e); /* skip to 500ms */
    for (int i = 0; i < (int)(sr * 0.5); i++) {
        double y = noiseboy_process(&e);
        if (!check_finite(y)) finite_ok = 0;
        if (fabs(y) > peakDuring500to1000ms) peakDuring500to1000ms = fabs(y);
    }

    printf("Peak during attack ramp (0-200ms): %.5f\n", peakDuring0to200ms);
    printf("Peak once envelope is fully up + string had time to settle (500-1000ms, still held): %.5f\n", peakDuring500to1000ms);
    if (!finite_ok) { printf("FAILED: non-finite output\n"); all_ok = 0; }
    if (peakDuring500to1000ms < 0.01) {
        printf("FAILED: signal essentially inaudible even once envelope is fully open -- sustain feed isn't working\n");
        all_ok = 0;
    } else {
        printf("PASSED: signal still clearly audible with envelope fully open -- sustain feed keeps the string ringing\n");
    }

    /* Now release and confirm it actually rings out to silence rather than sustaining forever. */
    noiseboy_note_off(&e, 48);
    for (int i = 0; i < (int)(sr * 1.0); i++) {
        double y = noiseboy_process(&e);
        if (!check_finite(y)) finite_ok = 0;
    }
    int stillActive = e.voices[0].active;
    /* Voice may have been assigned to a different slot; check all */
    int anyActive = 0;
    for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) if (e.voices[v].active) anyActive = 1;
    printf("Any voice still active 1s after release: %d (should be 0 -- confirms it still rings OUT, not forever)\n", anyActive);
    if (anyActive) { printf("FAILED: sustain feed prevents proper release\n"); all_ok = 0; }
    (void)stillActive;

    printf(all_ok ? "\nALL KARPLUS SUSTAIN CHECKS PASSED\n" : "\nSOME KARPLUS SUSTAIN CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
