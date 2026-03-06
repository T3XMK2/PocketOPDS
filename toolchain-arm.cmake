# ARM cross-compilation toolchain for PocketBook SDK 6.8
# Set PBSDK environment variable to the extracted SDK root before calling cmake.
# Example (Linux/WSL):
#   export PBSDK=/opt/pocketbook-sdk
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=../toolchain-arm.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Resolve SDK root from environment variable
if(DEFINED ENV{PBSDK})
    set(PBSDK $ENV{PBSDK})
else()
    message(FATAL_ERROR
        "PBSDK environment variable is not set.\n"
        "Point it to the extracted PocketBook SDK root, e.g.:\n"
        "  export PBSDK=/opt/pocketbook-sdk\n"
        "The folder should contain SDK-B300-6.8/ subdirectory.")
endif()

set(SDK_ROOT      "${PBSDK}/SDK-B300-6.8")
set(TOOLCHAIN_BIN "${SDK_ROOT}/usr/bin")
set(SYSROOT       "${SDK_ROOT}/usr/arm-obreey-linux-gnueabi/sysroot")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_BIN}/arm-obreey-linux-gnueabi-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_BIN}/arm-obreey-linux-gnueabi-g++")
set(CMAKE_SYSROOT      "${SYSROOT}")

set(CMAKE_FIND_ROOT_PATH "${SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
