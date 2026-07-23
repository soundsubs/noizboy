#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Tests the REVERTED LOOP design: a per-layer, pre-filter source
 * (always layer index 3), instantly captured at note-on, pitch-
 * transposed by note number (like the original design), with knob 4
 * (loopLengthKnob01) directly setting the master capture length (XX)
 * across its full range, and decaying to near-silence by 98% through
 * each pass. See LoopSource's own header comment for the full design
 * history. */

int main(void) {
    int all_ok = 1;

    /* Test 1: loopLengthKnob01 maps correctly across the full XX range */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, 42u);
        e.params.loopLengthKnob01 = 0.0;
        noiseboy_note_on(&e, 60, 0.8); /* middle C -- playbackRate=1.0, so captureLengthSamples == the loop's own period in samples */
        int lenAtMin = e.voices[0].layers[3].loop.captureLengthSamples;
        double expectedMin = NOISEBOY_LOOP_MIN_SECONDS * 48000.0;

        NoiseboyEngine e2;
        noiseboy_engine_init(&e2, 48000.0, 42u);
        e2.params.loopLengthKnob01 = 1.0;
        noiseboy_note_on(&e2, 60, 0.8);
        int lenAtMax = e2.voices[0].layers[3].loop.captureLengthSamples;
        double expectedMax = NOISEBOY_LOOP_MAX_SECONDS * 48000.0;

        printf("Loop Length knob=0.0 -> %d samples (expect ~%.0f), knob=1.0 -> %d samples (expect ~%.0f)\n",
               lenAtMin, expectedMin, lenAtMax, expectedMax);
        if (fabs(lenAtMin - expectedMin) > 10 || fabs(lenAtMax - expectedMax) > 10) {
            printf("FAILED: knob mapping doesn't match expected XX range\n");
            all_ok = 0;
        } else {
            printf("PASSED\n");
        }
    }

    /* Test 2: layer 3 is always LAYER_LOOP, present in every recipe */
    {
        int all_correct = 1;
        for (unsigned int i = 1; i < 200; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            if (e.recipe[3].type != LAYER_LOOP) all_correct = 0;
        }
        printf("\nLayer 3 is always LAYER_LOOP across 200 seeds: %d\n", all_correct);
        if (!all_correct) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 3: pitch-transposition -- higher note plays back faster */
    {
        NoiseboyEngine eLow, eHigh;
        noiseboy_engine_init(&eLow, 48000.0, 42u);
        noiseboy_engine_init(&eHigh, 48000.0, 42u);
        noiseboy_note_on(&eLow, 60, 0.8);  /* middle C */
        noiseboy_note_on(&eHigh, 72, 0.8); /* one octave up */
        double rateLow = eLow.voices[0].layers[3].loop.playbackRate;
        double rateHigh = eHigh.voices[0].layers[3].loop.playbackRate;
        printf("\nPlayback rate at middle C: %.4f, one octave up: %.4f (expect ~2x)\n", rateLow, rateHigh);
        if (fabs(rateHigh / rateLow - 2.0) > 0.01) {
            printf("FAILED: expected exactly 2x playback rate one octave up\n");
            all_ok = 0;
        } else {
            printf("PASSED\n");
        }
    }

    /* Test 4: available from the very first sample -- no capture delay.
       Check the loop layer's own raw output directly (via
       process_layer's dispatch, effectively) rather than through the
       full voice pipeline, which would otherwise be masked by the
       attack envelope's own ramp-up over the first few ms. */
    {
        LoopSource lp = {0};
        if (!loop_source_alloc(&lp)) { printf("\nFAILED: alloc failed\n"); all_ok = 0; }
        else {
            unsigned int rng = 42u;
            loop_capture(&lp, &rng, 261.6, 261.6, 4800);
            double peakVeryFirst = 0.0;
            for (int i = 0; i < 100; i++) {
                double out = loop_process(&lp);
                if (fabs(out) > peakVeryFirst) peakVeryFirst = fabs(out);
            }
            printf("\nPeak in the first 100 samples, raw LoopSource output: %.4f (expect clearly nonzero -- no capture delay)\n", peakVeryFirst);
            if (peakVeryFirst < 0.01) {
                printf("FAILED: Loop should be audible from the very first sample, not building up over time\n");
                all_ok = 0;
            } else {
                printf("PASSED\n");
            }
            loop_source_free(&lp);
        }
    }

    /* Test 5: decay reaches near-silence by 98% through the loop */
    {
        LoopSource lp = {0};
        if (!loop_source_alloc(&lp)) { printf("\nFAILED: alloc failed\n"); all_ok = 0; }
        else {
            unsigned int rng = 123u;
            int captureLen = 4800; /* 100ms at 48kHz */
            loop_capture(&lp, &rng, 261.6, 261.6, captureLen); /* playbackRate = 1.0 -- straightforward proportional read */
            double gainAt50 = 0, gainAt98 = 0, gainAt99 = 0;
            for (int i = 0; i < captureLen; i++) {
                double out = loop_process(&lp);
                double frac = (double)i / (double)captureLen;
                if (fabs(frac - 0.50) < 0.001) gainAt50 = fabs(out);
                if (fabs(frac - 0.98) < 0.001) gainAt98 = fabs(out);
                if (fabs(frac - 0.99) < 0.001) gainAt99 = fabs(out);
            }
            printf("\nDecay curve: gain magnitude at 50%%=%.4f, 98%%=%.4f, 99%%=%.4f (expect near-silent by 98%%)\n", gainAt50, gainAt98, gainAt99);
            if (gainAt98 > 0.05) {
                printf("FAILED: expected near-silence by 98%% through the loop\n");
                all_ok = 0;
            } else {
                printf("PASSED\n");
            }
            loop_source_free(&lp);
        }
    }

    /* Test 6: fresh capture every note -- different content each time */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, 42u);
        e.recipe[3].mixLevel01 = 1.0;
        noiseboy_note_on(&e, 60, 0.8);
        double firstSample = e.voices[0].layers[3].loop.buffer[0];
        noiseboy_note_off(&e, 60);
        for (int i = 0; i < 48000 * 2; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); } /* let it fully release */
        noiseboy_note_on(&e, 60, 0.8); /* fresh note, same pitch */
        double secondSample = e.voices[0].layers[3].loop.buffer[0];
        printf("\nFirst capture's buffer[0]=%.6f, second capture's buffer[0]=%.6f (expect different -- fresh capture each note)\n", firstSample, secondSample);
        if (fabs(firstSample - secondSample) < 1e-9) {
            printf("FAILED: expected fresh, different noise content captured each note\n");
            all_ok = 0;
        } else {
            printf("PASSED\n");
        }
    }

    /* Test 7: full pipeline sanity across many seeds/notes/knob settings */
    {
        int finite_ok = 1;
        for (unsigned int i = 1; i < 200; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            e.params.loopLengthKnob01 = (double)(i % 11) / 10.0;
            noiseboy_note_on(&e, 24 + (int)(i % 72), 0.8);
            for (int s = 0; s < 4800; s++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
            }
        }
        printf("\nFull pipeline finite check across 200 seeds/settings: %d\n", finite_ok);
        if (!finite_ok) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL LOOP CHECKS PASSED\n" : "\nSOME LOOP CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
