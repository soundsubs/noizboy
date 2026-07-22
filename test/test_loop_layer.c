#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the REDESIGNED LOOP mechanism, per explicit request: now a
 * per-voice, POST-filter effect (replacing AM/wavefold), capturing a
 * chunk of the actual filtered/pitched signal rather than raw noise,
 * with a recipe-level randomized length (0.25-3.0s), a 97%-through
 * decay, and a live SPEED knob controlling playback rate through the
 * fixed captured buffer. */

static int check_finite(double x) { return !(isnan(x) || isinf(x)); }

int main(void) {
    int all_ok = 1;

    /* Test 1: loop length randomizes within the documented 0.25-3.0s
     * range, and is fixed at the recipe level (same lifecycle as
     * mixLevel01/timbreCharacterMul). */
    {
        double minLen = 100.0, maxLen = 0.0;
        for (unsigned int i = 1; i < 500; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            if (e.loopLengthSeconds < minLen) minLen = e.loopLengthSeconds;
            if (e.loopLengthSeconds > maxLen) maxLen = e.loopLengthSeconds;
            if (e.loopLengthSeconds < NOISEBOY_LOOP_MIN_SECONDS - 0.001 || e.loopLengthSeconds > NOISEBOY_LOOP_MAX_SECONDS + 0.001) {
                printf("FAILED: loopLengthSeconds out of range (seed %u, got %.4f)\n", i, e.loopLengthSeconds);
                all_ok = 0;
            }
        }
        printf("loopLengthSeconds range across 500 seeds: %.4f to %.4f (expect close to 0.25-3.0)\n", minLen, maxLen);
        if (maxLen - minLen < 2.0) { printf("FAILED: not enough variety\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 2: during capture (before a full loop length has elapsed),
     * the voice should sound completely dry -- LOOP INTENSITY should
     * have NO audible effect yet, since there's nothing captured to
     * loop. Compare intensity=0 vs intensity=1 during this window;
     * they should be identical. */
    {
        NoiseboyEngine eDry, eLoop;
        unsigned int seed = 42u;
        noiseboy_engine_init(&eDry, 48000.0, seed);
        noiseboy_engine_init(&eLoop, 48000.0, seed);
        eDry.loopLengthSeconds = 1.0;
        eLoop.loopLengthSeconds = 1.0;
        eDry.params.loopIntensity01 = 0.0;
        eLoop.params.loopIntensity01 = 1.0;
        noiseboy_note_on(&eDry, 60, 0.8);
        noiseboy_note_on(&eLoop, 60, 0.8);

        double maxDiff = 0.0;
        int finite_ok = 1;
        /* Sample partway through the 1-second capture window (well before it completes) */
        for (int i = 0; i < 24000; i++) {
            double lDry, rDry, lLoop, rLoop;
            noiseboy_process_stereo(&eDry, &lDry, &rDry);
            noiseboy_process_stereo(&eLoop, &lLoop, &rLoop);
            if (!check_finite(lDry) || !check_finite(lLoop)) finite_ok = 0;
            double diff = fabs(lDry - lLoop);
            if (diff > maxDiff) maxDiff = diff;
        }
        printf("Max difference between intensity=0 and intensity=1 DURING capture: %.8f (expect ~0 -- loop shouldn't affect anything yet)\n", maxDiff);
        if (!finite_ok) { printf("FAILED: non-finite\n"); all_ok = 0; }
        if (maxDiff > 1e-9) { printf("FAILED: LOOP INTENSITY should have zero effect during the capture phase\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 3: after capture completes, intensity=1 should differ
     * measurably from intensity=0 (the loop is now audibly doing
     * something). Use a short loop length so the test doesn't need to
     * run for seconds. */
    {
        NoiseboyEngine eDry, eLoop;
        unsigned int seed = 42u;
        noiseboy_engine_init(&eDry, 48000.0, seed);
        noiseboy_engine_init(&eLoop, 48000.0, seed);
        eDry.loopLengthSeconds = NOISEBOY_LOOP_MIN_SECONDS; /* shortest possible, 0.25s */
        eLoop.loopLengthSeconds = NOISEBOY_LOOP_MIN_SECONDS;
        eDry.params.loopIntensity01 = 0.0;
        eLoop.params.loopIntensity01 = 1.0;
        noiseboy_note_on(&eDry, 60, 0.8);
        noiseboy_note_on(&eLoop, 60, 0.8);

        int captureSamples = (int)(NOISEBOY_LOOP_MIN_SECONDS * 48000.0);
        /* Skip past the capture window */
        for (int i = 0; i < captureSamples + 100; i++) {
            double l, r;
            noiseboy_process_stereo(&eDry, &l, &r);
            noiseboy_process_stereo(&eLoop, &l, &r);
        }

        double maxDiff = 0.0;
        int finite_ok = 1;
        for (int i = 0; i < 4800; i++) {
            double lDry, rDry, lLoop, rLoop;
            noiseboy_process_stereo(&eDry, &lDry, &rDry);
            noiseboy_process_stereo(&eLoop, &lLoop, &rLoop);
            if (!check_finite(lDry) || !check_finite(lLoop)) finite_ok = 0;
            double diff = fabs(lDry - lLoop);
            if (diff > maxDiff) maxDiff = diff;
        }
        printf("Max difference between intensity=0 and intensity=1 AFTER capture completes: %.6f (expect clearly nonzero)\n", maxDiff);
        if (!finite_ok) { printf("FAILED: non-finite\n"); all_ok = 0; }
        if (maxDiff < 0.001) { printf("FAILED: LOOP should audibly affect the signal once captured\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 4: decay -- per explicit spec, content should decay to
     * near-silence by 97% of the way through each loop pass. Measure
     * the loop's OWN captured content's decay directly via a
     * synthetic capture (bypassing the full engine) for a precise,
     * isolated check. */
    {
        PostFilterLoop lp;
        if (!postfilter_loop_alloc(&lp)) {
            printf("FAILED: could not allocate test loop buffer\n");
            all_ok = 0;
        } else {
            int captureLen = 4800; /* 100ms at 48kHz, small for a fast test */
            postfilter_loop_reset(&lp, captureLen);
            /* Capture a constant 1.0 signal -- makes it trivial to
               read the decay curve directly off the output. */
            for (int i = 0; i < captureLen; i++) {
                double outL, outR;
                postfilter_loop_process(&lp, 1.0, 1.0, 1.0, &outL, &outR);
            }
            /* Now play back one full pass, recording gain at each
               point, to verify the decay shape. */
            double gainAt50pct = -1, gainAt97pct = -1, gainAt99pct = -1;
            for (int i = 0; i < captureLen; i++) {
                double outL, outR;
                postfilter_loop_process(&lp, 0.0, 0.0, 1.0, &outL, &outR);
                double frac = (double)i / (double)captureLen;
                if (frac >= 0.50 && gainAt50pct < 0) gainAt50pct = outL;
                if (frac >= 0.97 && gainAt97pct < 0) gainAt97pct = outL;
                if (frac >= 0.99 && gainAt99pct < 0) gainAt99pct = outL;
            }
            printf("Decay curve: gain at 50%%=%.4f, 97%%=%.4f, 99%%=%.4f (expect ~0.01ish or lower by 97%%, i.e. near-silent)\n",
                   gainAt50pct, gainAt97pct, gainAt99pct);
            if (gainAt97pct > 0.01) {
                printf("FAILED: should be near-silent (~-60dB or lower) by 97%% through the loop\n");
                all_ok = 0;
            } else {
                printf("PASSED\n");
            }
            if (gainAt50pct <= gainAt97pct) {
                printf("FAILED: decay should be monotonically decreasing (50%% point should still be louder than 97%% point)\n");
                all_ok = 0;
            }
            postfilter_loop_free(&lp);
        }
    }

    /* Test 5: SPEED knob (loopSpeedMul) changes playback rate through
     * the fixed captured buffer in real time -- verify a higher speed
     * completes a full pass in fewer samples. */
    {
        PostFilterLoop lpSlow, lpFast;
        postfilter_loop_alloc(&lpSlow);
        postfilter_loop_alloc(&lpFast);
        int captureLen = 4800;
        postfilter_loop_reset(&lpSlow, captureLen);
        postfilter_loop_reset(&lpFast, captureLen);
        for (int i = 0; i < captureLen; i++) {
            double outL, outR;
            postfilter_loop_process(&lpSlow, (double)i, 0.0, 1.0, &outL, &outR);
            postfilter_loop_process(&lpFast, (double)i, 0.0, 1.0, &outL, &outR);
        }
        /* Play back: slow at 1.0x, fast at 4.0x -- fast should wrap
           back to the start in 1/4 the samples. */
        int slowWrapAt = -1, fastWrapAt = -1;
        double lastReadPosSlow = 0, lastReadPosFast = 0;
        for (int i = 0; i < captureLen * 2; i++) {
            double outL, outR;
            postfilter_loop_process(&lpSlow, 0.0, 0.0, 1.0, &outL, &outR);
            postfilter_loop_process(&lpFast, 0.0, 0.0, 4.0, &outL, &outR);
            if (slowWrapAt < 0 && lpSlow.readPos < lastReadPosSlow) slowWrapAt = i;
            if (fastWrapAt < 0 && lpFast.readPos < lastReadPosFast) fastWrapAt = i;
            lastReadPosSlow = lpSlow.readPos;
            lastReadPosFast = lpFast.readPos;
        }
        printf("Loop wrap point: 1.0x speed wraps at sample %d, 4.0x speed wraps at sample %d (expect ~4x faster)\n", slowWrapAt, fastWrapAt);
        if (slowWrapAt < 0 || fastWrapAt < 0) { printf("FAILED: expected both to wrap within the test window\n"); all_ok = 0; }
        else if (fabs((double)slowWrapAt / (double)fastWrapAt - 4.0) > 0.5) {
            printf("FAILED: expected roughly 4x faster wrap with 4x speed\n");
            all_ok = 0;
        } else {
            printf("PASSED\n");
        }
        postfilter_loop_free(&lpSlow);
        postfilter_loop_free(&lpFast);
    }

    /* Test 6: fresh capture on voice reuse -- a stolen voice should
     * NOT inherit a previous note's already-captured loop content. */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, 42u);
        e.loopLengthSeconds = NOISEBOY_LOOP_MIN_SECONDS;
        noiseboy_note_on(&e, 60, 0.8);
        int captureSamples = (int)(NOISEBOY_LOOP_MIN_SECONDS * 48000.0);
        for (int i = 0; i < captureSamples + 100; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }
        int filledAfterFirstNote = e.voices[0].loop.filled;

        noiseboy_note_on(&e, 67, 0.8);
        int filledRightAfterNewNote = -1;
        for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
            if (e.voices[v].active && e.voices[v].midiNote == 67) {
                filledRightAfterNewNote = e.voices[v].loop.filled;
            }
        }
        printf("First note's loop filled after capture window: %d; new note's loop filled immediately after starting: %d (should be 0 -- fresh capture)\n",
               filledAfterFirstNote, filledRightAfterNewNote);
        if (filledAfterFirstNote != 1) { printf("FAILED: first note's loop should have completed capture\n"); all_ok = 0; }
        if (filledRightAfterNewNote != 0) { printf("FAILED: new note should start a fresh, unfilled capture, not inherit old state\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 7: full pipeline sanity -- no NaN/Inf across many
       seeds/settings, including allocation-heavy repeated re-init. */
    {
        int finite_ok = 1;
        for (unsigned int i = 1; i < 200; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, i * 7919u);
            e.params.loopIntensity01 = 0.5 + 0.5 * ((i % 2) ? 1.0 : -1.0) * 0.5;
            e.params.loopSpeedMul = 0.25 + (i % 10) * 0.8;
            noiseboy_note_on(&e, 40 + (int)(i % 48), 0.8);
            for (int s = 0; s < 2400; s++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                if (!check_finite(l) || !check_finite(r)) finite_ok = 0;
            }
        }
        printf("Full pipeline finite check across 200 seeds/settings: %d\n", finite_ok);
        if (!finite_ok) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL LOOP CHECKS PASSED\n" : "\nSOME LOOP CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
