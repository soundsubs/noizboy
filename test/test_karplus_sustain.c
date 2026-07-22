#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

static int check_finite(double x) { return !(isnan(x) || isinf(x)); }

int main(void) {
    int all_ok = 1;

    /* Per the fixed 3-source mixer restructuring (layers 0/1 always
     * filtered-noise, layer 2 always Karplus-Strong -- see
     * LayerRecipe's own header comment), there's no more "all-Karplus
     * recipe" to search for -- layer 2 is always Karplus by
     * construction. Isolate it directly via mix levels instead (mute
     * the two noise sources, keep Karplus at full level) so this test
     * measures the Karplus sustain behaviour specifically, not a mix
     * of all three sources. */
    NoiseboyEngine e;
    unsigned int seed = 12345u;
    noiseboy_engine_init(&e, 48000.0, seed);
    e.recipe[0].mixLevel01 = 0.0;
    e.recipe[1].mixLevel01 = 0.0;
    e.recipe[2].mixLevel01 = 1.0;
    printf("Using seed %u, layer 2 (Karplus) isolated via mix levels\n", seed);

    /* Test: MAX attack time, hold the note, check the signal is still
     * clearly audible well after the envelope has risen -- this is
     * exactly the "with maximized Attack, you almost can't hear the
     * Karplus pluck" complaint. */
    noiseboy_engine_init(&e, 48000.0, seed);
    e.recipe[0].mixLevel01 = 0.0;
    e.recipe[1].mixLevel01 = 0.0;
    e.recipe[2].mixLevel01 = 1.0;
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
