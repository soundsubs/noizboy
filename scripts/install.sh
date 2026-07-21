#!/bin/sh
# Deploys noiseboy directly to a Move over SSH, bypassing the Module
# Store UI. Install path/user pattern verified against real published
# Schwung modules referenced in docs/MODULES.md and this project
# family's own prior emax12 module (which corrected an earlier
# move-anything-path draft against schwung-jv880/schwung-rex):
# ableton@move.local, /data/UserData/schwung/modules/<component_type>/.

set -e

if [ ! -f build/dsp.so ]; then
    echo "ERROR: build/dsp.so not found -- run ./scripts/build.sh first"
    exit 1
fi

REMOTE_DIR="/data/UserData/schwung/modules/sound_generators/noiseboy"

ssh ableton@move.local "mkdir -p $REMOTE_DIR"
scp module.json ableton@move.local:"$REMOTE_DIR/module.json"
scp build/dsp.so ableton@move.local:"$REMOTE_DIR/dsp.so"
scp src/ui.js ableton@move.local:"$REMOTE_DIR/ui.js"

echo "Installed to $REMOTE_DIR on move.local."
echo "In Schwung Manager (move.local:7700) or on-device, rescan modules"
echo "(or restart Schwung) if it doesn't appear immediately."
