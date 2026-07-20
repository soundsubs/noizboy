#!/bin/sh
set -e
if [ ! -f build/dsp.so ]; then
    echo "ERROR: build/dsp.so not found -- run ./scripts/build.sh first"
    exit 1
fi
mkdir -p dist
rm -rf dist/noiseboy
mkdir -p dist/noiseboy
cp module.json dist/noiseboy/
cp build/dsp.so dist/noiseboy/
tar -czf noiseboy-module.tar.gz -C dist noiseboy
echo "Packaged: noiseboy-module.tar.gz"
