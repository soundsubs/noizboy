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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    NoiseboyEngine engine;
} noiseboy_instance_t;

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
    unsigned int seed = 0;
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        if (fread(&seed, sizeof(seed), 1, urandom) != 1) seed = 0;
        fclose(urandom);
    }
    if (seed == 0) {
        /* Fallback: a real Schwung host provides a monotonic-ish value
         * via other means; if this ever needs to be swapped for a
         * host-provided clock/counter, this is the one place to do it. */
        seed = 0x5EED0001u;
    }

    /* Sample rate: Schwung's audio pipeline runs at a fixed rate
     * per-device; 48000 matches EMAX_FX's (this project family's)
     * established default. If the real host exposes its actual sample
     * rate through host_api_v1_t, prefer that over this constant --
     * see the NOTE at the top of this file. */
    noiseboy_engine_init(&inst->engine, 48000.0, seed);

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
    }
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    noiseboy_instance_t *inst = (noiseboy_instance_t*)instance;
    if (!inst || !key || !buf) return -1;
    NoiseboyParams *p = &inst->engine.params;

    double val01 = -1.0;
    if (strcmp(key, "filter_cutoff") == 0) val01 = p->filterCutoffOffset01;
    else if (strcmp(key, "resonance") == 0) val01 = p->filterResonance01;
    else if (strcmp(key, "am_rate") == 0) val01 = (p->amRateHz - 0.1) / 19.9;
    else if (strcmp(key, "am_depth") == 0) val01 = p->amDepth01;
    else if (strcmp(key, "attack") == 0) val01 = (p->attackMs - 0.5) / 199.5;
    else if (strcmp(key, "release") == 0) val01 = (p->releaseMs - 5.0) / 1995.0;
    else if (strcmp(key, "detune_spread") == 0) val01 = p->detuneSpread01;
    else if (strcmp(key, "master_level") == 0) val01 = p->masterLevel01;

    if (val01 < 0.0) return -1; /* unknown key */

    int written = snprintf(buf, (size_t)buf_len, "%d", (int)(clamp01(val01) * 127.0 + 0.5));
    return written;
}

static void render_block(void *instance, int16_t *out_lr, int frames) {
    noiseboy_instance_t *inst = (noiseboy_instance_t*)instance;
    if (!inst || !out_lr) return;

    for (int i = 0; i < frames; i++) {
        double y = noiseboy_process(&inst->engine);
        if (y > 1.0) y = 1.0;
        if (y < -1.0) y = -1.0;
        int16_t sample = (int16_t)(y * 32767.0);
        out_lr[i * 2 + 0] = sample; /* L */
        out_lr[i * 2 + 1] = sample; /* R -- mono source, duplicated (see noiseboy_process's own header comment) */
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
