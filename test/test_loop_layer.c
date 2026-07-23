#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Tests the REDESIGNED LOOP: a per-layer, pre-filter source (always
 * layer index 3), instantly captured at note-on, pitch-transposed by
 * note number ONLY (no knob involved in length at all, per explicit
 * request: "it doesn't need a knob to decide length, it should only
 * be tracking note number... how a real tape sampler would work").
 * Buffer length is fixed (NOISEBOY_LOOP_FIXED_SAMPLES). Knob 4 is now
 * LOOP DEPTH: the loop stays at full, sustained level for 97% of each
 * pass, then dips toward silence over the final 3%, with DEPTH
 * controlling how far that dip goes (0 = no dip, 1 = full silence) --
 * the loop's own equivalent of how AM Depth used to work. See
 * LoopSource's own header comment for the full design history. */

int main(void) {
    int all_ok = 1;

    /* Test 1: buffer length is always fixed, regardless of any knob
       setting -- no knob has any influence on it at all anymore. */
    {
        NoiseboyEngine eLowDepth, eHighDepth;
        noiseboy_engine_init(&eLowDepth, 48000.0, 42u);
        noiseboy_engine_init(&eHighDepth, 48000.0, 42u);
        eLowDepth.params.loopDepth01 = 0.0;
        eHighDepth.params.loopDepth01 = 1.0;
        noiseboy_note_on(&eLowDepth, 60, 0.8);
        noiseboy_note_on(&eHighDepth, 60, 0.8);
        /* Buffer itself has no length field anymore (removed entirely
           -- it's just always NOISEBOY_LOOP_FIXED_SAMPLES), so verify
           indirectly via the loop's own wrap period instead. */
        LoopSource *lpLow = &eLowDepth.voices[0].layers[3].loop;
        LoopSource *lpHigh = &eHighDepth.voices[0].layers[3].loop;
        printf("playbackRate identical regardless of depth knob: %.4f vs %.4f (expect identical -- depth has zero influence on length/rate)\n", lpLow->playbackRate, lpHigh->playbackRate);
        if (fabs(lpLow->playbackRate - lpHigh->playbackRate) > 1e-9) {
            printf("FAILED: depth knob should have zero influence on playback rate/length\n");
            all_ok = 0;
        } else printf("PASSED\n");
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

    /* Test 3: pitch-transposition -- higher note plays back faster,
       purely from note number, no knob involvement */
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
        } else printf("PASSED\n");
    }

    /* Test 4: available from the very first sample -- no capture delay */
    {
        LoopSource lp = {0};
        if (!loop_source_alloc(&lp)) { printf("\nFAILED: alloc failed\n"); all_ok = 0; }
        else {
            unsigned int rng = 42u;
            loop_capture(&lp, &rng, 261.6, 261.6);
            double peakVeryFirst = 0.0;
            for (int i = 0; i < 100; i++) {
                double out = loop_process(&lp, 0.0); /* depth=0 to isolate from the envelope shape */
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

    /* Test 5: envelope shape -- flat for 97%, dips toward (1-depth) over the final 3% */
    {
        LoopSource lp = {0};
        if (!loop_source_alloc(&lp)) { printf("\nFAILED: alloc failed\n"); all_ok = 0; }
        else {
            unsigned int rng = 123u;
            loop_capture(&lp, &rng, 48000.0 / NOISEBOY_LOOP_FIXED_SAMPLES, 48000.0 / NOISEBOY_LOOP_FIXED_SAMPLES); /* playbackRate=1.0 -- straightforward proportional read */

            /* depth=0: should stay flat the ENTIRE pass, no dip at all */
            double minGainDepth0 = 2.0, maxGainDepth0 = -2.0;
            for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES; i++) {
                double raw = lp.buffer[i % NOISEBOY_LOOP_FIXED_SAMPLES];
                double out = loop_process(&lp, 0.0);
                double impliedGain = (fabs(raw) > 1e-6) ? out / raw : 1.0;
                if (impliedGain < minGainDepth0) minGainDepth0 = impliedGain;
                if (impliedGain > maxGainDepth0) maxGainDepth0 = impliedGain;
            }
            printf("\nAt depth=0: implied gain range across full pass: %.4f to %.4f (expect both ~1.0 -- no dip at all)\n", minGainDepth0, maxGainDepth0);
            int depth0_ok = (minGainDepth0 > 0.99 && maxGainDepth0 < 1.01);
            if (!depth0_ok) { printf("FAILED: depth=0 should never dip\n"); all_ok = 0; }
            else printf("PASSED\n");

            /* depth=1: should be flat for 97%, then dip to exactly 0 gain by the very last sample */
            lp.readPos = 0.0;
            double gainAt50pct = 0, gainAt96pct = 0, gainAt99pct = 0, gainAtLastSample = 0;
            for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES; i++) {
                double raw = lp.buffer[i % NOISEBOY_LOOP_FIXED_SAMPLES];
                double out = loop_process(&lp, 1.0);
                double impliedGain = (fabs(raw) > 1e-6) ? out / raw : 1.0;
                double frac = (double)i / (double)NOISEBOY_LOOP_FIXED_SAMPLES;
                if (fabs(frac - 0.50) < 0.001) gainAt50pct = impliedGain;
                if (fabs(frac - 0.96) < 0.001) gainAt96pct = impliedGain;
                if (fabs(frac - 0.99) < 0.001) gainAt99pct = impliedGain;
                if (i == NOISEBOY_LOOP_FIXED_SAMPLES - 1) gainAtLastSample = impliedGain;
            }
            printf("\nAt depth=1: gain at 50%%=%.4f, 96%%=%.4f (still before the dip window), 99%%=%.4f (mid-dip), last sample=%.4f (expect near 0)\n",
                   gainAt50pct, gainAt96pct, gainAt99pct, gainAtLastSample);
            int depth1_ok = (fabs(gainAt50pct - 1.0) < 0.01) && (fabs(gainAt96pct - 1.0) < 0.01) && (gainAt99pct < gainAt96pct) && (gainAtLastSample < 0.05);
            if (!depth1_ok) { printf("FAILED: expected flat until 97%%, then a dip to ~0 by the end\n"); all_ok = 0; }
            else printf("PASSED\n");

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
        for (int i = 0; i < 48000 * 2; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }
        noiseboy_note_on(&e, 60, 0.8);
        double secondSample = e.voices[0].layers[3].loop.buffer[0];
        printf("\nFirst capture's buffer[0]=%.6f, second capture's buffer[0]=%.6f (expect different -- fresh capture each note)\n", firstSample, secondSample);
        if (fabs(firstSample - secondSample) < 1e-9) {
            printf("FAILED: expected fresh, different noise content captured each note\n");
            all_ok = 0;
        } else {
            printf("PASSED\n");
        }
    }

    /* Test 7: full pipeline sanity across many seeds/notes/depth settings */
    {
        int finite_ok = 1;
        for (unsigned int i = 1; i < 200; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            e.params.loopDepth01 = (double)(i % 11) / 10.0;
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
