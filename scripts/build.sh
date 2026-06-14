#!/usr/bin/env bash
# Cross-compile cook for aarch64 (Raspberry Pi 3B+ 64-bit) using Docker.
# Output: build/cross/cook
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="cook-builder:latest"
BUILD_DIR="build/cross"

if ! command -v docker &>/dev/null; then
    echo "ERROR: Docker not found."
    echo "       Install Docker Desktop: https://www.docker.com/products/docker-desktop/"
    exit 1
fi

if ! docker info &>/dev/null 2>&1; then
    echo "ERROR: Docker daemon is not running.  Start Docker Desktop and try again."
    exit 1
fi

echo "==> Building Docker image (uses cache if Dockerfile unchanged)"
docker build -q -t "$IMAGE" -f "$ROOT/docker/Dockerfile.build" "$ROOT"

echo "==> Cross-compiling for aarch64"
docker run --rm \
    -v "$ROOT:/src" \
    -w /src \
    "$IMAGE" \
    bash -c "
        set -e
        cmake -S . -B ${BUILD_DIR} \
              -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake \
              -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_VERBOSE_MAKEFILE=OFF
        cmake --build ${BUILD_DIR} --parallel \$(nproc)
        aarch64-linux-gnu-strip ${BUILD_DIR}/cook
    "

echo "==> Build complete"
file  "${ROOT}/${BUILD_DIR}/cook"
ls -lh "${ROOT}/${BUILD_DIR}/cook"
