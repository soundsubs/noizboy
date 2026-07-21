/*
 * NOISEBOY Shadow UI -- single-press Randomize.
 *
 * REWRITTEN from a v0.4.0 draft that never actually loaded. That
 * draft guessed relative import paths ('../../shared/menu_items.mjs')
 * and a menu-helper-based structure; real modules use ABSOLUTE
 * filesystem import paths (/data/UserData/schwung/shared/...) and,
 * for a "raw_ui": true module like this one, draw directly with the
 * low-level primitives rather than the shared menu system. Every
 * pattern below is copied from three real, working files pulled
 * directly from a local Schwung checkout (not guessed, not from
 * documentation): src/modules/tools/ui-test/ui.js,
 * src/modules/text-test/ui.js, and
 * src/modules/audio_fx/freeverb/ui_chain.js. In particular,
 * host_module_set_param(key, stringValue) is confirmed directly from
 * freeverb's real, working chain UI -- this is the one function this
 * whole file exists to call correctly.
 */

const SCREEN_WIDTH = 128;
const SCREEN_HEIGHT = 64;

/* CC numbers confirmed from real modules' own source:
 * - CC 3 (jog wheel click) from text-test/ui.js's CC_JOG_CLICK
 * - CC 51 (Back) from text-test/ui.js's CC_BACK
 * Both used here as-is, not guessed. */
const CC_JOG_CLICK = 3;
const CC_BACK = 51;

let lastAction = "";
let lastActionTimeout = 0;
const ACTION_DISPLAY_FRAMES = 30;

function doRandomize() {
    /* Rising-edge trigger on the C side (noiseboy_plugin.c's
     * set_param) -- set nonzero then immediately back to 0, so the
     * NEXT press is also a fresh rising edge rather than needing the
     * value nudged back down first. */
    host_module_set_param('randomize', '127');
    host_module_set_param('randomize', '0');
    lastAction = "Randomized!";
    lastActionTimeout = ACTION_DISPLAY_FRAMES;
}

function drawUI() {
    clear_screen();
    print(2, 2, "NOISEBOY", 1);
    fill_rect(0, 12, SCREEN_WIDTH, 1, 1);

    print(2, 24, "Jog click:", 1);
    print(2, 36, "Randomize", 1);

    if (lastActionTimeout > 0) {
        print(2, 50, lastAction, 1);
    }

    fill_rect(0, SCREEN_HEIGHT - 10, SCREEN_WIDTH, 1, 1);
    print(2, SCREEN_HEIGHT - 8, "Back: exit", 1);
}

globalThis.init = function () {
    lastAction = "";
    lastActionTimeout = 0;
    drawUI();
};

globalThis.tick = function () {
    if (lastActionTimeout > 0) {
        lastActionTimeout--;
        drawUI();
    }
};

globalThis.onMidiMessageInternal = function (data) {
    if (!data || data.length < 3) return;

    const status = data[0] & 0xF0;
    const cc = data[1];
    const val = data[2];

    if (status !== 0xB0) return;

    if (cc === CC_JOG_CLICK && val > 0) {
        doRandomize();
        drawUI();
        return;
    }

    if (cc === CC_BACK && val > 0) {
        host_return_to_menu();
        return;
    }
};
