#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

static int check_finite(double x) { return !(isnan(x) || isinf(x)); }

int main(void) {
    int all_ok = 1;
    NoiseboyEngine e;
    noiseboy_engine_init(&e, 48000.0, 0xABCDu);

    /* Max out everything that could interact badly */
    e.params.loopDepth01 = 1.0; /* max Loop Depth */
    e.params.drive01 = 1.0;

    for (int i = 0; i < 4; i++) {
        noiseboy_note_on(&e, 40 + i * 12, 1.0);
    }

    double peak = 0.0;
    int finite_ok = 1;
    for (int i = 0; i < 48000 * 2; i++) {
        double y = noiseboy_process(&e);
        if (!check_finite(y)) finite_ok = 0;
        if (fabs(y) > peak) peak = fabs(y);
    }

    printf("Max LOOP/drive/tapesat stress test: finite_ok=%d peak=%.4f\n", finite_ok, peak);
    if (!finite_ok) all_ok = 0;
    if (peak > 1.5) { printf("  WARN: unusually high peak\n"); }

    printf(all_ok ? "\nALL NEW-STAGE CHECKS PASSED\n" : "\nSOME NEW-STAGE CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
