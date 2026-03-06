# PocketOPDS build environment
#
# Installs the PocketBook SDK B300-6.8 inside an Ubuntu container and
# compiles the project.  The resulting pocketopds.app binary is copied
# to /workspace/build/ which is bind-mounted from the host.
#
# Usage (from repo root):
#   docker build -t pocketopds-builder .
#   docker run --rm -v "$(pwd):/workspace" pocketopds-builder
#
# Or just run:  ./build.ps1  (Windows PowerShell)

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# ── System tools ──────────────────────────────────────────────────────────────
RUN apt-get update && apt-get install -y \
        wget \
        p7zip-full \
        cmake \
        make \
        file \
        libmpfr6 \
    && rm -rf /var/lib/apt/lists/* \
    && ln -s /usr/lib/x86_64-linux-gnu/libmpfr.so.6 /usr/lib/x86_64-linux-gnu/libmpfr.so.4

# ── Download and extract PocketBook SDK B300-6.8 (~540 MB) ───────────────────
# The SDK contains arm-obreey-linux-gnueabi-gcc and a full ARM sysroot
# with libinkview.so, libcurl, libexpat, etc.
ENV PBSDK=/opt/pocketbook-sdk
ENV SDK_URL=https://github.com/pocketbook/SDK_6.3.0/releases/download/6.8/SDK-B300-6.8.7z

RUN mkdir -p "${PBSDK}" \
    && echo "Downloading PocketBook SDK B300-6.8 (~540 MB)…" \
    && wget -q --show-progress "${SDK_URL}" -O /tmp/sdk.7z \
    && echo "Extracting SDK…" \
    && 7z x /tmp/sdk.7z -o"${PBSDK}" -y \
    && rm /tmp/sdk.7z \
    && echo "SDK installed at ${PBSDK}"

# Make the toolchain compiler executable (SDK ships with Linux ELF binaries)
RUN chmod +x "${PBSDK}"/SDK_6.3.0/toolchain/bin/* 2>/dev/null || true

# ── Build ─────────────────────────────────────────────────────────────────────
WORKDIR /workspace

CMD ["bash", "-c", \
     "echo '=== Building PocketOPDS ===' \
     && cmake -B /build \
              -DCMAKE_TOOLCHAIN_FILE=/workspace/toolchain-arm.cmake \
              -DCMAKE_BUILD_TYPE=Release \
     && cmake --build /build --parallel \
     && mkdir -p /workspace/build \
     && cp /build/PocketOPDS.app /workspace/build/PocketOPDS.app \
     && echo '' \
     && echo '>>> Build successful: build/PocketOPDS.app' \
     && file /workspace/build/PocketOPDS.app"]
