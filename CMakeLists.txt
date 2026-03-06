cmake_minimum_required(VERSION 3.10)
project(PocketOPDS C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- SDK sysroot paths (set by toolchain file) ---------------------------------
set(PBSDK_SYSROOT "${CMAKE_SYSROOT}")

include_directories(
    ${PBSDK_SYSROOT}/usr/local/include   # inkview.h
    ${PBSDK_SYSROOT}/usr/include
    ${CMAKE_SOURCE_DIR}/src              # project headers
)

link_directories(
    ${PBSDK_SYSROOT}/usr/local/lib       # libinkview.so
    ${PBSDK_SYSROOT}/usr/lib
    ${PBSDK_SYSROOT}/lib
)

# ---- Source files --------------------------------------------------------------
set(SOURCES
    src/main.c
    src/config.c
    src/net.c
    src/opds.c
    src/screen_servers.c
    src/screen_catalog.c
    src/screen_detail.c
)

add_executable(PocketOPDS ${SOURCES})

# ---- Linked libraries ----------------------------------------------------------
target_link_libraries(PocketOPDS
    inkview     # InkView UI / system API
    curl        # libcurl  (HTTP, auth, HTTPS)
    expat       # Expat    (XML/Atom parsing)
    pthread
    m
)

# ---- ARM compile flags ---------------------------------------------------------
target_compile_options(PocketOPDS PRIVATE
    -march=armv7-a
    -mfloat-abi=softfp
    -mfpu=neon
    -O2
    -Wall
    -Wextra
    -Wno-unused-parameter
)

# ---- Output as .app (PocketBook expects this extension) ------------------------
set_target_properties(PocketOPDS PROPERTIES OUTPUT_NAME "PocketOPDS.app")

# ---- Runtime library search path on the device --------------------------------
# PocketBook firmware keeps shared libs in /usr/lib and /usr/local/lib.
# Embedding the RPATH ensures the dynamic linker finds libcurl, libexpat etc.
# even when LD_LIBRARY_PATH is not set by the launcher.
target_link_options(PocketOPDS PRIVATE
    -Wl,-rpath,/usr/lib
    -Wl,-rpath,/usr/local/lib
    -Wl,-rpath,/lib
)

install(TARGETS PocketOPDS DESTINATION applications)
