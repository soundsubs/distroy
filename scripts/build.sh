#!/bin/sh
# scripts/build.sh -- cross-compile DISTROY.so for Move (aarch64 Linux).
#
# DISTROY.so is a Schwung audio_fx PLUGIN -- a shared library dlopen()'d
# by Schwung's chain host, exporting move_audio_fx_init_v2(). Same
# architecture as EMAX_FX, using the same cross-compile approach
# (verified working there).
#
# Requires: Docker.

set -e

IMAGE_NAME=distroy-builder
OUT_DIR="$(pwd)/build-arm64"

mkdir -p "$OUT_DIR"

cat > /tmp/distroy-Dockerfile <<'EOF'
FROM arm64v8/debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential pkg-config \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
EOF

echo "==> Building cross-compile image (arm64v8/debian base, needs QEMU"
echo "    user-mode emulation registered -- run 'docker run --privileged"
echo "    --rm tonistiigi/binfmt --install arm64' once if this image"
echo "    fails to start)."
docker build --platform=linux/arm64 -t "$IMAGE_NAME" -f /tmp/distroy-Dockerfile .

echo "==> Compiling inside container..."
docker run --rm --platform=linux/arm64 \
    -v "$(pwd)":/work \
    -v "$OUT_DIR":/out \
    "$IMAGE_NAME" \
    sh -c "cc -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -fPIC \
           -shared -o /out/DISTROY.so src/distroy_dsp.c src/distroy_audio_fx.c -lm"

echo "==> Built: $OUT_DIR/DISTROY.so"
echo "    Next: scripts/package.sh"
