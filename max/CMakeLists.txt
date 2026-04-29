cmake_minimum_required(VERSION 3.19)
get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME})

# ----------------------------------------------------------------------------
# macOS Universal Binary (arm64 + x86_64)
# Must be set before max-pretarget.cmake is included by sub-projects.
# ----------------------------------------------------------------------------
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# ----------------------------------------------------------------------------
# Max SDK path resolution
# ----------------------------------------------------------------------------
# Priority order:
#   1. Explicit CMake var (-DMAX_SDK_PATH=...)
#   2. MAX_SDK_PATH environment variable
#   3. Hardcoded fallback (Julien's local install)
#
# Override on the command line:
#   cmake -B build -DMAX_SDK_PATH=/path/to/max-sdk
# ----------------------------------------------------------------------------
if(NOT DEFINED MAX_SDK_PATH)
    if(DEFINED ENV{MAX_SDK_PATH})
        set(MAX_SDK_PATH $ENV{MAX_SDK_PATH})
    else()
        message(FATAL_ERROR "MAX_SDK_PATH not set. Export MAX_SDK_PATH=/path/to/max-sdk or pass -DMAX_SDK_PATH=/path/to/max-sdk to cmake. Get the SDK from https://github.com/Cycling74/max-sdk")
    endif()
endif()

if(NOT EXISTS "${MAX_SDK_PATH}/source/max-sdk-base/script/max-pretarget.cmake")
    message(FATAL_ERROR
        "Max SDK not found at ${MAX_SDK_PATH}.\n"
        "Set MAX_SDK_PATH (env var or -DMAX_SDK_PATH) to point at a valid max-sdk checkout.")
endif()

message(STATUS "Max SDK path: ${MAX_SDK_PATH}")

# ----------------------------------------------------------------------------
# Shared paths (used by every external in this folder)
# ----------------------------------------------------------------------------
set(VOID_REPO_ROOT  "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(VOID_CORE_ROOT  "${VOID_REPO_ROOT}/core")
set(VOID_LINK_ROOT  "${VOID_REPO_ROOT}/thirdparty/link")

if(NOT EXISTS "${VOID_LINK_ROOT}/include/ableton/LinkAudio.hpp")
    message(FATAL_ERROR
        "Ableton Link not found at ${VOID_LINK_ROOT}.\n"
        "Did you run: git submodule update --init --recursive ?")
endif()

if(NOT EXISTS "${VOID_CORE_ROOT}/LinkAudioManager.h")
    message(FATAL_ERROR
        "core/LinkAudioManager.h not found at ${VOID_CORE_ROOT}.")
endif()

set(VOID_ASIO_INCLUDE "${VOID_LINK_ROOT}/modules/asio-standalone/asio/include")

# ----------------------------------------------------------------------------
# Auto-discover externals in source/objects/
# ----------------------------------------------------------------------------
file(GLOB OBJ_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/source/objects/*")

foreach(obj_path ${OBJ_DIRS})
    if(IS_DIRECTORY "${obj_path}" AND EXISTS "${obj_path}/CMakeLists.txt")
        get_filename_component(obj_name "${obj_path}" NAME)
        message(STATUS "Adding Max external: ${obj_name}")
        add_subdirectory("${obj_path}")
    endif()
endforeach()

message(STATUS "Universal binary: ${CMAKE_OSX_ARCHITECTURES}")