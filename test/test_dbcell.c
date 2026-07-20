#include "dbcell_dsp.h"
#include <stdio.h>
#include <math.h>

static int check_finite(double x) { return !(isnan(x) || isinf(x)); }

int main(void) {
    int all_ok = 1;

    for (unsigned int seed = 1; seed <= 8; seed++) {
        DbCellEngine e;
        dbcell_engine_init(&e, 48000.0, seed * 12347u);

        int finite_ok = 1;
        double peak = 0.0;
        for (int i = 0; i < 48000; i++) {
            /* Feed it a modest sine tone, like NOISEBOY's mono output duplicated to L/R */
            double x = 0.3 * sin(2.0 * M_PI * 220.0 * i / 48000.0);
            double l = x, r = x;
            dbcell_process(&e, &l, &r);
            if (!check_finite(l) || !check_finite(r)) finite_ok = 0;
            if (fabs(l) > peak) peak = fabs(l);
            if (fabs(r) > peak) peak = fabs(r);
        }
        printf("seed %u: finite_ok=%d peak=%.4f noizSlot=%d\n", seed, finite_ok, peak, e.noizSlot);
        if (!finite_ok) all_ok = 0;
        if (peak > 2.0) { printf("  WARN: unusually high peak\n"); }
    }

    /* Re-randomize mid-stream, verify still finite */
    {
        DbCellEngine e;
        dbcell_engine_init(&e, 48000.0, 999u);
        for (int i = 0; i < 10000; i++) {
            double l = 0.2, r = 0.2;
            dbcell_process(&e, &l, &r);
        }
        dbcell_randomize(&e, 5555u);
        int finite_ok = 1;
        for (int i = 0; i < 48000; i++) {
            double l = 0.2, r = 0.2;
            dbcell_process(&e, &l, &r);
            if (!check_finite(l) || !check_finite(r)) finite_ok = 0;
        }
        printf("Mid-stream re-randomize test: finite_ok=%d\n", finite_ok);
        if (!finite_ok) all_ok = 0;
    }

    printf(all_ok ? "\nALL DBCELL CHECKS PASSED\n" : "\nSOME DBCELL CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
