#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

int main(void) {
    int silentCount = 0;
    int totalTests = 0;
    int notes[] = { 21, 36, 48, 60, 72, 84, 96, 108 };

    for (unsigned int seed = 1; seed <= 3000; seed++) {
        for (int ni = 0; ni < 8; ni++) {
            NoiseboyEngine e;
            noiseboy_engine_init(&e, 48000.0, seed);
            noiseboy_note_on(&e, notes[ni], 0.7);
            double peak = 0.0;
            int finite_ok = 1;
            for (int i = 0; i < 4000; i++) {
                double y = noiseboy_process(&e);
                if (isnan(y) || isinf(y)) finite_ok = 0;
                if (fabs(y) > peak) peak = fabs(y);
            }
            totalTests++;
            if (!finite_ok) {
                printf("NON-FINITE: seed=%u note=%d\n", seed, notes[ni]);
            }
            if (peak < 1e-6) {
                printf("SILENT: seed=%u note=%d numLayers=%d\n", seed, notes[ni], e.numRecipeLayers);
                silentCount++;
            }
        }
    }
    printf("\nTotal tests: %d, silent: %d\n", totalTests, silentCount);
    return silentCount > 0 ? 1 : 0;
}
