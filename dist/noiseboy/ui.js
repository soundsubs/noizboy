/* NOISEBOY's Shadow UI menu -- deliberately minimal: a single
 * "Randomize" action, per explicit request for a real single-button-
 * press way to re-roll the recipe from the menu, rather than the
 * "randomize" chain_param alone (which needs a knob/value nudged from
 * 0 to nonzero -- functional, but not a clean single press).
 *
 * VERIFICATION CAVEAT: built from docs/MODULES.md's own JavaScript UI
 * and Menu System examples (found via transcript search earlier in
 * this session), not confirmed against a real working module's ui.js
 * -- this is genuinely new ground for this project (everything before
 * this was pure native C/dsp.so, no JS UI layer at all). If any import
 * path or menu-helper function name here doesn't match the real
 * shared/ modules on your Schwung installation, the module should
 * still load and play (this only affects the on-screen menu), and
 * whatever error the console shows is the fastest way to find the
 * mismatch.
 */

import { createAction } from '../../shared/menu_items.mjs';
import { createMenuState, handleMenuInput } from '../../shared/menu_nav.mjs';
import { createMenuStack } from '../../shared/menu_stack.mjs';
import { drawMenuHeader, drawMenuList, drawMenuFooter, menuLayoutDefaults } from '../../shared/menu_layout.mjs';

const menuState = createMenuState();
const menuStack = createMenuStack();

function getRootMenu() {
    return [
        createAction('Randomize', () => {
            /* Set the trigger param nonzero, then immediately back to
             * 0 -- the C side (noiseboy_plugin.c's set_param) detects
             * a 0->nonzero rising edge and re-rolls both NOISEBOY's
             * own recipe and DBCELL's on that edge. Resetting back to
             * 0 right away means the NEXT press is also a fresh rising
             * edge, rather than requiring the value to be nudged back
             * down first -- a real button doesn't "stay pressed". */
            host_module_set_param('randomize', '127');
            host_module_set_param('randomize', '0');
        })
    ];
}

function redraw() {
    const current = menuStack.current();
    clear_screen();
    drawMenuHeader(current.title);
    drawMenuList({
        items: current.items,
        selectedIndex: 0,
        listArea: {
            topY: menuLayoutDefaults.listTopY,
            bottomY: menuLayoutDefaults.listBottomWithFooter
        }
    });
    drawMenuFooter('Click: Randomize');
}

globalThis.init = function () {
    menuStack.push({ title: 'NOISEBOY', items: getRootMenu() });
    redraw();
};

globalThis.tick = function () {
    // No animation/live display needed -- this menu is static aside
    // from the one action.
};

globalThis.onMidiMessageInternal = function (data) {
    if ((data[0] & 0xF0) === 0xB0) {
        const cc = data[1];
        const value = data[2];
        const current = menuStack.current();
        const result = handleMenuInput({
            cc,
            value,
            items: current.items,
            state: menuState,
            stack: menuStack,
            shiftHeld: false,
            onBack: () => host_return_to_menu()
        });
        if (result.needsRedraw) {
            redraw();
        }
    }
};
