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
 * (always-on db-cell port, dbcell_dsp.c/h) -> noise gate. The gate's
 * placement AFTER db-cell specifically (not before it) is per direct
 * correction: db-cell's forced-always-present Noiz slot generates
 * sound regardless of NOISEBOY's own input, so gating only NOISEBOY's
 * raw output wouldn't catch db-cell's own residual noise -- the gate
 * has to be the LAST stage, keyed off actual NOISEBOY voice activity,
 * to guarantee true silence when nothing is being played.
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

/* Noise gate applied AFTER db-cell, per explicit correction: db-cell's
 * forced-always-present Noiz slot generates sound regardless of
 * NOISEBOY's own input, so without this an "instrument" would still
 * be audibly hissing even with zero voices playing. Keyed off actual
 * NOISEBOY voice activity (noiseboy_any_voice_active), not off signal
 * level -- a level-based gate could still let db-cell's own noise
 * through during a genuinely quiet-but-still-playing moment, or fail
 * to fully silence a loud db-cell moment right as the last voice
 * releases. Fast attack (opens quickly when a note starts) and a
 * slower, smoothed release (avoids an abrupt click when the last
 * voice stops) -- one shared envelope drives both channels, since
 * "any voice active" is a single global condition, not a per-channel
 * one. */
typedef struct {
    double envelope;
} NoiseGate;

static void noisegate_init(NoiseGate *g) {
    g->envelope = 0.0;
}

static double noisegate_process(NoiseGate *g, double x, int voicesActive, double sampleRate) {
    const double target = voicesActive ? 1.0 : 0.0;
    const double timeMs = voicesActive ? 3.0 : 150.0;
    const double coeff = exp(-1.0 / (0.001 * timeMs * sampleRate));
    g->envelope = target + (g->envelope - target) * coeff;
    return x * g->envelope;
}

typedef struct {
    NoiseboyEngine engine;
    /* DBCELL always runs on NOISEBOY's output, per explicit request
     * ("add db-cell on the output, always... this will test whether I
     * like my own work"). See dbcell_dsp.h for the full port
     * rationale. Separate RNG seed from the main engine's, drawn from
     * the same /dev/urandom source below, so the two layers'
     * randomizations are independent rather than correlated. */
    DbCellEngine dbcell;
    /* Placed AFTER dbcell in the signal chain -- see NoiseGate's own
     * comment above for why. */
    NoiseGate noiseGate;
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
    noisegate_init(&inst->noiseGate);

    return inst;
}

static void destroy_instance(void *instance) {
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
    } else if (strcmp(key, "am_rate") == 0) {
        p->amRateHz = 0.1 + raw01 * 19.9; /* 0.1-20 Hz */
    } else if (strcmp(key, "am_depth") == 0) {
        p->amDepth01 = raw01;
    } else if (strcmp(key, "attack") == 0) {
        p->attackMs = 0.5 + raw01 * 199.5; /* 0.5-200 ms */
    } else if (strcmp(key, "release") == 0) {
        p->releaseMs = 5.0 + raw01 * 1995.0; /* 5-2000 ms */
    } else if (strcmp(key, "detune_spread") == 0) {
        p->detuneSpread01 = raw01;
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
    "{\"levels\":{\"root\":{\"name\":\"NOISEBOY\",\"params\":["
    "{\"key\":\"filter_cutoff\",\"name\":\"Filter Offset\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"resonance\",\"name\":\"Resonance\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"am_depth\",\"name\":\"AM Depth\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"am_rate\",\"name\":\"AM Rate\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"attack\",\"name\":\"Attack\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"release\",\"name\":\"Release\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"detune_spread\",\"name\":\"Detune\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"master_level\",\"name\":\"Level\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"drive\",\"name\":\"Drive\",\"type\":\"int\",\"min\":0,\"max\":127},"
    "{\"key\":\"randomize\",\"name\":\"Randomize\",\"type\":\"int\",\"min\":0,\"max\":127}"
    "],\"knobs\":[\"filter_cutoff\",\"resonance\",\"am_depth\",\"am_rate\",\"attack\",\"release\",\"detune_spread\",\"master_level\"]}}}";

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
    else if (strcmp(key, "am_rate") == 0) val01 = (p->amRateHz - 0.1) / 19.9;
    else if (strcmp(key, "am_depth") == 0) val01 = p->amDepth01;
    else if (strcmp(key, "attack") == 0) val01 = (p->attackMs - 0.5) / 199.5;
    else if (strcmp(key, "release") == 0) val01 = (p->releaseMs - 5.0) / 1995.0;
    else if (strcmp(key, "detune_spread") == 0) val01 = p->detuneSpread01;
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
        double y = noiseboy_process(&inst->engine);

        /* DBCELL always runs on the output, per explicit request --
         * NOISEBOY's mono synthesis duplicated to both channels as
         * db-cell's "dry" input; db-cell's own independent per-channel
         * chain state naturally diverges the two over time even though
         * they start identical each sample, so the combined result has
         * some real stereo width NOISEBOY's own mono voice engine
         * doesn't produce on its own. */
        double l = y, r = y;
        dbcell_process(&inst->dbcell, &l, &r);

        /* Noise gate AFTER dbcell, per explicit correction -- db-cell's
         * forced-always-present Noiz slot would otherwise keep making
         * sound even with zero NOISEBOY voices playing. Keyed off
         * actual voice activity, not signal level -- see NoiseGate's
         * own comment for why. */
        const int voicesActive = noiseboy_any_voice_active(&inst->engine);
        l = noisegate_process(&inst->noiseGate, l, voicesActive, inst->engine.sampleRate);
        r = noisegate_process(&inst->noiseGate, r, voicesActive, inst->engine.sampleRate);

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
