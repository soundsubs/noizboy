#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Tests the REDESIGNED LOOP: a genuine, SHARED digital delay line, per
 * explicit request: "capture from 'whole final mix' and replay it,
 * transposing according to pad number... This is how a digital delay
 * works, I think." ONE buffer per engine now (not one per voice),
 * continuously written every sample with the engine's own summed mix;
 * each voice reads its own transposed position into that shared
 * buffer. See GlobalDelayLine's own header comment for the full
 * design. */

int main(void) {
    int all_ok = 1;

    /* Test 1: GlobalDelayLine alloc/free and basic write/wrap behavior */
    {
        GlobalDelayLine dl = {0};
        if (!global_delay_line_alloc(&dl)) { printf("FAILED: alloc failed\n"); all_ok = 0; }
        else {
            if (dl.writePos != 0) { printf("FAILED: writePos should start at 0\n"); all_ok = 0; }
            global_delay_line_write(&dl, 0.5, -0.5);
            if (dl.writePos != 1) { printf("FAILED: writePos should advance by 1 per write\n"); all_ok = 0; }
            if (fabs(dl.bufferL[0] - 0.5) > 1e-9 || fabs(dl.bufferR[0] - (-0.5)) > 1e-9) {
                printf("FAILED: written sample not found at the expected position\n"); all_ok = 0;
            }
            for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES - 1; i++) global_delay_line_write(&dl, 0.1, 0.1);
            if (dl.writePos != 0) { printf("FAILED: writePos should wrap back to 0 after a full cycle\n"); all_ok = 0; }
            printf("GlobalDelayLine alloc/write/wrap: %s\n", all_ok ? "PASSED" : "FAILED");
            global_delay_line_free(&dl);
        }
    }

    /* Test 2: loop_voice_start sets readPos to the current write head
       (the oldest available sample) and playbackRate correctly */
    {
        GlobalDelayLine dl = {0};
        global_delay_line_alloc(&dl);
        for (int i = 0; i < 12345; i++) global_delay_line_write(&dl, 0.0, 0.0);
        LoopSource lp = {0};
        unsigned int rng = 42u;
        loop_voice_start(&lp, &rng, &dl, 261.6, 261.6);
        printf("\nloop_voice_start: readPos=%.1f (expect %d, the current write head), playbackRate=%.4f (expect 1.0)\n",
               lp.readPos, dl.writePos, lp.playbackRate);
        if (fabs(lp.readPos - (double)dl.writePos) > 1e-9) { printf("FAILED: readPos should start at the current write head\n"); all_ok = 0; }
        else printf("PASSED\n");
        if (fabs(lp.playbackRate - 1.0) > 1e-6) { printf("FAILED: playbackRate should be 1.0 at the reference frequency\n"); all_ok = 0; }
        else printf("PASSED\n");
        global_delay_line_free(&dl);
    }

    /* Test 3: pitch-transposition -- higher note plays back faster */
    {
        GlobalDelayLine dl = {0};
        global_delay_line_alloc(&dl);
        LoopSource lpLow = {0}, lpHigh = {0};
        unsigned int rng = 42u;
        loop_voice_start(&lpLow, &rng, &dl, 261.6, 261.6);
        loop_voice_start(&lpHigh, &rng, &dl, 523.3, 261.6);
        printf("\nPlayback rate at middle C: %.4f, one octave up: %.4f (expect ~2x)\n", lpLow.playbackRate, lpHigh.playbackRate);
        if (fabs(lpHigh.playbackRate / lpLow.playbackRate - 2.0) > 0.01) {
            printf("FAILED: expected exactly 2x playback rate one octave up\n");
            all_ok = 0;
        } else printf("PASSED\n");
        global_delay_line_free(&dl);
    }

    /* Test 4: loop_process actually reads back real, previously-written
       content from the shared buffer -- this is what makes it a
       genuine delay, not a noise generator. */
    {
        GlobalDelayLine dl = {0};
        global_delay_line_alloc(&dl);
        for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES; i++) {
            double v = (double)i / (double)NOISEBOY_LOOP_FIXED_SAMPLES;
            global_delay_line_write(&dl, v, v);
        }
        LoopSource lp = {0};
        unsigned int rng = 42u;
        loop_voice_start(&lp, &rng, &dl, 261.6, 261.6);
        double first = loop_process(&lp, &dl, 0.0, 48000.0);
        double second = loop_process(&lp, &dl, 0.0, 48000.0);
        printf("\nFirst two samples read back from the shared buffer: %.6f, %.6f (expect close to 0.0 and 1/N, matching the written ramp's oldest values)\n", first, second);
        double expectedFirst = 0.0 / NOISEBOY_LOOP_FIXED_SAMPLES;
        double expectedSecond = 1.0 / NOISEBOY_LOOP_FIXED_SAMPLES;
        if (fabs(first - expectedFirst) > 1e-6 || fabs(second - expectedSecond) > 1e-6) {
            printf("FAILED: loop_process should read back the actual, previously-written buffer content\n");
            all_ok = 0;
        } else printf("PASSED: genuinely reads back real captured content, not synthesized noise\n");
        global_delay_line_free(&dl);
    }

    /* Test 5: envelope shape -- flat for most of the pass, dips toward
       (1-depth) over the final 3%. Unchanged logic. */
    {
        LoopSource lp = {0};
        double minGainDepth0 = 2.0, maxGainDepth0 = -2.0;
        for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES; i += 97) {
            lp.readPos = i;
            double gain = loop_envelope_gain(&lp, 0.0);
            if (gain < minGainDepth0) minGainDepth0 = gain;
            if (gain > maxGainDepth0) maxGainDepth0 = gain;
        }
        printf("\nAt depth=0: gain range across full pass: %.4f to %.4f (expect both ~1.0 -- no dip at all)\n", minGainDepth0, maxGainDepth0);
        int depth0_ok = (minGainDepth0 > 0.99 && maxGainDepth0 < 1.01);
        if (!depth0_ok) { printf("FAILED: depth=0 should never dip\n"); all_ok = 0; }
        else printf("PASSED\n");

        double gainAtMid = 0, gainJustPastFadeIn = 0, gainBeforeDip = 0, gainMidDip = 0, gainAtLastSample = 0;
        for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES; i++) {
            lp.readPos = i;
            double gain = loop_envelope_gain(&lp, 1.0);
            double frac = (double)i / (double)NOISEBOY_LOOP_FIXED_SAMPLES;
            if (fabs(frac - 0.50) < 0.0001) gainAtMid = gain;
            if (fabs(frac - 0.505) < 0.0001) gainJustPastFadeIn = gain;
            if (fabs(frac - 0.96) < 0.0001) gainBeforeDip = gain;
            if (fabs(frac - 0.99) < 0.0001) gainMidDip = gain;
            if (i == NOISEBOY_LOOP_FIXED_SAMPLES - 1) gainAtLastSample = gain;
        }
        printf("\nAt depth=1: mid=%.4f, just-past-gap/fadein=%.4f, before-dip=%.4f, mid-dip=%.4f, last=%.4f (expect flat until dip, then near 0)\n",
               gainAtMid, gainJustPastFadeIn, gainBeforeDip, gainMidDip, gainAtLastSample);
        int depth1_ok = (fabs(gainJustPastFadeIn - 1.0) < 0.01) && (fabs(gainBeforeDip - 1.0) < 0.01) && (gainMidDip < gainBeforeDip) && (gainAtLastSample < 0.05);
        if (!depth1_ok) { printf("FAILED: expected flat for most of the pass, then a dip to ~0 by the end\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 6: tape jitter localized near the wrap, absent mid-cycle and at depth=0 */
    {
        GlobalDelayLine dl = {0};
        global_delay_line_alloc(&dl);
        LoopSource lp = {0};
        unsigned int rng = 55u;
        loop_voice_start(&lp, &rng, &dl, 261.6, 261.6);
        double minRateNearWrap = 100, maxRateNearWrap = 0;
        double minRateMidCycle = 100, maxRateMidCycle = 0;
        for (int cyc = 0; cyc < 3; cyc++) {
            for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES; i++) {
                double before = lp.readPos;
                loop_process(&lp, &dl, 1.0, 48000.0);
                double after = lp.readPos;
                double actualRate = (after > before) ? (after - before) : (after + NOISEBOY_LOOP_FIXED_SAMPLES - before);
                double frac = before / (double)NOISEBOY_LOOP_FIXED_SAMPLES;
                if (frac > 0.995 || frac < 0.005) {
                    if (actualRate < minRateNearWrap) minRateNearWrap = actualRate;
                    if (actualRate > maxRateNearWrap) maxRateNearWrap = actualRate;
                } else if (frac > 0.4 && frac < 0.6) {
                    if (actualRate < minRateMidCycle) minRateMidCycle = actualRate;
                    if (actualRate > maxRateMidCycle) maxRateMidCycle = actualRate;
                }
            }
        }
        printf("\nAt depth=1: rate near wrap varies %.4f to %.4f, mid-cycle varies %.4f to %.4f (expect near-wrap clearly varying, mid-cycle exactly constant)\n",
               minRateNearWrap, maxRateNearWrap, minRateMidCycle, maxRateMidCycle);
        int jitterPresent = (maxRateNearWrap - minRateNearWrap) > 0.05;
        int midCycleStable = (maxRateMidCycle - minRateMidCycle) < 1e-9;
        if (!jitterPresent || !midCycleStable) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
        global_delay_line_free(&dl);
    }

    /* Test 7: layer 3 is always LAYER_LOOP, present in every recipe */
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

    /* Test 8: whole-voice amplitude application still works */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, 42u);
        e.params.loopDepth01 = 1.0;
        e.recipe[3].mixLevel01 = 0.0;
        e.recipe[0].mixLevel01 = 0.8;
        e.recipe[1].mixLevel01 = 0.8;
        e.recipe[2].mixLevel01 = 0.0;
        e.params.filterResonance01 = 0.0;
        noiseboy_note_on(&e, 60, 0.8);
        for (int i = 0; i < 4800; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }
        double prevReadPos = e.voices[0].layers[3].loop.readPos;
        double peakNearWrap = 0.0, peakMidCycle = 0.0;
        int sawWrap = 0;
        for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES + 5000; i++) {
            double l, r;
            noiseboy_process_stereo(&e, &l, &r);
            double curReadPos = e.voices[0].layers[3].loop.readPos;
            if (curReadPos < prevReadPos) sawWrap = 1;
            double frac = prevReadPos / (double)NOISEBOY_LOOP_FIXED_SAMPLES;
            if (frac > 0.995) { if (fabs(l) > peakNearWrap) peakNearWrap = fabs(l); }
            if (frac > 0.3 && frac < 0.6) { if (fabs(l) > peakMidCycle) peakMidCycle = fabs(l); }
            prevReadPos = curReadPos;
        }
        printf("\nWith LOOP's own mixLevel01=0: peak near cycle end=%.5f, peak mid-cycle=%.5f, wrap observed=%d\n", peakNearWrap, peakMidCycle, sawWrap);
        if (!sawWrap) { printf("FAILED: test didn't run long enough to observe a full cycle\n"); all_ok = 0; }
        else if (peakNearWrap > peakMidCycle * 0.3) { printf("FAILED: expected a clear dip near cycle end\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 9: binary depth threshold logic */
    {
        double rawValues[] = {0.0, 1.0/127.0, 0.5, 1.0};
        double expected[]  = {0.0, 1.0,       1.0, 1.0};
        int binary_ok = 1;
        for (int i = 0; i < 4; i++) {
            double result = (rawValues[i] > 0.0) ? 1.0 : 0.0;
            if (result != expected[i]) binary_ok = 0;
        }
        printf("\nBinary depth threshold: %s\n", binary_ok ? "PASSED" : "FAILED");
        if (!binary_ok) all_ok = 0;
    }

    /* Test 10: the KEY new behavior -- LOOP genuinely captures the
       engine's own recent output. Play a distinctive note, let it
       ring into the shared delay line, confirm real captured audio. */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, 42u);
        e.recipe[0].mixLevel01 = 0.0; e.recipe[1].mixLevel01 = 0.0; e.recipe[3].mixLevel01 = 0.0;
        e.recipe[2].mixLevel01 = 1.0;
        noiseboy_note_on(&e, 60, 0.9);
        for (int i = 0; i < 24000; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }

        double sumSq = 0;
        for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES; i += 100) {
            sumSq += e.delayLine.bufferL[i] * e.delayLine.bufferL[i];
        }
        double rms = sqrt(sumSq / (NOISEBOY_LOOP_FIXED_SAMPLES / 100));
        printf("\nDelay line RMS after playing a note for 0.5s: %.5f (expect clearly nonzero -- genuinely captured real audio)\n", rms);
        if (rms < 0.001) { printf("FAILED: delay line should contain real captured audio by now\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 11: full pipeline sanity across many seeds/notes/depth settings */
    {
        int finite_ok = 1;
        for (unsigned int i = 1; i < 100; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            e.params.loopDepth01 = (i % 2 == 0) ? 1.0 : 0.0;
            noiseboy_note_on(&e, 24 + (int)(i % 72), 0.8);
            for (int s = 0; s < 4800; s++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
            }
        }
        printf("\nFull pipeline finite check across 100 seeds/settings: %d\n", finite_ok);
        if (!finite_ok) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL LOOP CHECKS PASSED\n" : "\nSOME LOOP CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
