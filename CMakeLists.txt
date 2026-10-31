cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Bismuth VERSION 1.0.0)

if (APPLE)
    enable_language(OBJCXX)
endif()

# I want to use `geode build` with clang but this is the only way to do it :(
message(STATUS "Found Geode: $ENV{CMAKE_C_COMPILER}")
if (DEFINED ENV{CMAKE_C_COMPILER})
    set(CMAKE_C_COMPILER $ENV{CMAKE_C_COMPILER})
endif()
if (DEFINED ENV{CMAKE_CXX_COMPILER})
    set(CMAKE_CXX_COMPILER $ENV{CMAKE_CXX_COMPILER})
endif()

# Add all source files inside src (recursively)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
if (APPLE)
    # Metal <-> OpenGL sharing and access to NSOpenGLContext / EAGLContext.
    file(GLOB_RECURSE APPLE_SOURCES CONFIGURE_DEPENDS src/*.mm)
    list(APPEND SOURCES ${APPLE_SOURCES})
    set_source_files_properties(${APPLE_SOURCES} PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
endif()

# Set up the mod binary
add_library(${PROJECT_NAME} SHARED ${SOURCES})

# Not sure about this...............
set(CMAKE_BUILD_TYPE RelWithDebInfo)

# Libraries
include(FetchContent)

FetchContent_Declare(
	glm
	GIT_REPOSITORY	https://github.com/g-truc/glm.git
	GIT_TAG 	0af55ccecd98d4e5a8d1fad7de25ba429d60e863
)
FetchContent_MakeAvailable(glm)

# bgfx (pinned + patched), see cmake/Bgfx.cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Bgfx.cmake)

target_link_libraries(${PROJECT_NAME} glm::glm bismuth_bgfx bismuth_vulkan_headers)
target_include_directories(${PROJECT_NAME} PRIVATE src)

if (WIN32)
    # GPU core count through OpenCL (loaded at runtime).
    FetchContent_Declare(
        OpenCL-Headers
        GIT_REPOSITORY	https://github.com/KhronosGroup/OpenCL-Headers.git
        GIT_TAG 	    v2026.05.29
    )
    FetchContent_MakeAvailable(OpenCL-Headers)
    target_link_libraries(${PROJECT_NAME} OpenCL::Headers)
elseif (ANDROID)
    target_link_libraries(${PROJECT_NAME} EGL android log)
elseif (IOS OR "${CMAKE_SYSTEM_NAME}" STREQUAL "iOS")
    target_link_libraries(${PROJECT_NAME}
        "-framework Metal" "-framework IOSurface" "-framework CoreVideo"
        "-framework OpenGLES" "-framework QuartzCore" "-framework Foundation"
    )
elseif (APPLE)
    target_link_libraries(${PROJECT_NAME}
        "-framework Metal" "-framework IOSurface" "-framework CoreVideo"
        "-framework OpenGL" "-framework AppKit" "-framework QuartzCore" "-framework Foundation"
    )
endif()

# Other Geode stuff

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Set up dependencies, resources, and link Geode.
setup_geode_mod(${PROJECT_NAME})
