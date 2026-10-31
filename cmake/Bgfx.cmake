# Builds bx, bimg and bgfx (static) for the mod.
#
# bgfx is pinned to the last revision (2023-05) that still supports OpenGL on
# macOS and iOS. Geometry Dash renders with OpenGL on every platform, so the
# OpenGL fallback must exist everywhere, including Apple platforms, while the
# default backends are Vulkan (Windows, Android) and Metal (macOS, iOS).
#
# The patch in cmake/patches adds host-application integration (rendering inside
# Geometry Dash's own GL context and sharing textures/semaphores with it).

include(${CMAKE_CURRENT_LIST_DIR}/FetchPinnedGit.cmake)

set(BISMUTH_BGFX_COMMIT "e7e75e4bffbf9f0acc1de1dfe601313db6bda3f6")
set(BISMUTH_BX_COMMIT   "7ac95d51314bb1a05508113cac0dc48314433488")
set(BISMUTH_BIMG_COMMIT "ec02df824a763b2e2ae31e19c674ba0bc88c0695")

bismuth_fetch_pinned_git(NAME bismuth_bx   URL https://github.com/bkaradzic/bx.git   COMMIT ${BISMUTH_BX_COMMIT})
bismuth_fetch_pinned_git(NAME bismuth_bimg URL https://github.com/bkaradzic/bimg.git COMMIT ${BISMUTH_BIMG_COMMIT})
bismuth_fetch_pinned_git(NAME bismuth_bgfx URL https://github.com/bkaradzic/bgfx.git COMMIT ${BISMUTH_BGFX_COMMIT}
    PATCHES ${CMAKE_CURRENT_LIST_DIR}/patches/bgfx-host-integration.patch)

set(BX_DIR   ${bismuth_bx_SOURCE_DIR})
set(BIMG_DIR ${bismuth_bimg_SOURCE_DIR})
set(BGFX_DIR ${bismuth_bgfx_SOURCE_DIR})

# Expose for tools (shader compiler includes, tests).
set(BISMUTH_BGFX_DIR ${BGFX_DIR} CACHE INTERNAL "")

if (MINGW)
    set(_bxCompat mingw)
elseif (WIN32)
    set(_bxCompat msvc)
elseif (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_bxCompat ios)
elseif (APPLE)
    set(_bxCompat osx)
else()
    set(_bxCompat linux)
endif()

function(_bismuth_bgfx_common target)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        C_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    if (MSVC)
        target_compile_options(${target} PRIVATE /Zc:__cplusplus /Zc:preprocessor /W0)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -w -fno-strict-aliasing)
    endif()
endfunction()

#### bx ####
add_library(bismuth_bx STATIC ${BX_DIR}/src/amalgamated.cpp)
_bismuth_bgfx_common(bismuth_bx)
target_include_directories(bismuth_bx SYSTEM PUBLIC
    ${BX_DIR}/include
    ${BX_DIR}/3rdparty
    ${BX_DIR}/include/compat/${_bxCompat}
)
target_compile_definitions(bismuth_bx PUBLIC
    BX_CONFIG_DEBUG=0
    __STDC_LIMIT_MACROS
    __STDC_FORMAT_MACROS
    __STDC_CONSTANT_MACROS
)
if (WIN32)
    target_link_libraries(bismuth_bx PUBLIC psapi)
elseif (ANDROID)
    target_link_libraries(bismuth_bx PUBLIC log)
elseif (APPLE)
    target_link_libraries(bismuth_bx PUBLIC "-framework Foundation")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(bismuth_bx PUBLIC Threads::Threads ${CMAKE_DL_LIBS} rt)
endif()

#### bimg (core only: bgfx needs texture format info and ASTC decode) ####
file(GLOB _astcSources ${BIMG_DIR}/3rdparty/astc-encoder/source/*.cpp)
add_library(bismuth_bimg STATIC
    ${BIMG_DIR}/src/image.cpp
    ${BIMG_DIR}/src/image_gnf.cpp
    ${_astcSources}
)
_bismuth_bgfx_common(bismuth_bimg)
target_include_directories(bismuth_bimg SYSTEM
    PUBLIC  ${BIMG_DIR}/include
    PRIVATE ${BIMG_DIR}/3rdparty/astc-encoder/include ${BIMG_DIR}/3rdparty
)
target_link_libraries(bismuth_bimg PUBLIC bismuth_bx)

#### bgfx ####
if (APPLE)
    set(_bgfxSource ${BGFX_DIR}/src/amalgamated.mm)
else()
    set(_bgfxSource ${BGFX_DIR}/src/amalgamated.cpp)
endif()

add_library(bismuth_bgfx STATIC ${_bgfxSource})
_bismuth_bgfx_common(bismuth_bgfx)

if (APPLE)
    # bgfx's Objective-C++ code uses manual reference counting.
    set_source_files_properties(${_bgfxSource} PROPERTIES COMPILE_OPTIONS "-fno-objc-arc;-x;objective-c++")
endif()

target_include_directories(bismuth_bgfx SYSTEM
    PUBLIC  ${BGFX_DIR}/include
    PRIVATE ${BGFX_DIR}/3rdparty ${BGFX_DIR}/3rdparty/khronos
)
if (WIN32)
    target_include_directories(bismuth_bgfx SYSTEM PRIVATE
        ${BGFX_DIR}/3rdparty/directx-headers/include/directx
        ${BGFX_DIR}/3rdparty/directx-headers/include
    )
endif()

target_link_libraries(bismuth_bgfx PUBLIC bismuth_bimg bismuth_bx)

# Renderer selection. Defining any BGFX_CONFIG_RENDERER_* disables all others.
#   Vulkan / Metal: default backends.
#   OpenGL / OpenGL ES: fallback, rendering directly inside Geometry Dash's context.
#     The value is the minimum version bgfx assumes at compile time; actual
#     features (compute, indirect draw) are detected at runtime by Bismuth.
set(_bgfxDefs
    BGFX_CONFIG_MULTITHREADED=0
    BGFX_CONFIG_DEBUG=0
    BGFX_CONFIG_DEBUG_ANNOTATION=0
    BGFX_CONFIG_PROFILER=0
)

if (ANDROID)
    # Android GD creates an ES 2 context, but drivers return ES 3.x in practice;
    # Bismuth refuses the GL fallback at runtime if the context is below 3.0.
    list(APPEND _bgfxDefs BGFX_CONFIG_RENDERER_VULKAN=1 BGFX_CONFIG_RENDERER_OPENGLES=30)
    target_link_libraries(bismuth_bgfx PUBLIC EGL GLESv3 android log)
elseif (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(APPEND _bgfxDefs BGFX_CONFIG_RENDERER_METAL=1 BGFX_CONFIG_RENDERER_OPENGLES=20)
    target_link_libraries(bismuth_bgfx PUBLIC
        "-framework OpenGLES" "-framework Metal" "-framework UIKit" "-framework CoreGraphics"
        "-framework QuartzCore" "-framework IOKit" "-framework CoreFoundation"
    )
elseif (APPLE)
    list(APPEND _bgfxDefs BGFX_CONFIG_RENDERER_METAL=1 BGFX_CONFIG_RENDERER_OPENGL=21)
    target_link_libraries(bismuth_bgfx PUBLIC
        "-framework Cocoa" "-framework Metal" "-framework QuartzCore"
        "-framework IOKit" "-framework CoreFoundation"
    )
elseif (WIN32)
    list(APPEND _bgfxDefs BGFX_CONFIG_RENDERER_VULKAN=1 BGFX_CONFIG_RENDERER_OPENGL=21)
    target_link_libraries(bismuth_bgfx PUBLIC gdi32 user32)
else()
    # Linux: only used for tests (Geometry Dash runs the Windows build under Wine).
    if (BISMUTH_BGFX_LINUX_GLES)
        list(APPEND _bgfxDefs BGFX_CONFIG_RENDERER_VULKAN=1 BGFX_CONFIG_RENDERER_OPENGLES=31)
        target_link_libraries(bismuth_bgfx PUBLIC EGL GLESv2)
    else()
        list(APPEND _bgfxDefs BGFX_CONFIG_RENDERER_VULKAN=1 BGFX_CONFIG_RENDERER_OPENGL=21)
        find_package(X11 REQUIRED)
        target_link_libraries(bismuth_bgfx PUBLIC ${X11_LIBRARIES} GL)
    endif()
endif()

target_compile_definitions(bismuth_bgfx PUBLIC ${_bgfxDefs})

# Vulkan headers bundled with bgfx, for Bismuth's own interop code.
add_library(bismuth_vulkan_headers INTERFACE)
target_include_directories(bismuth_vulkan_headers SYSTEM INTERFACE ${BGFX_DIR}/3rdparty/khronos)
