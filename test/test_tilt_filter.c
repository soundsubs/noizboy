#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the REDESIGNED TILT filter, per explicit request: "Instead
 * of a lowpass/hipass scheme, let's make it more TILT positively or
 * negatively from 50%... It should roll off below 100hz, and steeply
 * decline starting at 10000hz... the TILT shifts this either higher
 * or lower." */

/* Measures gain at a specific test frequency by feeding a sine wave
   and measuring RMS of the (settled) output relative to input. */
static double measure_gain_at_freq(double tilt01, double testFreqHz, double sampleRate) {
    TiltFilter t;
    tilt_filter_init(&t, sampleRate);
    /* Force the smoothed value directly to skip the ~15ms glide --
       isolates the filter's own frequency response from the knob
       smoothing, which is tested separately. */
    t.smoothedTilt01 = tilt01;

    double phase = 0.0;
    double phaseInc = 2.0 * M_PI * testFreqHz / sampleRate;
    int settleSamples = (int)(sampleRate * 0.05); /* 50ms settle */
    int measureSamples = (int)(sampleRate * 0.05);

    for (int i = 0; i < settleSamples; i++) {
        double l = sin(phase), r = l;
        tilt_filter_process(&t, &l, &r, tilt01, sampleRate);
        phase += phaseInc;
    }
    double sumSqIn = 0.0, sumSqOut = 0.0;
    for (int i = 0; i < measureSamples; i++) {
        double inSample = sin(phase);
        double l = inSample, r = inSample;
        tilt_filter_process(&t, &l, &r, tilt01, sampleRate);
        sumSqIn += inSample * inSample;
        sumSqOut += l * l;
        phase += phaseInc;
    }
    double rmsIn = sqrt(sumSqIn / measureSamples);
    double rmsOut = sqrt(sumSqOut / measureSamples);
    return rmsOut / rmsIn;
}

int main(void) {
    int all_ok = 1;
    double sr = 48000.0;

    /* Test 1: at neutral tilt (0.5), verify the base tape-bandwidth
       window -- roughly flat in the passband, rolled off below 100Hz
       and above 10kHz. */
    {
        double gain50 = measure_gain_at_freq(0.5, 50.0, sr);     /* below the 100Hz edge */
        double gain1k = measure_gain_at_freq(0.5, 1000.0, sr);   /* well within the passband */
        double gain20k = measure_gain_at_freq(0.5, 20000.0, sr); /* well above the 10kHz edge (Nyquist guard permitting) */
        printf("Neutral tilt (0.5): gain@50Hz=%.3f, gain@1kHz=%.3f, gain@20kHz=%.3f\n", gain50, gain1k, gain20k);
        if (gain1k < 0.7) { printf("FAILED: 1kHz should pass through mostly unattenuated in the tape window\n"); all_ok = 0; }
        /* Real bug caught here in an earlier version of this filter:
         * a weak assertion (just "gain50 < gain1k") passed even when
         * 50Hz was barely attenuated at all (-0.5dB, not a meaningful
         * "roll off") due to a nonlinear interaction between the two
         * filter stages being combined in the wrong order. Require a
         * genuine, meaningful attenuation instead -- at least 2dB
         * (gain <= ~0.79) one octave below the 100Hz edge. */
        if (gain50 > 0.79) { printf("FAILED: 50Hz should show a MEANINGFUL roll off (at least ~2dB), not just any nonzero difference\n"); all_ok = 0; }
        if (gain20k >= gain1k * 0.5) { printf("FAILED: 20kHz should be substantially rolled off relative to 1kHz\n"); all_ok = 0; }
        if (all_ok) printf("PASSED\n");
    }

    /* Test 2: tilt never fully silences the signal, unlike the old
       design -- even at extreme settings, SOME signal should get
       through (verified at 1kHz, comfortably inside the window at
       neutral, to see how much tilt eats into it at the extremes). */
    {
        double gainBassExtreme = measure_gain_at_freq(0.0, 1000.0, sr);
        double gainTrebleExtreme = measure_gain_at_freq(1.0, 1000.0, sr);
        printf("\nAt 1kHz: full bass-emphasis tilt (0.0) gain=%.4f, full treble-emphasis tilt (1.0) gain=%.4f (expect neither to be near-zero)\n",
               gainBassExtreme, gainTrebleExtreme);
        if (gainBassExtreme < 0.01 || gainTrebleExtreme < 0.01) {
            printf("FAILED: TILT should never fully silence the signal, unlike the old lowpass/highpass sweep design\n");
            all_ok = 0;
        } else {
            printf("PASSED\n");
        }
    }

    /* Test 3: tilt actually shifts the balance -- bass-emphasis tilt
       should boost low frequencies RELATIVE to high, and vice versa. */
    {
        double lowAtBassEmphasis = measure_gain_at_freq(0.0, 300.0, sr);
        double highAtBassEmphasis = measure_gain_at_freq(0.0, 8000.0, sr);
        double lowAtTrebleEmphasis = measure_gain_at_freq(1.0, 300.0, sr);
        double highAtTrebleEmphasis = measure_gain_at_freq(1.0, 8000.0, sr);
        double ratioBassEmphasis = lowAtBassEmphasis / (highAtBassEmphasis + 1e-9);
        double ratioTrebleEmphasis = lowAtTrebleEmphasis / (highAtTrebleEmphasis + 1e-9);
        printf("\nLow/high ratio at bass-emphasis tilt: %.3f, at treble-emphasis tilt: %.3f (expect the former clearly higher)\n",
               ratioBassEmphasis, ratioTrebleEmphasis);
        if (ratioBassEmphasis <= ratioTrebleEmphasis) {
            printf("FAILED: bass-emphasis tilt should favor low frequencies relative to treble-emphasis tilt\n");
            all_ok = 0;
        } else {
            printf("PASSED\n");
        }
    }

    /* Test 4: knob smoothing -- an instant target change shouldn't
       produce an instant jump in the actual filtered cutoffs. */
    {
        TiltFilter t;
        tilt_filter_init(&t, sr);
        double l = 0.0, r = 0.0;
        tilt_filter_process(&t, &l, &r, 0.5, sr);
        double before = t.smoothedTilt01;
        tilt_filter_process(&t, &l, &r, 1.0, sr); /* instant jump in target */
        double after = t.smoothedTilt01;
        printf("\nSmoothing: before=%.4f, after one sample toward a full jump=%.4f (expect a small step, not an instant jump to 1.0)\n", before, after);
        if (fabs(after - 1.0) < 0.01) {
            printf("FAILED: smoothing should prevent an instant jump\n");
            all_ok = 0;
        } else {
            printf("PASSED\n");
        }
    }

    /* Test 5: full pipeline sanity across many seeds and tilt settings */
    {
        int finite_ok = 1;
        for (unsigned int i = 1; i < 100; i++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, sr, i * 7919u);
            noiseboy_note_on(&e, 40 + (int)(i % 48), 0.8);
            TiltFilter t;
            tilt_filter_init(&t, sr);
            double tiltSetting = (double)(i % 11) / 10.0;
            for (int s = 0; s < 4800; s++) {
                double l, r;
                noiseboy_process_stereo(&e, &l, &r);
                tilt_filter_process(&t, &l, &r, tiltSetting, sr);
                if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) finite_ok = 0;
            }
        }
        printf("\nFull pipeline sanity across 100 seeds/tilt settings: %d\n", finite_ok);
        if (!finite_ok) { printf("FAILED\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL TILT FILTER CHECKS PASSED\n" : "\nSOME TILT FILTER CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
