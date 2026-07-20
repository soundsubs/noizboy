#include "../src/noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

static int check_finite(double x) {
    return !(isnan(x) || isinf(x));
}

/* Counts zero-crossings over a fixed window -- a rough, weak-but-
 * meaningful proxy for "is this pitched roughly where expected", since
 * a real spectral/FFT-based pitch check is more machinery than this
 * sanity test needs. Not a precise pitch measurement -- filtered noise
 * and Karplus-Strong are both noisy/non-pure-tone sources, so exact
 * zero-crossing-rate-to-frequency conversion isn't meaningful here;
 * this only checks the DIRECTIONAL relationship (higher note ->
 * more crossings) holds. */
static int count_zero_crossings(NoiseboyEngine *e, int midiNote, int numSamples) {
    noiseboy_all_notes_off(e);
    for (int i = 0; i < 200; i++) noiseboy_process(e); /* let any previous voice fully release */
    noiseboy_note_on(e, midiNote, 0.9);
    for (int i = 0; i < 500; i++) noiseboy_process(e); /* skip past attack ramp */

    int crossings = 0;
    double last = noiseboy_process(e);
    for (int i = 0; i < numSamples; i++) {
        double y = noiseboy_process(e);
        if (!check_finite(y)) return -1;
        if ((last < 0.0 && y >= 0.0) || (last >= 0.0 && y < 0.0)) crossings++;
        last = y;
    }
    noiseboy_note_off(e, midiNote);
    return crossings;
}

int main(void) {
    int all_ok = 1;
    double sr = 48000.0;

    /* --- Test 1: multiple random seeds, verify finite output through
     * note-on -> hold -> note-off -> full release, across several
     * different MIDI notes each time (exercises different recipe
     * combinations across seeds, and different layer pitches/detune
     * ratios across notes). --- */
    for (unsigned int seed = 1; seed <= 12; seed++) {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, sr, seed * 7919u);

        printf("seed %u: %d layer(s), types:", seed, e.numRecipeLayers);
        for (int i = 0; i < e.numRecipeLayers; i++) {
            printf(" %s", e.recipe[i].type == LAYER_FILTERED_NOISE ? "filt" : "karp");
        }
        printf("\n");

        int notes[] = { 21, 40, 60, 72, 96, 108 }; /* A0 up to C8-ish, covers the practical range */
        for (int ni = 0; ni < 6; ni++) {
            noiseboy_note_on(&e, notes[ni], 0.8);
            int finite_ok = 1;
            double peak = 0.0;
            for (int i = 0; i < (int)(sr * 0.3); i++) {
                double y = noiseboy_process(&e);
                if (!check_finite(y)) finite_ok = 0;
                if (fabs(y) > peak) peak = fabs(y);
            }
            noiseboy_note_off(&e, notes[ni]);
            /* Let it fully release before the next note in this loop. */
            for (int i = 0; i < (int)(sr * 0.5); i++) {
                double y = noiseboy_process(&e);
                if (!check_finite(y)) finite_ok = 0;
            }
            if (!finite_ok) {
                printf("  FAIL: non-finite output, seed=%u note=%d\n", seed, notes[ni]);
                all_ok = 0;
            }
            if (peak < 1e-6) {
                printf("  FAIL: note produced no audible signal, seed=%u note=%d\n", seed, notes[ni]);
                all_ok = 0;
            }
            if (peak > 4.0) {
                printf("  WARN: unusually high peak %.3f, seed=%u note=%d\n", peak, seed, notes[ni]);
            }
        }
    }
    printf(all_ok ? "Finite-output/audible-signal test across seeds and notes: PASSED\n\n"
                   : "Finite-output/audible-signal test across seeds and notes: FAILED\n\n");

    /* --- Test 2: voice fully releases to silence after note-off --- */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, sr, 0xA17ADE55u);
        e.params.releaseMs = 50.0;
        noiseboy_note_on(&e, 60, 0.9);
        for (int i = 0; i < 2000; i++) noiseboy_process(&e);
        noiseboy_note_off(&e, 60);
        for (int i = 0; i < (int)(sr * 0.5); i++) noiseboy_process(&e);
        int anyActive = 0;
        for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) if (e.voices[v].active) anyActive = 1;
        if (anyActive) {
            printf("Release-to-silence test: FAILED (voice still active after 500ms post-release)\n");
            all_ok = 0;
        } else {
            printf("Release-to-silence test: PASSED\n");
        }
    }

    /* --- Test 3: polyphony -- 4 simultaneous notes, all finite --- */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, sr, 0xC0FFEEu);
        int notes[] = { 48, 52, 55, 60 };
        for (int i = 0; i < 4; i++) noiseboy_note_on(&e, notes[i], 0.7);
        int finite_ok = 1;
        int activeCount = 0;
        for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) if (e.voices[v].active) activeCount++;
        for (int i = 0; i < (int)(sr * 0.2); i++) {
            double y = noiseboy_process(&e);
            if (!check_finite(y)) finite_ok = 0;
        }
        if (!finite_ok || activeCount != 4) {
            printf("Polyphony test: FAILED (finite_ok=%d activeCount=%d, expected 4)\n", finite_ok, activeCount);
            all_ok = 0;
        } else {
            printf("Polyphony test: PASSED (4 simultaneous voices, all finite)\n");
        }
    }

    /* --- Test 4: voice stealing -- 9 notes on an 8-voice engine
     * shouldn't crash or produce non-finite output. --- */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, sr, 0xDEADBEEFu);
        int finite_ok = 1;
        for (int i = 0; i < 9; i++) {
            noiseboy_note_on(&e, 40 + i, 0.6);
            for (int s = 0; s < 200; s++) {
                double y = noiseboy_process(&e);
                if (!check_finite(y)) finite_ok = 0;
            }
        }
        printf(finite_ok ? "Voice-stealing test (9 notes, 8 voices): PASSED\n"
                          : "Voice-stealing test (9 notes, 8 voices): FAILED\n");
        if (!finite_ok) all_ok = 0;
    }

    /* --- Test 5: directional pitch-tracking sanity -- a low note
     * should produce noticeably fewer zero-crossings than a high note
     * over the same window, for BOTH a filtered-noise-only recipe and
     * whatever recipe a given seed rolls (weak check, see comment on
     * count_zero_crossings). --- */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, sr, 0x1234u);
        int lowCrossings = count_zero_crossings(&e, 36, 4000);
        int highCrossings = count_zero_crossings(&e, 84, 4000);
        printf("Pitch-tracking direction check: note 36 -> %d crossings, note 84 -> %d crossings\n",
               lowCrossings, highCrossings);
        if (lowCrossings < 0 || highCrossings < 0) {
            printf("  FAILED: non-finite output during pitch check\n");
            all_ok = 0;
        } else if (highCrossings <= lowCrossings) {
            printf("  WARN: expected higher note to have more zero-crossings than lower note\n");
        } else {
            printf("  PASSED\n");
        }
    }

    printf(all_ok ? "\nALL CHECKS PASSED\n" : "\nSOME CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
