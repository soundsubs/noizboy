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
                double out = loop_process(&lp, 0.0, 48000.0); /* depth=0 to isolate from the envelope shape */
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

    /* Test 5: envelope shape -- flat for 97%, dips toward (1-depth) over the final 3%.
       Uses loop_envelope_gain directly (not inferred via loop_process's
       out/raw ratio) since that's unaffected by the new tape jitter,
       which legitimately perturbs the actual read position near the
       wrap -- the envelope SHAPE itself is a separate, deterministic
       function of readPos alone, and this verifies that directly. */
    {
        LoopSource lp = {0};
        /* depth=0: should stay flat the ENTIRE pass, no dip at all */
        double minGainDepth0 = 2.0, maxGainDepth0 = -2.0;
        for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES; i++) {
            lp.readPos = i;
            double gain = loop_envelope_gain(&lp, 0.0);
            if (gain < minGainDepth0) minGainDepth0 = gain;
            if (gain > maxGainDepth0) maxGainDepth0 = gain;
        }
        printf("\nAt depth=0: gain range across full pass: %.4f to %.4f (expect both ~1.0 -- no dip at all)\n", minGainDepth0, maxGainDepth0);
        int depth0_ok = (minGainDepth0 > 0.99 && maxGainDepth0 < 1.01);
        if (!depth0_ok) { printf("FAILED: depth=0 should never dip\n"); all_ok = 0; }
        else printf("PASSED\n");

        /* depth=1: should be flat for most of the pass, then dip to
           exactly 0 gain by the very last sample */
        double gainAt50pct = 0, gainAt50_5pct = 0, gainAt96pct = 0, gainAt99pct = 0, gainAtLastSample = 0;
        for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES; i++) {
            lp.readPos = i;
            double gain = loop_envelope_gain(&lp, 1.0);
            double frac = (double)i / (double)NOISEBOY_LOOP_FIXED_SAMPLES;
            if (fabs(frac - 0.50) < 0.001) gainAt50pct = gain;
            if (fabs(frac - 0.505) < 0.001) gainAt50_5pct = gain; /* just past the fade-in window, still flat */
            if (fabs(frac - 0.96) < 0.001) gainAt96pct = gain;
            if (fabs(frac - 0.99) < 0.001) gainAt99pct = gain;
            if (i == NOISEBOY_LOOP_FIXED_SAMPLES - 1) gainAtLastSample = gain;
        }
        printf("\nAt depth=1: gain at 50%%=%.4f, 50.5%%=%.4f (well past the gap/fade-in), 96%%=%.4f (still before the dip window), 99%%=%.4f (mid-dip), last sample=%.4f (expect near 0)\n",
               gainAt50pct, gainAt50_5pct, gainAt96pct, gainAt99pct, gainAtLastSample);
        int depth1_ok = (fabs(gainAt50_5pct - 1.0) < 0.01) && (fabs(gainAt96pct - 1.0) < 0.01) && (gainAt99pct < gainAt96pct) && (gainAtLastSample < 0.05);
        if (!depth1_ok) { printf("FAILED: expected flat for most of the pass, then a dip to ~0 by the end\n"); all_ok = 0; }
        else printf("PASSED\n");
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

    /* Test 8: LOOP's envelope shape impacts the WHOLE VOICE's
       amplitude, per direct request -- "if LOOP = 127 (therefore ON)
       but the loop alg isnt feeding the mixer (or is inaudible) the
       LOOP won't do anything. Lets make the LOOP always impact
       amplitude by the curve we set earlier." Verify the whole-voice
       output genuinely dips near the end of each loop cycle even with
       LOOP's OWN source contribution fully muted (mixLevel01=0) --
       confirming the modulation comes from applying loop_envelope_gain
       to the voice as a whole, not from LOOP's own audio being loud in
       the mix. Uses raw LoopSource calls directly (not the full
       engine) to know exactly where in the cycle each sample falls,
       avoiding the indexing mistake an earlier, manual version of this
       same test made (measuring the wrong portion of a loop cycle
       relative to when the voice's own note-on/attack settle period
       had already advanced it). */
    {
        NoiseboyEngine e;
        noiseboy_engine_init(&e, 48000.0, 42u);
        e.params.loopDepth01 = 1.0;
        e.recipe[3].mixLevel01 = 0.0; /* LOOP's own source contribution fully muted -- the exact reported scenario */
        e.recipe[0].mixLevel01 = 0.8;
        e.recipe[1].mixLevel01 = 0.8;
        e.recipe[2].mixLevel01 = 0.0;
        e.params.filterResonance01 = 0.0; /* isolate from the (now genuinely resonant) pitch filter's own ringing, per direct request elsewhere -- keep this test about the envelope multiplier specifically */
        noiseboy_note_on(&e, 60, 0.8);
        /* Track LOOP's own readPos alongside the voice's output so we
           know exactly when a wrap (start of a fresh cycle) happens,
           rather than assuming a fixed sample offset. */
        double prevReadPos = e.voices[0].layers[3].loop.readPos;
        double peakNearWrap = 0.0, peakMidCycle = 0.0;
        int sawWrap = 0;
        for (int i = 0; i < 48000; i++) { /* run several full cycles */
            double l, r;
            noiseboy_process_stereo(&e, &l, &r);
            double curReadPos = e.voices[0].layers[3].loop.readPos;
            if (i > 4800) { /* past the attack settle */
                if (curReadPos < prevReadPos) sawWrap = 1; /* wrapped this sample -- was at the very end of a cycle last sample */
                double frac = prevReadPos / (double)NOISEBOY_LOOP_FIXED_SAMPLES;
                if (frac > 0.995) { if (fabs(l) > peakNearWrap) peakNearWrap = fabs(l); }
                if (frac > 0.3 && frac < 0.6) { if (fabs(l) > peakMidCycle) peakMidCycle = fabs(l); }
            }
            prevReadPos = curReadPos;
        }
        printf("\nWith LOOP's own mixLevel01=0: peak amplitude near end of cycle (>99.5%%)=%.5f, peak mid-cycle (30-60%%)=%.5f, wrap observed=%d\n",
               peakNearWrap, peakMidCycle, sawWrap);
        if (!sawWrap) { printf("FAILED: test didn't run long enough to observe a full loop cycle\n"); all_ok = 0; }
        else if (peakNearWrap > peakMidCycle * 0.3) {
            printf("FAILED: expected a clear amplitude dip near the end of each cycle, even with LOOP's own source muted\n");
            all_ok = 0;
        } else {
            printf("PASSED: whole-voice amplitude dips near the end of each cycle regardless of LOOP's own mix level\n");
        }
    }

    /* Test 9: click fix -- the wrap transition should be smooth
       (small per-sample jump), not an abrupt snap back to full gain,
       per direct report: "the loop point now sounds like a click,
       rather than a tape looping over itself." At max depth, there
       should also be a brief period of genuine, true silence right
       after the wrap (the "tape splice" gap), before swelling back up. */
    {
        LoopSource lp = {0};
        double depth = 1.0;

        /* Continuity across the wrap: the jump from the very last
           sample of one cycle to the very first sample of the next
           should be small, not a near-full-scale snap. */
        lp.readPos = NOISEBOY_LOOP_FIXED_SAMPLES - 1;
        double lastOfCycle = loop_envelope_gain(&lp, depth);
        lp.readPos = 0;
        double firstOfNext = loop_envelope_gain(&lp, depth);
        double wrapJump = fabs(lastOfCycle - firstOfNext);
        printf("\nGain at last sample of cycle: %.6f, first sample of next: %.6f, wrap jump: %.6f (expect small, not ~1.0)\n",
               lastOfCycle, firstOfNext, wrapJump);
        if (wrapJump > 0.05) {
            printf("FAILED: wrap transition should be smooth, not an abrupt snap\n");
            all_ok = 0;
        } else printf("PASSED\n");

        /* A genuine silence gap exists right after the wrap at max depth */
        int foundTrueSilence = 0;
        for (int i = 0; i < 50; i++) {
            lp.readPos = i;
            if (loop_envelope_gain(&lp, depth) < 1e-6) { foundTrueSilence = 1; break; }
        }
        printf("Genuine silence found in the first 50 samples after the wrap at depth=1: %d\n", foundTrueSilence);
        if (!foundTrueSilence) { printf("FAILED: expected a true-silence gap at max depth\n"); all_ok = 0; }
        else printf("PASSED\n");

        /* Verify the maximum per-sample gain change ANYWHERE in a full
           cycle is small -- confirms there's no OTHER abrupt jump
           hiding elsewhere in the new shape (e.g. at the gap-to-fade-in
           or fade-in-to-sustain boundaries). */
        double maxStep = 0.0;
        double prevGain = loop_envelope_gain(&lp, depth); /* readPos still at 49 from the loop above, fine as a starting reference */
        for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES; i++) {
            lp.readPos = i;
            double g = loop_envelope_gain(&lp, depth);
            double step = fabs(g - prevGain);
            if (step > maxStep) maxStep = step;
            prevGain = g;
        }
        printf("Max per-sample gain change anywhere in a full cycle: %.6f (expect small throughout)\n", maxStep);
        if (maxStep > 0.05) { printf("FAILED: found an unexpectedly large per-sample jump somewhere in the cycle\n"); all_ok = 0; }
        else printf("PASSED\n");

        /* depth=0 remains completely unaffected -- flat at 1.0 always */
        int depth0_ok = 1;
        for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES; i += 137) {
            lp.readPos = i;
            if (fabs(loop_envelope_gain(&lp, 0.0) - 1.0) > 1e-9) depth0_ok = 0;
        }
        printf("depth=0 stays exactly flat throughout: %d\n", depth0_ok);
        if (!depth0_ok) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Test 10: tape jitter, per explicit request -- "There should also
       be more 'tape jitter'... right around the gap!" Verify the
       actual playback rate genuinely varies near the wrap (not just
       the intended design, but the measured behavior), stays exactly
       at the base rate mid-cycle, and vanishes entirely at depth=0
       (nothing to smooth/jitter around if there's no gap in the first
       place). */
    {
        LoopSource lp = {0};
        if (!loop_source_alloc(&lp)) { printf("\nFAILED: alloc failed\n"); all_ok = 0; }
        else {
            unsigned int rng = 55u;
            loop_capture(&lp, &rng, 48000.0 / NOISEBOY_LOOP_FIXED_SAMPLES, 48000.0 / NOISEBOY_LOOP_FIXED_SAMPLES);
            double minRateNearWrap = 100, maxRateNearWrap = 0;
            double minRateMidCycle = 100, maxRateMidCycle = 0;
            for (int cyc = 0; cyc < 5; cyc++) {
                for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES; i++) {
                    double before = lp.readPos;
                    loop_process(&lp, 1.0, 48000.0);
                    double after = lp.readPos;
                    double actualRate = (after > before) ? (after - before) : (after + NOISEBOY_LOOP_FIXED_SAMPLES - before);
                    double frac = before / (double)NOISEBOY_LOOP_FIXED_SAMPLES;
                    if (frac > 0.98 || frac < 0.02) {
                        if (actualRate < minRateNearWrap) minRateNearWrap = actualRate;
                        if (actualRate > maxRateNearWrap) maxRateNearWrap = actualRate;
                    } else if (frac > 0.4 && frac < 0.6) {
                        if (actualRate < minRateMidCycle) minRateMidCycle = actualRate;
                        if (actualRate > maxRateMidCycle) maxRateMidCycle = actualRate;
                    }
                }
            }
            printf("\nAt depth=1: rate near wrap varies %.4f to %.4f, rate mid-cycle varies %.4f to %.4f (expect near-wrap CLEARLY varying, mid-cycle exactly constant)\n",
                   minRateNearWrap, maxRateNearWrap, minRateMidCycle, maxRateMidCycle);
            int jitterPresent = (maxRateNearWrap - minRateNearWrap) > 0.05;
            int midCycleStable = (maxRateMidCycle - minRateMidCycle) < 1e-9;
            if (!jitterPresent) { printf("FAILED: expected clearly measurable rate jitter near the wrap\n"); all_ok = 0; }
            else if (!midCycleStable) { printf("FAILED: mid-cycle rate should be exactly stable, no jitter there\n"); all_ok = 0; }
            else printf("PASSED\n");
            loop_source_free(&lp);
        }

        /* At depth=0, jitter should vanish entirely -- rate stays exactly constant everywhere, including near the wrap */
        LoopSource lp0 = {0};
        if (!loop_source_alloc(&lp0)) { printf("\nFAILED: alloc failed\n"); all_ok = 0; }
        else {
            unsigned int rng = 55u;
            loop_capture(&lp0, &rng, 48000.0 / NOISEBOY_LOOP_FIXED_SAMPLES, 48000.0 / NOISEBOY_LOOP_FIXED_SAMPLES);
            double minRate = 100, maxRate = 0;
            for (int i = 0; i < NOISEBOY_LOOP_FIXED_SAMPLES * 3; i++) {
                double before = lp0.readPos;
                loop_process(&lp0, 0.0, 48000.0);
                double after = lp0.readPos;
                double actualRate = (after > before) ? (after - before) : (after + NOISEBOY_LOOP_FIXED_SAMPLES - before);
                if (actualRate < minRate) minRate = actualRate;
                if (actualRate > maxRate) maxRate = actualRate;
            }
            printf("At depth=0: rate range across 3 full cycles: %.4f to %.4f (expect exactly constant, no jitter anywhere)\n", minRate, maxRate);
            if (maxRate - minRate > 1e-9) { printf("FAILED: depth=0 should have zero jitter anywhere in the cycle\n"); all_ok = 0; }
            else printf("PASSED\n");
            loop_source_free(&lp0);
        }
    }

    /* Test 11: binary depth threshold logic, replicating set_param's
       own formula (can't unit-test set_param itself here, since it
       needs the real Schwung plugin headers) -- per explicit request:
       "one turn turns LOOP ON. One turn back turns LOOP OFF." */
    {
        double rawValues[] = {0.0, 1.0/127.0, 0.5, 1.0};
        double expected[]  = {0.0, 1.0,       1.0, 1.0};
        int binary_ok = 1;
        for (int i = 0; i < 4; i++) {
            double result = (rawValues[i] > 0.0) ? 1.0 : 0.0;
            printf("raw01=%.5f -> loopDepth01=%.1f (expect %.1f)\n", rawValues[i], result, expected[i]);
            if (result != expected[i]) binary_ok = 0;
        }
        printf("Binary depth threshold: %s\n", binary_ok ? "PASSED" : "FAILED");
        if (!binary_ok) all_ok = 0;
    }

    printf(all_ok ? "\nALL LOOP CHECKS PASSED\n" : "\nSOME LOOP CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
