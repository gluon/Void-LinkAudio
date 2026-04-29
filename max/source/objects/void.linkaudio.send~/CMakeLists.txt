get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME})

# Max SDK pre-target (sets up universal binary, plist, output dirs etc.)
include(${MAX_SDK_PATH}/source/max-sdk-base/script/max-pretarget.cmake)

# ----------------------------------------------------------------------------
# Sources
#   - all .c/.cpp/.h in this folder
#   - all .cpp/.h from VoidLinkAudio's shared core/
# ----------------------------------------------------------------------------
file(GLOB LOCAL_SRC
    "*.c"
    "*.cpp"
    "*.h"
)

file(GLOB CORE_SRC
    "${VOID_CORE_ROOT}/*.cpp"
    "${VOID_CORE_ROOT}/*.h"
)

set(PROJECT_SRC ${LOCAL_SRC} ${CORE_SRC})

include_directories(
    "${MAX_SDK_INCLUDES}"
    "${MAX_SDK_MSP_INCLUDES}"
    "${MAX_SDK_JIT_INCLUDES}"
    "${VOID_CORE_ROOT}"
    "${VOID_LINK_ROOT}/include"
    "${VOID_ASIO_INCLUDE}"
)

add_library(${PROJECT_NAME} MODULE ${PROJECT_SRC})

# C++17 for std::optional, etc. (used in core/)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
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
endif()

# Max SDK post-target (generates .mxo bundle, signing, etc.)
include(${MAX_SDK_PATH}/source/max-sdk-base/script/max-posttarget.cmake)