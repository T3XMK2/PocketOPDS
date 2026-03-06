#!/usr/bin/env bash
# build.sh – build PocketOPDS on Linux / WSL2
#
# Requirements:
#   - PBSDK env var pointing to extracted PocketBook SDK root
#     e.g.  export PBSDK=/opt/pocketbook-sdk
#   - cmake, make
#
# The SDK can be downloaded from:
#   https://github.com/pocketbook/SDK_6.3.0/releases/tag/6.8
#   → SDK-B300-6.8.7z  (Verse Pro and most modern 6" devices)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Check prerequisites ───────────────────────────────────────────────────────
if [[ -z "${PBSDK:-}" ]]; then
    echo "ERROR: PBSDK is not set."
    echo "  Download SDK-B300-6.8.7z from:"
    echo "  https://github.com/pocketbook/SDK_6.3.0/releases/tag/6.8"
    echo "  Extract it and set:"
    echo "    export PBSDK=/path/to/extracted/sdk"
    exit 1
fi

if [[ ! -f "${PBSDK}/SDK_6.3.0/toolchain/bin/arm-obreey-linux-gnueabi-gcc" ]]; then
    echo "ERROR: Toolchain not found at \${PBSDK}/SDK_6.3.0/toolchain/bin/"
    echo "  Expected: arm-obreey-linux-gnueabi-gcc"
    echo "  Check that PBSDK points to the correct directory."
    exit 1
fi

for cmd in cmake make; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "ERROR: '$cmd' is not installed. Install it with:"
        echo "  sudo apt install cmake make"
        exit 1
    fi
done

# ── Build ─────────────────────────────────────────────────────────────────────
BUILD_DIR="${SCRIPT_DIR}/build"
mkdir -p "${BUILD_DIR}"

echo "=== Configuring ==="
cmake -B "${BUILD_DIR}" \
      -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/toolchain-arm.cmake" \
      -DCMAKE_BUILD_TYPE=Release \
      -S "${SCRIPT_DIR}"

echo ""
echo "=== Compiling ==="
cmake --build "${BUILD_DIR}" --parallel

echo ""
echo ">>> Build successful: build/pocketopds.app"
file "${BUILD_DIR}/pocketopds.app"
echo ""
echo "Copy to device:"
echo "  /mnt/ext1/applications/pocketopds.app"
