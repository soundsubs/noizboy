#include "noiseboy_dsp.h"
#include "dbcell_dsp.h"
#include <stdio.h>
#include <math.h>

/* RE-PURPOSED per direct report: after v0.16.0 removed the dedicated
 * noise gate (verified at the time to leave idle output at roughly
 * -55 to -58dB relative to a played note), that turned out to still
 * be audibly present in practice ("I can hear db-cell at the end
 * making sound"). The gate is back, now positioned AFTER TILT (TILT
 * used to sit where the gate now sits again) -- this test verifies
 * the RESTORED gate actually produces genuine silence when idle, not
 * just "quiet". */

int main(void) {
    int all_ok = 1;
    NoiseboyEngine e;
    noiseboy_engine_init(&e, 48000.0, 42u);
    /* No notes played -- e has zero active voices throughout */

    DbCellEngine dbcell;
    dbcell_engine_init(&dbcell, 48000.0, 999u);

    TiltFilter tilt;
    tilt_filter_init(&tilt, 48000.0);

    NoiseboyOutputGate gate;
    noiseboy_output_gate_init(&gate);

    double sumSqUngated = 0.0, sumSqGated = 0.0, peakGated = 0.0;
    int n = 48000; /* 1 second */
    for (int i = 0; i < n; i++) {
        double l, r;
        noiseboy_process_stereo(&e, &l, &r); /* silent -- no voices */
        dbcell_process(&dbcell, &l, &r);      /* db-cell's own always-on noise */
        tilt_filter_process(&tilt, &l, &r, 0.5, 48000.0);
        sumSqUngated += l * l;

        int voicesActive = noiseboy_any_voice_active(&e); /* always 0 here */
        l = noiseboy_output_gate_process(&gate, l, voicesActive, 48000.0);
        r = noiseboy_output_gate_process(&gate, r, voicesActive, 48000.0);
        sumSqGated += l * l;
        if (fabs(l) > peakGated) peakGated = fabs(l);
    }
    double rmsUngated = sqrt(sumSqUngated / n);
    double rmsGated = sqrt(sumSqGated / n);
    printf("Idle instrument (zero voices), after TILT: RMS=%.5f. After gate too: RMS=%.8f, peak=%.8f\n",
           rmsUngated, rmsGated, peakGated);

    /* The gate should reduce this to genuine, effectively-inaudible silence */
    if (rmsGated > 0.0001) {
        printf("FAILED: gate should reduce idle output to effective silence, not just 'quiet'\n");
        all_ok = 0;
    } else {
        printf("PASSED: gate produces genuine silence when idle\n");
    }

    /* Verify the gate opens promptly and passes signal through cleanly once a voice is active */
    {
        NoiseboyOutputGate g2;
        noiseboy_output_gate_init(&g2);
        double out = 0.0;
        for (int i = 0; i < 4800; i++) { /* 100ms of "voice active" */
            out = noiseboy_output_gate_process(&g2, 1.0, 1, 48000.0);
        }
        printf("\nGate envelope after 100ms with a voice active: %.4f (expect close to 1.0 -- fully open)\n", out);
        if (out < 0.99) { printf("FAILED: gate should be fully open well within 100ms\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    /* Verify no click: transition from active to inactive should be smooth (release, not instant cutoff) */
    {
        NoiseboyOutputGate g3;
        noiseboy_output_gate_init(&g3);
        for (int i = 0; i < 4800; i++) noiseboy_output_gate_process(&g3, 1.0, 1, 48000.0); /* open it up */
        double maxJump = 0.0, prev = noiseboy_output_gate_process(&g3, 1.0, 0, 48000.0); /* voice just stopped */
        for (int i = 0; i < 480; i++) {
            double v = noiseboy_output_gate_process(&g3, 1.0, 0, 48000.0);
            double jump = fabs(v - prev);
            if (jump > maxJump) maxJump = jump;
            prev = v;
        }
        printf("\nMax per-sample jump right after voice stops: %.6f (expect small -- smoothed release, not an instant click)\n", maxJump);
        if (maxJump > 0.01) { printf("FAILED: release should be smoothed, not an abrupt click\n"); all_ok = 0; }
        else printf("PASSED\n");
    }

    printf(all_ok ? "\nALL GATE CHECKS PASSED\n" : "\nSOME GATE CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
