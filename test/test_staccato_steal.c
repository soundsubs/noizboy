#include "noiseboy_dsp.h"
#include <stdio.h>
#include <math.h>

int main(void) {
    NoiseboyEngine e;
    noiseboy_engine_init(&e, 48000.0, 42u);
    e.params.releaseMs = 2000.0;
    e.params.attackMs = 4.0;

    for (int i = 0; i < 8; i++) noiseboy_note_on(&e, 40 + i * 3, 0.9);
    for (int i = 0; i < 4800; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }
    for (int i = 0; i < 8; i++) noiseboy_note_off(&e, 40 + i * 3);
    for (int i = 0; i < 480; i++) { double l, r; noiseboy_process_stereo(&e, &l, &r); }

    /* Trigger a steal, then IMMEDIATELY release the new note (a quick
       staccato hit) before the deferred steal has any chance to complete. */
    noiseboy_note_on(&e, 72, 0.9);
    noiseboy_note_off(&e, 72);

    /* Verify the pending-released flag was actually set on the voice
       that has this pending note. */
    int foundPending = 0, pendingReleasedSet = 0;
    for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
        if (e.voices[v].pendingSteal && e.voices[v].pendingMidiNote == 72) {
            foundPending = 1;
            pendingReleasedSet = e.voices[v].pendingNoteReleased;
        }
    }
    printf("Pending steal for note 72 found: %d, pendingNoteReleased flag set: %d\n", foundPending, pendingReleasedSet);

    /* Process forward until the deferred steal actually completes, then
       confirm the voice enters release (gateOpen=0) right away instead
       of sustaining, and eventually goes fully silent within a
       reasonable time -- not stuck sustaining forever. */
    int becameActive72 = 0, wasGateOpenWhenStarted = -1;
    for (int i = 0; i < 8000; i++) {
        double l, r;
        noiseboy_process_stereo(&e, &l, &r);
        if (isnan(l) || isnan(r)) { printf("FAILED: non-finite\n"); return 1; }
        for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
            if (e.voices[v].active && e.voices[v].midiNote == 72 && !becameActive72) {
                becameActive72 = 1;
                wasGateOpenWhenStarted = e.voices[v].gateOpen;
                printf("Note 72 started at sample %d, gateOpen=%d (should be 0 -- already released)\n", i, wasGateOpenWhenStarted);
            }
        }
    }

    int all_ok = 1;
    if (!foundPending) { printf("FAILED: pending steal not found\n"); all_ok = 0; }
    if (!pendingReleasedSet) { printf("FAILED: pendingNoteReleased was not set after early note-off\n"); all_ok = 0; }
    if (!becameActive72) { printf("FAILED: note 72 never started within test window\n"); all_ok = 0; }
    if (wasGateOpenWhenStarted != 0) { printf("FAILED: note 72 should have started already in release (gateOpen=0), not sustained\n"); all_ok = 0; }

    printf(all_ok ? "\nALL STACCATO-STEAL CHECKS PASSED\n" : "\nSOME STACCATO-STEAL CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
