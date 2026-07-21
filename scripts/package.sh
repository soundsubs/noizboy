#!/bin/sh
# Packages module.json + build/dsp.so into a tarball for GitHub Release
# distribution, per the "Install Custom Module" flow in Schwung Manager
# (move.local:7700/modules), which reads release.json from the repo
# root and downloads/extracts the tarball named at its download_url.

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
cp src/ui.js dist/noiseboy/

tar -czf noiseboy-module.tar.gz -C dist noiseboy

echo "Packaged: noiseboy-module.tar.gz"
echo "Next: create a GitHub Release, attach this file as an asset,"
echo "then update release.json's download_url to match the release's"
echo "actual asset URL."
