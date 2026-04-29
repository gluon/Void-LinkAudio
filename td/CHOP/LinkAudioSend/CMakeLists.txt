cmake_minimum_required(VERSION 3.15)
get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME} LANGUAGES CXX)

message(STATUS "** Building ${PROJECT_NAME} (Universal) **")

# ------------------------------------------------------------------
# macOS Universal Binary (arm64 + x86_64)
# ------------------------------------------------------------------
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------
# Output directory
# ------------------------------------------------------------------
set(OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/plugin)

foreach(TYPE LIBRARY RUNTIME ARCHIVE)
    set(CMAKE_${TYPE}_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY})
endforeach()

# ------------------------------------------------------------------
# Project paths
# ------------------------------------------------------------------
# We are now under td-linkaudio/td/CHOP/<NameCHOP>/
# So 3 levels up reaches the repo root.
set(REPO_ROOT  "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(LINK_ROOT  "${REPO_ROOT}/thirdparty/link")
set(CORE_ROOT  "${REPO_ROOT}/core")

# ------------------------------------------------------------------
# Ableton Link (header-only) + bundled ASIO standalone
# ------------------------------------------------------------------
if(NOT EXISTS "${LINK_ROOT}/include/ableton/Link.hpp")
    message(FATAL_ERROR
        "Ableton Link not found at ${LINK_ROOT}.\n"
        "Did you run: git submodule update --init --recursive ?")
endif()

if(NOT EXISTS "${LINK_ROOT}/include/ableton/LinkAudio.hpp")
    message(FATAL_ERROR
        "LinkAudio.hpp not found. Your Link checkout does not contain the\n"
        "Link Audio API. Update the submodule to a recent enough revision.")
endif()

if(NOT EXISTS "${CORE_ROOT}/LinkAudioManager.h")
    message(FATAL_ERROR
        "core/LinkAudioManager.h not found at ${CORE_ROOT}.\n"
        "The core/ folder seems missing or moved.")
endif()

set(ASIO_INCLUDE "${LINK_ROOT}/modules/asio-standalone/asio/include")

# ------------------------------------------------------------------
# Sources (local CHOP files + shared core files)
# ------------------------------------------------------------------
file(GLOB LOCAL_SRC
    "*.cpp"
    "*.h"
    "*.hpp"
)

file(GLOB CORE_SRC
    "${CORE_ROOT}/*.cpp"
    "${CORE_ROOT}/*.h"
)

set(SRC ${LOCAL_SRC} ${CORE_SRC})

# ------------------------------------------------------------------
# Build CHOP plugin
# ------------------------------------------------------------------
add_library(${PROJECT_NAME} MODULE ${SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    BUNDLE TRUE
    BUNDLE_EXTENSION "plugin"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${LINK_ROOT}/include"
    "${ASIO_INCLUDE}"
    "${CORE_ROOT}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
)

if(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        LINK_PLATFORM_MACOSX=1
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework CoreFoundation"
    )
elseif(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        LINK_PLATFORM_WINDOWS=1
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ws2_32
        iphlpapi
        winmm
    )
elseif(UNIX)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        LINK_PLATFORM_LINUX=1
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        pthread
        atomic
    )
endif()

message(STATUS "Universal binary enabled: ${CMAKE_OSX_ARCHITECTURES}")
message(STATUS "Ableton Link include: ${LINK_ROOT}/include")
message(STATUS "Core sources: ${CORE_ROOT}")