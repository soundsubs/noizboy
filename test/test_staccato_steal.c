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

    /* Process forward until the deferred steal actually completes.
     * Per a real bug found and fixed here: the ORIGINAL fix for the
     * staccato case set gateOpen=0 immediately when the voice started,
     * in the SAME sample voice_start ran -- before the envelope had
     * computed even once with gateOpen=1. Since gateOpen IS the
     * envelope's attack target, envLevel never rose above 0 at all;
     * the note was completely, silently dropped, not just released
     * early -- exactly what a fast, short pad tap under voice-stealing
     * load would trigger every time. The fix gives the voice a brief,
     * fixed minimum hold (~5ms) so its attack genuinely gets a chance
     * to rise and be audible first. This test now verifies the
     * property that actually matters -- the note becomes genuinely
     * audible (envLevel rises meaningfully above 0) -- rather than
     * asserting on gateOpen's exact value at the instant voice_start
     * ran, which was the source of the original bug. */
    int becameActive72 = 0;
    double peakEnvLevel = 0.0;
    int eventuallyReleased = 0;
    for (int i = 0; i < 12000; i++) {
        double l, r;
        noiseboy_process_stereo(&e, &l, &r);
        if (isnan(l) || isnan(r)) { printf("FAILED: non-finite\n"); return 1; }
        for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
            if (e.voices[v].active && e.voices[v].midiNote == 72) {
                becameActive72 = 1;
                if (e.voices[v].envLevel > peakEnvLevel) peakEnvLevel = e.voices[v].envLevel;
                if (!e.voices[v].gateOpen) eventuallyReleased = 1;
            }
        }
    }
    printf("Note 72: became active=%d, peak envLevel=%.4f, eventually released=%d\n",
           becameActive72, peakEnvLevel, eventuallyReleased);

    int all_ok = 1;
    if (!foundPending) { printf("FAILED: pending steal not found\n"); all_ok = 0; }
    if (!pendingReleasedSet) { printf("FAILED: pendingNoteReleased was not set after early note-off\n"); all_ok = 0; }
    if (!becameActive72) { printf("FAILED: note 72 never started within test window\n"); all_ok = 0; }
    if (peakEnvLevel < 0.1) { printf("FAILED: note should have been genuinely audible (envLevel should rise meaningfully), not silently skipped\n"); all_ok = 0; }
    if (!eventuallyReleased) { printf("FAILED: note should still transition to release eventually, not sustain forever\n"); all_ok = 0; }

    printf(all_ok ? "\nALL STACCATO-STEAL CHECKS PASSED\n" : "\nSOME STACCATO-STEAL CHECKS FAILED\n");
    return all_ok ? 0 : 1;
}
