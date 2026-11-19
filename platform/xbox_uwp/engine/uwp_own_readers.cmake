# okay_uwp_own_readers(<root CMakeLists.txt> <out var of compile definitions> <out var of format names>)
# Reads the `option(OKAY_OWN_<FMT> "..." ON|OFF)` lines of the root CMakeLists.txt and returns the compile definitions ModelImport.cpp reads
# (OKAY_PREFER_OWN_READERS_DEFAULT and OKAY_OWN_<FMT>_DEFAULT for every format whose default is ON). FBX (FbxScene.cpp, forced on under OKAY_XBOX_UWP) and HTTP
# (not a model reader) are left out. A reader added to the root options is on in the UWP build without an edit here.
# Used by platform/xbox_uwp/engine/CMakeLists.txt (the UWP engine) and platform/xbox_uwp/CMakeLists.txt (the Linux test of the same defines).
# Written by us for this repository; no outside code.
function(okay_uwp_own_readers root_cmake out_defs out_formats)
    file(STRINGS "${root_cmake}" _opts REGEX "^option\\(OKAY_OWN_[A-Z0-9]+ ")
    set(_on "")
    foreach(_line IN LISTS _opts)
        if(_line MATCHES "^option\\(OKAY_OWN_([A-Z0-9]+) .*\" (ON|OFF)\\)")
            if(CMAKE_MATCH_2 STREQUAL "ON")
                list(APPEND _on ${CMAKE_MATCH_1})
            endif()
        endif()
    endforeach()
    set(_defs OKAY_PREFER_OWN_READERS_DEFAULT)
    foreach(_f IN LISTS _on)
        if(NOT _f STREQUAL "FBX" AND NOT _f STREQUAL "HTTP")
            list(APPEND _defs OKAY_OWN_${_f}_DEFAULT)
        endif()
    endforeach()
    set(${out_defs} "${_defs}" PARENT_SCOPE)
    set(${out_formats} "${_on}" PARENT_SCOPE)
endfunction()
