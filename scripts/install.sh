#!/bin/sh
# scripts/install.sh -- deploy DISTROY.so + module.json to your Move
# directly over SSH (bypasses the Module Store / Install Custom Module
# UI -- useful for iterating during development).
#
# Confirmed path/user convention (verified working via EMAX_FX):
#   /data/UserData/schwung/modules/audio_fx/DISTROY/module.json
#   /data/UserData/schwung/modules/audio_fx/DISTROY/DISTROY.so
# as ableton@move.local (not root@).

set -e

DEVICE_HOST="${DEVICE_HOST:-move.local}"
MODULE_NAME=DISTROY
MODULE_CATEGORY=audio_fx
REMOTE_DIR="/data/UserData/schwung/modules/${MODULE_CATEGORY}/${MODULE_NAME}"
BIN_PATH="build-arm64/${MODULE_NAME}.so"
MANIFEST_PATH="module.json"

if [ ! -f "$BIN_PATH" ]; then
    echo "error: $BIN_PATH not found -- run scripts/build.sh first" >&2
    exit 1
fi
if [ ! -f "$MANIFEST_PATH" ]; then
    echo "error: $MANIFEST_PATH not found at repo root" >&2
    exit 1
fi

echo "==> Deploying to ${DEVICE_HOST}:${REMOTE_DIR}"
ssh "ableton@${DEVICE_HOST}" "mkdir -p '${REMOTE_DIR}'"
scp "$BIN_PATH" "ableton@${DEVICE_HOST}:${REMOTE_DIR}/${MODULE_NAME}.so"
scp "$MANIFEST_PATH" "ableton@${DEVICE_HOST}:${REMOTE_DIR}/module.json"

echo "==> Done. Restart Schwung Manager (or reboot Move) so it picks up"
echo "    the new module directory, then check move.local:7700/modules"
echo "    or add it to a Signal Chain slot's Audio FX."
