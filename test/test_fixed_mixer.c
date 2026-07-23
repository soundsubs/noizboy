#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the fixed 4-source mixer restructuring, per explicit
 * request: always exactly 2 noise + 1 Karplus + 1 Loop source, only
 * mix level (0-100%) randomized, and that randomization happens at
 * the RECIPE level (fixed until the user globally re-randomizes), not
 * re-rolled per note. (Originally 3 sources; Loop was added back as a
 * 4th per a later explicit revert request -- see LoopSource's own
 * comment.) */

int main(void) {
    int all_ok = 1;

    /* Structure check: always 4 layers, always layers 0/1 = noise, layer 2 = karplus, layer 3 = loop */
    for (unsigned int i = 1; i < 200; i++) {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, i * 7919u);
        if (e.numRecipeLayers != 4) { printf("FAILED: numRecipeLayers should always be 4 (seed %u, got %d)\n", i, e.numRecipeLayers); all_ok = 0; }
        if (e.recipe[0].type != LAYER_FILTERED_NOISE) { printf("FAILED: layer 0 should always be filtered-noise (seed %u)\n", i); all_ok = 0; }
        if (e.recipe[1].type != LAYER_FILTERED_NOISE) { printf("FAILED: layer 1 should always be filtered-noise (seed %u)\n", i); all_ok = 0; }
        if (e.recipe[2].type != LAYER_KARPLUS_STRONG) { printf("FAILED: layer 2 should always be Karplus (seed %u)\n", i); all_ok = 0; }
        if (e.recipe[3].type != LAYER_LOOP) { printf("FAILED: layer 3 should always be Loop (seed %u)\n", i); all_ok = 0; }
        if (e.recipe[0].mixLevel01 < 0.0 || e.recipe[0].mixLevel01 > 1.0) { printf("FAILED: mixLevel01 out of 0-1 range\n"); all_ok = 0; }
    }
    printf("Structure check across 200 seeds: %s\n", all_ok ? "PASSED" : "FAILED");

    /* Mix level randomization check: different seeds should give
       different mix levels (real variety), and mix levels should span
       close to the full 0-1 range across many seeds. */
    double minMix = 2.0, maxMix = -1.0;
    for (unsigned int i = 1; i < 500; i++) {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, i * 7919u);
        for (int li = 0; li < 4; li++) {
            if (e.recipe[li].mixLevel01 < minMix) minMix = e.recipe[li].mixLevel01;
            if (e.recipe[li].mixLevel01 > maxMix) maxMix = e.recipe[li].mixLevel01;
        }
    }
    printf("Mix level range across 500 seeds x 4 layers: %.4f to %.4f (expect close to 0.0-1.0)\n", minMix, maxMix);
    if (maxMix - minMix < 0.9) { printf("FAILED: not enough mix level variety\n"); all_ok = 0; }

    /* Per-note stability check: mix levels should NOT change across
       repeated note-on calls within the same engine instance (fixed
       until global re-randomize, not re-rolled per note). */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, 42u);
        double mix0 = e.recipe[0].mixLevel01, mix1 = e.recipe[1].mixLevel01, mix2 = e.recipe[2].mixLevel01, mix3 = e.recipe[3].mixLevel01;
        for (int n = 0; n < 20; n++) {
            noiseboy_note_on(&e, 48 + n, 0.8);
            for (int s = 0; s < 480; s++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }
            noiseboy_note_off(&e, 48 + n);
            for (int s = 0; s < 480; s++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }
        }
        int stable = (e.recipe[0].mixLevel01 == mix0 && e.recipe[1].mixLevel01 == mix1 && e.recipe[2].mixLevel01 == mix2 && e.recipe[3].mixLevel01 == mix3);
        printf("Mix levels stable across 20 note-on/off cycles (no per-note re-randomization): %s\n", stable ? "PASSED" : "FAILED");
        if (!stable) all_ok = 0;
    }

    /* Re-randomize check: calling noiseboy_randomize_recipe should
       actually change the mix levels (confirming the global re-
       randomize path still works). */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, 999u);
        double before0 = e.recipe[0].mixLevel01, before1 = e.recipe[1].mixLevel01, before2 = e.recipe[2].mixLevel01, before3 = e.recipe[3].mixLevel01;
        noiseboy_randomize_recipe(&e);
        int changed = (e.recipe[0].mixLevel01 != before0 || e.recipe[1].mixLevel01 != before1 || e.recipe[2].mixLevel01 != before2 || e.recipe[3].mixLevel01 != before3);
        printf("Global re-randomize changes mix levels: %s\n", changed ? "PASSED" : "FAILED (extremely unlikely coincidence, or a real bug)");
        if (!changed) all_ok = 0;
        /* Structure should still hold after re-randomize too */
        if (e.numRecipeLayers != 4 || e.recipe[0].type != LAYER_FILTERED_NOISE ||
            e.recipe[1].type != LAYER_FILTERED_NOISE || e.recipe[2].type != LAYER_KARPLUS_STRONG ||
            e.recipe[3].type != LAYER_LOOP) {
            printf("FAILED: structure should stay fixed even after re-randomize\n");
            all_ok = 0;
        }
    }

    /* Mute check: a source with mixLevel01=0 should contribute nothing audible */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, 42u);
        e.recipe[0].mixLevel01 = 0.0;
        e.recipe[1].mixLevel01 = 0.0;
        e.recipe[2].mixLevel01 = 1.0;
        e.recipe[3].mixLevel01 = 0.0;
        noiseboy_note_on(&e, 48, 0.8);
        for (int i = 0; i < 2400; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }
        /* verify voice's other layer outputs are being scaled to 0 -- indirect check via full mute of all sources */
        NoiseboyEngine e2;
        noiseboy_engine_init(&e2, 48000.0, 42u);
        e2.recipe[0].mixLevel01 = 0.0;
        e2.recipe[1].mixLevel01 = 0.0;
        e2.recipe[2].mixLevel01 = 0.0;
        e2.recipe[3].mixLevel01 = 0.0;
        noiseboy_note_on(&e2, 48, 0.8);
        double peak = 0.0;
        for (int i = 0; i < 4800; i++) { double l, r; noiseboy_process_stereo(&e2, &l, &r); if (fabs(l) > peak) peak = fabs(l); }
        printf("All sources muted (mixLevel01=0) -> peak=%.6f (expect ~0)\n", peak);
        if (peak > 0.001) { printf("FAILED: muted sources should contribute no audible signal\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL FIXED MIXER CHECKS PASSED\n" : "\nSOME FIXED MIXER CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
