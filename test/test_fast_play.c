#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

int main(void) {
    int all_ok = 1;
    NoiseboyEngine e;
    noiseboy_engine_init(&e, 48000.0, 42u);
    e.params.attackMs = 4.0;
    e.params.releaseMs = 400.0;

    int numNotes = 8; /* fewer notes, but track a unique voice-instance identity, not just midi note number */
    int noteOnSample[8], noteOffSample[8], notes[8];
    for (int i = 0; i < numNotes; i++) {
        noteOnSample[i] = i * 1200;  /* 25ms apart at 48kHz */
        noteOffSample[i] = noteOnSample[i] + 480; /* held 10ms */
        notes[i] = 24 + i * 7; /* unique, well-spread notes, no repeats */
    }
    int noteAudibleSample[8]; for (int i = 0; i < 8; i++) noteAudibleSample[i] = -1;
    int alreadyClaimed[8]; for (int i = 0; i < 8; i++) alreadyClaimed[i] = 0; /* avoid double-claiming */

    int onIdx = 0, offIdx = 0;
    for (int s = 0; s < 30000; s++) {
        if (onIdx < numNotes && s == noteOnSample[onIdx]) {
            noiseboy_note_on(&e, notes[onIdx], 0.8);
            onIdx++;
        }
        if (offIdx < numNotes && s == noteOffSample[offIdx]) {
            noiseboy_note_off(&e, notes[offIdx]);
            offIdx++;
        }
        double l, r;
        noiseboy_process_stereo(&e, &l, &r);
        if (isnan(l) || isnan(r) || isinf(l) || isinf(r)) { printf("FAILED: non-finite\n"); all_ok = 0; }

        for (int ni = 0; ni < numNotes; ni++) {
            if (!alreadyClaimed[ni] && s >= noteOnSample[ni]) {
                for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
                    if (e.voices[v].active && e.voices[v].midiNote == notes[ni] && !e.voices[v].pendingSteal && e.voices[v].envLevel > 0.05) {
                        noteAudibleSample[ni] = s;
                        alreadyClaimed[ni] = 1;
                    }
                }
            }
        }
    }

    int droppedCount = 0;
    double maxLatencyMs = 0;
    for (int ni = 0; ni < numNotes; ni++) {
        if (noteAudibleSample[ni] < 0) {
            droppedCount++;
            printf("note %d (on at sample %d) DROPPED\n", notes[ni], noteOnSample[ni]);
        } else {
            double latencyMs = (noteAudibleSample[ni] - noteOnSample[ni]) / 48.0;
            if (latencyMs > maxLatencyMs) maxLatencyMs = latencyMs;
            printf("note %d: audible after %.2fms\n", notes[ni], latencyMs);
        }
    }
    printf("\nExtreme fast playing (25ms apart, %d notes): dropped=%d/%d, max latency=%.2fms\n", numNotes, droppedCount, numNotes, maxLatencyMs);
    if (droppedCount > 0) { printf("FAILED\n"); all_ok = 0; }
    else printf("PASSED\n");

    return all_ok ? 0 : 1;
}
