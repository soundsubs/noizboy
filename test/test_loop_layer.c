#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the "poorly looped sample" third sound generation method,
 * per explicit spec: a fixed-size buffer, always NOISEBOY_LOOP_BUFFER_SIZE
 * samples, with pitch transposition achieved by reading it back at a
 * variable (nearest-neighbor) rate rather than resizing the buffer.
 * At middle C, one loop = the full buffer length. One octave up, one
 * loop = half that (buffer read twice as fast). One octave down, one
 * loop = double that (buffer read at half speed, each sample read
 * twice in a row -- "every other sample duplicated"). */

static double midi_to_freq(int note) {
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

/* Measure the actual loop period in output samples by finding when
   the read position (and therefore the output signal) returns to its
   starting value. */
static int measure_loop_period(double freqHz, double referenceFreqHz) {
    LoopSource lp;
    unsigned int rngState = 12345u;
    loop_capture(&lp, &rngState, NOISE_WHITE, freqHz, referenceFreqHz);

    double firstSample = loop_process(&lp);
    for (int i = 1; i < 200000; i++) {
        double y = loop_process(&lp);
        /* readPos wraps exactly when it crosses back to (or past) 0 --
           detect via the buffer's own readPos field directly instead
           of comparing sample values (noise could coincidentally
           repeat a value early). */
        if (lp.readPos < lp.playbackRate) {
            /* just wrapped this call */
            return i + 1;
        }
        (void)y;
        (void)firstSample;
    }
    return -1;
}

int main(void) {
    int all_ok = 1;
    double middleC = midi_to_freq(60);

    int periodAtMiddleC = measure_loop_period(middleC, middleC);
    int periodOctaveUp = measure_loop_period(middleC * 2.0, middleC);
    int periodOctaveDown = measure_loop_period(middleC * 0.5, middleC);

    printf("Loop period at middle C: %d (expect %d)\n", periodAtMiddleC, NOISEBOY_LOOP_BUFFER_SIZE);
    printf("Loop period one octave up: %d (expect %d)\n", periodOctaveUp, NOISEBOY_LOOP_BUFFER_SIZE / 2);
    printf("Loop period one octave down: %d (expect %d)\n", periodOctaveDown, NOISEBOY_LOOP_BUFFER_SIZE * 2);

    if (periodAtMiddleC != NOISEBOY_LOOP_BUFFER_SIZE) { printf("FAILED: middle C period\n"); all_ok = 0; }
    if (periodOctaveUp != NOISEBOY_LOOP_BUFFER_SIZE / 2) { printf("FAILED: octave up period\n"); all_ok = 0; }
    if (periodOctaveDown != NOISEBOY_LOOP_BUFFER_SIZE * 2) { printf("FAILED: octave down period\n"); all_ok = 0; }

    /* Verify "every other sample duplicated" at octave down specifically */
    {
        LoopSource lp;
        unsigned int rngState = 999u;
        loop_capture(&lp, &rngState, NOISE_WHITE, middleC * 0.5, middleC);
        double prev = loop_process(&lp);
        int duplicatesFound = 0;
        for (int i = 0; i < 20; i++) {
            double cur = loop_process(&lp);
            if (cur == prev) duplicatesFound++;
            prev = cur;
        }
        printf("Duplicate consecutive samples in first 20 (octave down): %d (expect close to 10)\n", duplicatesFound);
        if (duplicatesFound < 8) { printf("FAILED: expected roughly every other sample duplicated\n"); all_ok = 0; }
    }

    /* Full pipeline test: no NaN/Inf, signal actually present, across many seeds */
    {
        int finite_ok = 1, foundLoopType = 0;
        for (unsigned int i = 1; i < 500; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            int hasLoop = 0;
            for (int li = 0; li < e.numRecipeLayers; li++) {
                if (e.recipe[li].type == LAYER_LOOP) hasLoop = 1;
            }
            if (!hasLoop) continue;
            foundLoopType = 1;
            noiseboy_note_on(&e, 48 + (int)(i % 36), 0.8);
            double peak = 0.0;
            for (int s = 0; s < 24000; s++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
                if (fabs(l) > peak) peak = fabs(l);
            }
            if (peak < 1e-6) { printf("FAILED: silent voice with a loop layer (seed %u)\n", i); finite_ok = 0; }
        }
        printf("Full pipeline test across seeds with loop layers: foundLoopType=%d finite_ok=%d\n", foundLoopType, finite_ok);
        if (!foundLoopType) { printf("FAILED: never found a recipe with a loop layer in 500 seeds\n"); all_ok = 0; }
        if (!finite_ok) all_ok = 0;
    }

    printf(all_ok ? "\nALL LOOP LAYER CHECKS PASSED\n" : "\nSOME LOOP LAYER CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
