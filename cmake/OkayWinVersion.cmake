# okay_win_version(<target> <Description> <OriginalFilename>)
#
# Give a Windows exe its own name, description and version (Task Manager, file
# Properties) and an application manifest. Without this an exe carries whatever
# version block the linker finds, and MinGW's libwinpthread brought "POSIX
# WinThreads for Windows" (see the toolchain file, which keeps that one out).
# No-op off Windows.
function(okay_win_version target description filename)
    if(NOT WIN32)
        return()
    endif()
    enable_language(RC)
    get_filename_component(_internal "${filename}" NAME_WE)
    set(OKAY_RC_DESCRIPTION "${description}")
    set(OKAY_RC_FILENAME    "${filename}")
    set(OKAY_RC_INTERNAL    "${_internal}")
    set(OKAY_RC_VERSION     "${PROJECT_VERSION}")
    set(OKAY_RC_MAJOR       "${PROJECT_VERSION_MAJOR}")
    set(OKAY_RC_MINOR       "${PROJECT_VERSION_MINOR}")
    set(OKAY_RC_PATCH       "${PROJECT_VERSION_PATCH}")
    set(OKAY_RC_MANIFEST    "${CMAKE_SOURCE_DIR}/cmake/okayspace.exe.manifest")
    set(_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/okay_version.rc.in" "${_rc}" @ONLY)
    target_sources(${target} PRIVATE "${_rc}")
    # Rebuild the resource when the manifest changes.
    set_source_files_properties("${_rc}" PROPERTIES OBJECT_DEPENDS "${OKAY_RC_MANIFEST}")
endfunction()
