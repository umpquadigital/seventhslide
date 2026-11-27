# What a SeventhSlide program carries on Windows: its manifest
# (packaging/windows/seventhslide.manifest: UTF-8 as the code page, long
# paths, no elevation) and, for the app, its icon. Nothing elsewhere.

set(SEVENTHSLIDE_WINDOWS_MANIFEST "${CMAKE_SOURCE_DIR}/packaging/windows/seventhslide.manifest")

if(WIN32)
    # The programs' icon and manifest are resource scripts.
    enable_language(RC)
endif()

# MinGW's windres runs the C preprocessor through cmd.exe, by the full path
# of the gcc beside it. Given any other quoted argument (CMake quotes every
# -I), cmd drops the quotes round that path, so a toolchain in a folder with
# a space (C:\Users\First Last\scoop\apps\msys2) cannot be started: "'C:\Users
# \First' is not recognized". Named without its folder, the preprocessor is
# found on PATH instead, when PATH finds this same compiler.
if(MINGW AND CMAKE_RC_COMPILER MATCHES "windres")
    get_filename_component(_ss_rc_dir "${CMAKE_RC_COMPILER}" DIRECTORY)
    if(_ss_rc_dir MATCHES " ")
        get_filename_component(_ss_cxx_name "${CMAKE_CXX_COMPILER}" NAME_WE)
        find_program(_ss_cxx_on_path NAMES "${_ss_cxx_name}" NO_CACHE
            NO_PACKAGE_ROOT_PATH NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH
            NO_CMAKE_SYSTEM_PATH NO_CMAKE_INSTALL_PREFIX)
        get_filename_component(_ss_cxx_real "${CMAKE_CXX_COMPILER}" REALPATH)
        get_filename_component(_ss_cxx_on_path_real "${_ss_cxx_on_path}" REALPATH)
        if(_ss_cxx_on_path AND _ss_cxx_on_path_real STREQUAL _ss_cxx_real)
            string(APPEND CMAKE_RC_FLAGS " --preprocessor=${_ss_cxx_name}"
                " --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED")
            message(STATUS "SeventhSlide: windres preprocesses with ${_ss_cxx_name} from PATH "
                "(its own folder has a space)")
        else()
            message(WARNING "SeventhSlide: windres is in a folder with a space "
                "(${_ss_rc_dir}) and cannot start its preprocessor from there. Put "
                "the toolchain's bin folder on PATH, or install it where there is no space.")
        endif()
    endif()
endif()

# seventhslide_windows_executable(<target> [ICON <file.ico>])
function(seventhslide_windows_executable target)
    if(NOT WIN32)
        return()
    endif()
    cmake_parse_arguments(arg "" "ICON" "" ${ARGN})
    set(rc "")
    if(arg_ICON)
        string(APPEND rc "IDI_ICON1 ICON \"${arg_ICON}\"\n")
    endif()
    if(MSVC)
        # MSVC's linker merges a listed .manifest into the one it writes.
        target_sources(${target} PRIVATE "${SEVENTHSLIDE_WINDOWS_MANIFEST}")
    else()
        # MinGW's writes none: the manifest is resource 1 of type 24
        # (RT_MANIFEST).
        string(APPEND rc "1 24 \"${SEVENTHSLIDE_WINDOWS_MANIFEST}\"\n")
    endif()
    if(rc)
        set(rc_file "${CMAKE_CURRENT_BINARY_DIR}/${target}_windows.rc")
        file(GENERATE OUTPUT "${rc_file}" CONTENT "${rc}")
        target_sources(${target} PRIVATE "${rc_file}")
    endif()
endfunction()
