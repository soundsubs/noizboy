/* noiseboy -- Schwung sound_generator module (Plugin API v2).
 *
 * Wires the NOISEBOY DSP engine (noiseboy_dsp.c/h) up to Schwung's
 * native plugin interface, per docs/MODULES.md's "Plugin API v2
 * (Recommended)" section:
 *   - create_instance / destroy_instance: engine lifecycle
 *   - on_midi: note-on/off drive noiseboy_note_on/off
 *   - set_param / get_param: knobs 1-8 via chain_params (see
 *     module.json), each writing directly into the shared
 *     NoiseboyParams struct
 *   - render_block: synthesizes audio from scratch (NOISEBOY generates
 *     its own sound; it does not process an incoming stream the way
 *     an audio_fx module would)
 *
 * Signal chain in render_block: NOISEBOY voice engine -> DBCELL
 * (always-on db-cell port, dbcell_dsp.c/h) -> TILT (analog-tape-style
 * EQ, see TiltFilter's own comment) -> noise gate. db-cell sits BEFORE
 * TILT, per explicit request: "move the db-cell technology to before
 * the TILT. This way, it must flow through tone shaping on its way
 * out." A dedicated gate was removed at the same time on the theory
 * that TILT's own always-present bandwidth limiting would keep an
 * idle instrument quiet enough on its own -- measured at the time as
 * roughly -55 to -58dB relative to a played note. That measurement
 * wasn't wrong, but it turned out to still be audibly present in
 * practice ("I can hear db-cell at the end making sound"), so the
 * gate is back -- positioned AFTER TILT this time (TILT used to sit
 * where the gate now sits again), so db-cell's output still flows
 * through TILT's tone-shaping exactly as originally requested, with
 * the gate as the final stage guaranteeing true silence rather than
 * TILT trying to do double duty as both an EQ and a gate.
 *
 * NOTE ON VERIFICATION: the v2 API's exact struct layout (host_api_v1_t
 * fields, plugin_api_v2_t member order) is taken directly from
 * docs/MODULES.md's own code example, not guessed -- but this file
 * itself has not been build-tested against the real Schwung headers in
 * this environment (no network access to fetch them here). Build
 * against the actual src/host/plugin_api_v1.h from a real Schwung
 * checkout before deploying; if any field names differ from what's
 * used below, the compiler will point at exactly where.
 */

#include "host/plugin_api_v1.h" /* v2 API is defined in this same file, per docs/MODULES.md */
#include "noiseboy_dsp.h"
#include "dbcell_dsp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* TiltFilter itself (struct + tilt_filter_init/tilt_filter_process)
 * lives in noiseboy_dsp.c/h now, not here -- it only depends on
 * primitives already there (MoogLadder, Korg35HP), and keeping it
 * there means it's testable standalone via this project's own C test
 * suite, matching where every other piece of real DSP logic in this
 * project lives. This file stays thin Schwung-specific glue. See
 * TiltFilter's own header comment in noiseboy_dsp.h for the full
 * design rationale. */

typedef struct {
    NoiseboyEngine engine;
    /* DBCELL always runs on NOISEBOY's output, per explicit request
     * ("add db-cell on the output, always... this will test whether I
     * like my own work"). See dbcell_dsp.h for the full port
     * rationale. Separate RNG seed from the main engine's, drawn from
     * the same /dev/urandom source below, so the two layers'
     * randomizations are independent rather than correlated. Now sits
     * BEFORE TILT in the signal chain, not after -- see TiltFilter's
     * own comment and render_block's own comment for the full
     * rationale. */
    DbCellEngine dbcell;
    /* TILT -- see TiltFilter's own comment. */
    TiltFilter tilt;
    /* Noise gate, RE-ADDED after TILT -- see NoiseboyOutputGate's own
     * header comment in noiseboy_dsp.h for why. */
    NoiseboyOutputGate outputGate;
} noiseboy_instance_t;

static unsigned int read_random_seed(unsigned int fallback) {
    unsigned int seed = 0;
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        if (fread(&seed, sizeof(seed), 1, urandom) != 1) seed = 0;
        fclose(urandom);
    }
    return seed != 0 ? seed : fallback;
}

static void* create_instance(const char *module_dir, const char *json_defaults) {
    (void)module_dir;
    (void)json_defaults; /* no defaults currently parsed from module.json's "defaults" block -- randomization seed comes from a runtime source below instead, so a fixed json default wouldn't be used anyway */

    noiseboy_instance_t *inst = (noiseboy_instance_t*)calloc(1, sizeof(noiseboy_instance_t));
    if (!inst) return NULL;

    /* Seed from a runtime source (not a fixed constant) so every
     * instantiation genuinely randomizes, per explicit spec ("every
     * time you instantiate, its a randomized noise block"). Falls back
     * to a fixed seed only if neither entropy source is available,
     * rather than leaving the seed uninitialized. */
    unsigned int seed = read_random_seed(0x5EED0001u);

    /* Sample rate: Schwung's audio pipeline runs at a fixed rate
     * per-device; 48000 matches EMAX_FX's (this project family's)
     * established default. If the real host exposes its actual sample
     * rate through host_api_v1_t, prefer that over this constant --
     * see the NOTE at the top of this file. */
    noiseboy_engine_init(&inst->engine, 48000.0, seed);
    dbcell_engine_init(&inst->dbcell, 48000.0, read_random_seed(0xDBCE11u));
    tilt_filter_init(&inst->tilt, 48000.0);
    noiseboy_output_gate_init(&inst->outputGate);

    return inst;
}

static void destroy_instance(void *instance) {
    /* Loop buffers are separately heap-allocated (see
     * postfilter_loop_alloc's own comment for why -- keeping
     * NoiseboyEngine itself small enough to stack-declare safely
     * elsewhere, like this project's own tests, while still
     * supporting a full 3-second loop). free(instance) alone would
     * free the outer calloc'd block but NOT these separately-
     * allocated buffers -- a real leak on every instance destruction
     * without this. */
    noiseboy_instance_t *inst = (noiseboy_instance_t*)instance;
    if (inst) {
        for (int v = 0; v < NOISEBOY_MAX_VOICES; v++) {
            postfilter_loop_free(&inst->engine.voices[v].loop);
        }
    }
    free(instance);
}

static void on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source; /* 0 = internal (Move pads/sequencer), 1 = external (USB) -- NOISEBOY treats both identically, no reason to distinguish */
    noiseboy_instance_t *inst = (noiseboy_instance_t*)instance;
    if (!inst || len < 1) return;

    uint8_t status = msg[0] & 0xF0;

    if (status == 0x90 && len >= 3) { /* Note On */
        int note = msg[1];
        int velocity = msg[2];
        if (velocity == 0) {
            noiseboy_note_off(&inst->engine, note); /* note-on with velocity 0 is a note-off, per MIDI spec */
        } else {
            noiseboy_note_on(&inst->engine, note, (double)velocity / 127.0);
        }
    } else if (status == 0x80 && len >= 3) { /* Note Off */
        int note = msg[1];
        noiseboy_note_off(&inst->engine, note);
    } else if (status == 0xB0 && len >= 3 && msg[1] == 123) { /* CC 123 = All Notes Off */
        noiseboy_all_notes_off(&inst->engine);
    }
}

/* Maps knob 0-127 (or a pre-normalized 0-1 string, depending on how
 * the host's chain_params system delivers values -- see module.json's
 * "type": "int" 0-127 declarations, matched here) to each param's
 * actual range. */
static double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

static void set_param(void *instance, const char *key, const char *val) {
    noiseboy_instance_t *inst = (noiseboy_instance_t*)instance;
    if (!inst || !key || !val) return;

    double raw01 = clamp01(atof(val) / 127.0);
    NoiseboyParams *p = &inst->engine.params;

    if (strcmp(key, "filter_cutoff") == 0) {
        p->filterCutoffOffset01 = raw01;
    } else if (strcmp(key, "resonance") == 0) {
        p->filterResonance01 = raw01;
    } else if (strcmp(key, "loop_length") == 0) {
        /* SPEED knob, per explicit request -- real-time multiplier on
         * playback rate through the fixed captured loop buffer (NOT
         * the buffer's own length, which is loopLengthSeconds,
         * randomized once at the recipe level -- see NoiseboyEngine's
         * own field). Symmetric exponential range, knob centre =
         * exactly 1.0x (normal/neutral speed) -- 0.125x at knob=0 (8x
         * slower), 8.0x at knob=1 (8x faster). Chosen to put "no
         * change" intuitively at centre, like a real tape-loop speed
         * control, rather than an asymmetric range where centre would
         * land on some arbitrary non-1.0 multiplier. */
        p->loopSpeedMul = pow(2.0, (raw01 - 0.5) * 6.0);
    } else if (strcmp(key, "loop_intensity") == 0) {
        /* LOOP Intensity, per explicit request (formerly AM Depth) --
         * blend between the dry, post-filter signal and the looped,
         * decaying playback. See PostFilterLoop's own comment for the
         * full design. */
        p->loopIntensity01 = raw01;
    } else if (strcmp(key, "attack") == 0) {
        p->attackMs = 0.5 + raw01 * 199.5; /* 0.5-200 ms */
    } else if (strcmp(key, "release") == 0) {
        /* Exponential (log-scale) mapping, per explicit request:
         * "the release should be a single sample at [knob] 0, and at
         * 1 should be pluckier but almost hard to hear... a decaying
         * exponential curve". The envelope's own decay was ALREADY
         * exponential (verified directly, no linear branch anywhere
         * in that code) -- the actual issue was this KNOB-TO-MS
         * mapping being linear across a huge range (5-2000ms) with
         * only 128 discrete MIDI steps to cover it. Linear meant most
         * of those 128 steps were spent on the long, pad-like end of
         * the range, leaving very few, coarse steps for the short,
         * "plucky" end where fine control actually matters most.
         * min lowered to 0.02ms (~1 sample at 48kHz, matching "a
         * single sample" literally -- see the envelope's own clamp
         * floor, lowered to match) from the previous 5ms/0.5ms floor.
         * max raised to 4000ms (up from 2000ms) to give genuine room
         * for "almost hard to hear" at the top of the knob -- a long
         * enough exponential tail that most of its energy decays
         * quickly even though the time constant itself is long. */
        p->releaseMs = 0.02 * pow(4000.0 / 0.02, raw01);
    } else if (strcmp(key, "detune_spread") == 0) {
        p->detuneSpread01 = raw01;
    } else if (strcmp(key, "tilt") == 0) {
        /* TILT, per explicit request (renamed from Output Filt/
         * output_filter_freq, "since it loses the filter qualities").
         * See TiltFilter's own comment for the full design -- this
         * knob now controls a genuine tilt EQ within a fixed tape-
         * bandwidth window, not a lowpass/highpass sweep-to-silence. */
        p->tiltAmount01 = raw01;
    } else if (strcmp(key, "master_level") == 0) {
        p->masterLevel01 = raw01;
    } else if (strcmp(key, "drive") == 0) {
        p->drive01 = raw01;
    } else if (strcmp(key, "randomize") == 0) {
        /* Rising-edge trigger, per explicit request for a way to get a
         * new randomized set without reinstantiating the module --
         * re-rolls the recipe once when this value crosses from 0 to
         * nonzero, rather than continuously re-randomizing while a
         * bound knob/menu control is mid-movement. Only affects notes
         * played AFTER this call -- see noiseboy_randomize_recipe's
         * own comment for why in-flight voices aren't touched. Also
         * re-rolls DBCELL's recipe (a fresh independent seed, not
         * correlated with NOISEBOY's) so one Randomize gesture
         * refreshes both layers of the sound together. */
        int raw = atoi(val);
        if (raw != 0 && inst->engine.lastRandomizeTriggerRaw == 0) {
            noiseboy_randomize_recipe(&inst->engine);
            dbcell_randomize(&inst->dbcell, read_random_seed(0xDBCE22u));
        }
        inst->engine.lastRandomizeTriggerRaw = raw;
    }
}

/* Navigable parameter hierarchy for the Shadow UI -- separate
 * mechanism from chain_params (which docs/MODULES.md describes as
 * mapping specifically to the 8 physical knobs, "knobs 1-8 in the
 * Shadow UI for quick access" -- there's no knob 9 or 10 for it to
 * map "drive"/"randomize" onto). This is what actually makes those
 * two reachable at all: a "root" level listing all 10 params, with
 * only the first 8 bound to knobs via the "knobs" array, matching the
 * "levels" dictionary format documented in docs/MODULES.md's "Shadow
 * UI Parameter Hierarchy" section. Same verification caveat as the
 * rest of this file applies -- built from that documentation's own
 * example, not confirmed against a real working module. */
static const char *NOISEBOY_UI_HIERARCHY_JSON =
    "{\"levels\":{\"root\":{\"name\":\"NOIZBOY\",\"params\":["
    "{\"key\":\"filter_cutoff\",\"name\":\"Filter Offset\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"resonance\",\"name\":\"Resonance\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"loop_intensity\",\"name\":\"LOOP Intensity\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"loop_length\",\"name\":\"Loop Length\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"attack\",\"name\":\"Attack\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"release\",\"name\":\"Release\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"detune_spread\",\"name\":\"Detune\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"tilt\",\"name\":\"TILT\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"drive\",\"name\":\"Drive\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"randomize\",\"name\":\"Randomize\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"master_level\",\"name\":\"Level\",\"type\":\"int\",\"min\":0,\"max\":127}"
    "],\"knobs\":[\"filter_cutoff\",\"resonance\",\"loop_intensity\",\"loop_length\",\"attack\",\"release\",\"detune_spread\",\"tilt\"]}}}";

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    noiseboy_instance_t *inst = (noiseboy_instance_t*)instance;
    if (!inst || !key || !buf) return -1;
    NoiseboyParams *p = &inst->engine.params;

    if (strcmp(key, "ui_hierarchy") == 0) {
        int written = snprintf(buf, (size_t)buf_len, "%s", NOISEBOY_UI_HIERARCHY_JSON);
        return written;
    }

    double val01 = -1.0;
    if (strcmp(key, "filter_cutoff") == 0) val01 = p->filterCutoffOffset01;
    else if (strcmp(key, "resonance") == 0) val01 = p->filterResonance01;
    else if (strcmp(key, "loop_length") == 0) val01 = log2(p->loopSpeedMul) / 6.0 + 0.5;
    else if (strcmp(key, "loop_intensity") == 0) val01 = p->loopIntensity01;
    else if (strcmp(key, "attack") == 0) val01 = (p->attackMs - 0.5) / 199.5;
    else if (strcmp(key, "release") == 0) val01 = log(p->releaseMs / 0.02) / log(4000.0 / 0.02);
    else if (strcmp(key, "detune_spread") == 0) val01 = p->detuneSpread01;
    else if (strcmp(key, "tilt") == 0) val01 = p->tiltAmount01;
    else if (strcmp(key, "master_level") == 0) val01 = p->masterLevel01;
    else if (strcmp(key, "drive") == 0) val01 = p->drive01;
    else if (strcmp(key, "randomize") == 0) {
        /* Always reads back as 0 -- it's a momentary trigger, not a
         * value with a meaningful resting state to report. */
        int written = snprintf(buf, (size_t)buf_len, "0");
        return written;
    }

    if (val01 < 0.0) return -1; /* unknown key */

    int written = snprintf(buf, (size_t)buf_len, "%d", (int)(clamp01(val01) * 127.0 + 0.5));
    return written;
}

static void render_block(void *instance, int16_t *out_lr, int frames) {
    noiseboy_instance_t *inst = (noiseboy_instance_t*)instance;
    if (!inst || !out_lr) return;

    for (int i = 0; i < frames; i++) {
        /* NOISEBOY now produces genuine stereo (Detune-driven layer
         * panning, per explicit request), not a mono signal duplicated
         * to both channels -- see noiseboy_process_stereo's own header
         * comment. This IS the "dry" signal db-cell then processes;
         * db-cell's own independent per-channel chain state further
         * diverges the two beyond what NOISEBOY's own panning already
         * provides. */
        double l, r;
        noiseboy_process_stereo(&inst->engine, &l, &r);
        dbcell_process(&inst->dbcell, &l, &r);

        /* TILT applied AFTER dbcell, per explicit request -- see this
         * file's own header comment and TiltFilter's own comment for
         * the full rationale. */
        tilt_filter_process(&inst->tilt, &l, &r, inst->engine.params.tiltAmount01, inst->engine.sampleRate);

        /* Noise gate, RE-ADDED, positioned AFTER TILT -- see this
         * file's own header comment and NoiseboyOutputGate's own
         * comment (noiseboy_dsp.h) for the full rationale. Keyed off
         * actual voice activity, not signal level. */
        const int voicesActive = noiseboy_any_voice_active(&inst->engine);
        l = noiseboy_output_gate_process(&inst->outputGate, l, voicesActive, inst->engine.sampleRate);
        r = noiseboy_output_gate_process(&inst->outputGate, r, voicesActive, inst->engine.sampleRate);

        if (l > 1.0) l = 1.0;
        if (l < -1.0) l = -1.0;
        if (r > 1.0) r = 1.0;
        if (r < -1.0) r = -1.0;

        out_lr[i * 2 + 0] = (int16_t)(l * 32767.0);
        out_lr[i * 2 + 1] = (int16_t)(r * 32767.0);
    }
}

static plugin_api_v2_t api = {
    .api_version = 2,
    .create_instance = create_instance,
    .destroy_instance = destroy_instance,
    .on_midi = on_midi,
    .set_param = set_param,
    .get_param = get_param,
    .render_block = render_block,
};

plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t* host) {
    (void)host; /* not currently used -- NOISEBOY doesn't need any host callbacks (no file I/O, no modulation-matrix integration) beyond what create_instance/on_midi/render_block already cover */
    return &api;
}
