# Shallow-fetches a git repository at an exact commit, optionally applying patches.
#
# FetchContent can't shallow-clone a commit SHA, and bgfx's full history is very
# large, so this does `git fetch --depth 1 <url> <sha>` instead. The checkout is
# re-done only when the commit or the patch contents change.
#
#   bismuth_fetch_pinned_git(
#       NAME    <name>            # result variable <name>_SOURCE_DIR
#       URL     <git url>
#       COMMIT  <full sha>
#       [PATCHES <patch files...>])

find_package(Git REQUIRED)

function(bismuth_fetch_pinned_git)
    cmake_parse_arguments(ARG "" "NAME;URL;COMMIT" "PATCHES" ${ARGN})

    if (DEFINED FETCHCONTENT_BASE_DIR)
        set(baseDir "${FETCHCONTENT_BASE_DIR}")
    else()
        set(baseDir "${CMAKE_BINARY_DIR}/_deps")
    endif()

    set(srcDir "${baseDir}/${ARG_NAME}-src")

    # Stamp = commit + hash of every patch, so editing a patch re-fetches.
    set(stamp "${ARG_COMMIT}")
    foreach(patch IN LISTS ARG_PATCHES)
        file(SHA256 "${patch}" patchHash)
        string(APPEND stamp ";${patchHash}")
    endforeach()

    set(stampFile "${srcDir}/.bismuth-stamp")
    set(currentStamp "")
    if (EXISTS "${stampFile}")
        file(READ "${stampFile}" currentStamp)
    endif()

    if (NOT currentStamp STREQUAL stamp)
        message(STATUS "Fetching ${ARG_NAME} @ ${ARG_COMMIT}")

        file(REMOVE_RECURSE "${srcDir}")
        file(MAKE_DIRECTORY "${srcDir}")

        macro(_bismuth_git)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" ${ARGV}
                WORKING_DIRECTORY "${srcDir}"
                RESULT_VARIABLE result
                OUTPUT_VARIABLE output
                ERROR_VARIABLE  output
            )
            if (NOT result EQUAL 0)
                message(FATAL_ERROR "git ${ARGV} failed for ${ARG_NAME}:\n${output}")
            endif()
        endmacro()

        # Keep upstream line endings (Windows runners default to core.autocrlf=true,
        # which would make the LF patches fail to apply).
        _bismuth_git(init -q)
        _bismuth_git(config core.autocrlf false)
        _bismuth_git(config core.eol lf)
        _bismuth_git(fetch -q --depth 1 "${ARG_URL}" "${ARG_COMMIT}")
        _bismuth_git(-c advice.detachedHead=false checkout -q FETCH_HEAD)

        foreach(patch IN LISTS ARG_PATCHES)
            message(STATUS "Applying ${patch}")
            _bismuth_git(apply --whitespace=nowarn --ignore-whitespace "${patch}")
        endforeach()

        file(WRITE "${stampFile}" "${stamp}")
    endif()

    set(${ARG_NAME}_SOURCE_DIR "${srcDir}" PARENT_SCOPE)
endfunction()
