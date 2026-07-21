#!/bin/sh
# Cross-compiles noiseboy's dsp.so for the Move's ARM64 Cortex-A72,
# using Docker so no local ARM64 toolchain is required.
#
# Requires a real Schwung checkout (for src/host/plugin_api_v1.h,
# which noiseboy_plugin.c includes) as a sibling directory, same
# convention as this project family's other tools expecting JUCE
# alongside them:
#   your-workspace/
#     schwung/        <- git clone https://github.com/charlesvestal/schwung.git
#     noiseboy/       <- this project
# Override with: SCHWUNG_DIR=/path/to/schwung ./scripts/build.sh
#
# IMPORTANT: this script has not been run against a real Schwung
# checkout in this environment (no network access here to clone it) --
# the exact include path below (schwung's own src/ layout) is inferred
# from docs/MODULES.md's build example, not verified by an actual
# build. If the path is wrong, the compiler error will point at
# exactly what's missing; fix the -I path below and re-run.

set -e

SCHWUNG_DIR="${SCHWUNG_DIR:-../schwung}"

if [ ! -f "$SCHWUNG_DIR/src/host/plugin_api_v1.h" ]; then
    echo "ERROR: could not find $SCHWUNG_DIR/src/host/plugin_api_v1.h"
    echo "Clone Schwung first: git clone https://github.com/charlesvestal/schwung.git"
    echo "as a sibling of this project folder, or set SCHWUNG_DIR=/path/to/schwung"
    exit 1
fi

mkdir -p build

docker run --rm \
    -v "$(pwd)":/work \
    -v "$(cd "$SCHWUNG_DIR" && pwd)":/schwung \
    -w /work \
    arm64v8/debian:bookworm \
    sh -c '
        apt-get update -qq && apt-get install -y -qq gcc make > /dev/null
        gcc -g -O3 -shared -fPIC \
            src/noiseboy_dsp.c src/noiseboy_plugin.c \
            -o build/dsp.so \
            -Isrc -I/schwung/src \
            -lm
    '

echo "Built: build/dsp.so"
