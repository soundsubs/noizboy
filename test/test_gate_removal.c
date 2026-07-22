#include "noiseboy_dsp.h"
#include "dbcell_dsp.h"
#include <stdio.h>
#include <math.h>

/* Verifies the "shouldn't have to be noise gated" claim directly --
 * simulates render_block's exact new signal chain (noiseboy ->
 * dbcell -> TILT, no gate) with ZERO voices playing, and measures how
 * loud db-cell's own always-present Noiz slot ends up after TILT's
 * bandwidth limiting, compared to how loud it was before this
 * redesign (pre-TILT, i.e. dbcell's raw output). */

int main(void) {
    NoiseboyEngine e;
    noiseboy_engine_init(&e, 48000.0, 42u);
    /* No notes played -- e has zero active voices throughout */

    DbCellEngine dbcell;
    dbcell_engine_init(&dbcell, 48000.0, 999u);

    TiltFilter tilt;
    tilt_filter_init(&tilt, 48000.0);

    double sumSqRaw = 0.0, sumSqTilted = 0.0;
    int n = 48000; /* 1 second */
    for (int i = 0; i < n; i++) {
        double l, r;
        noiseboy_process_stereo(&e, &l, &r); /* silent -- no voices */
        dbcell_process(&dbcell, &l, &r);      /* db-cell's own always-on noise */
        double rawL = l;
        sumSqRaw += rawL * rawL;
        tilt_filter_process(&tilt, &l, &r, 0.5, 48000.0); /* neutral tilt */
        sumSqTilted += l * l;
    }
    double rmsRaw = sqrt(sumSqRaw / n);
    double rmsTilted = sqrt(sumSqTilted / n);
    printf("Idle instrument (zero voices): db-cell raw RMS=%.5f, after TILT (neutral) RMS=%.5f (%.1fdB reduction)\n",
           rmsRaw, rmsTilted, 20.0 * log10((rmsTilted + 1e-12) / (rmsRaw + 1e-12)));
    printf("For reference, a typical played note's RMS is roughly 0.3-0.6 in this engine (see test_dbcell.c's own peak measurements)\n");

    if (rmsTilted > rmsRaw * 0.5) {
        printf("NOTE: TILT alone provides only modest reduction at neutral position -- worth listening to confirm this reads as acceptably quiet, not silent, when idle.\n");
    } else {
        printf("TILT provides a meaningful reduction in db-cell's idle hiss.\n");
    }
    return 0;
}
