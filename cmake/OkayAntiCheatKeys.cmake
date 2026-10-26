# Anti-cheat: validate -DOKAY_AC_TRUSTED_KEYS (docs/anticheat.md). Each key must be
# exactly 64 hex digits (an ed25519 public key), comma-separated, nothing else:
# a malformed list must stop the configure, never quietly build a player that
# trusts nothing (or, worse, falls back to an environment key).
# Used by the top-level CMakeLists.txt and by tests/anticheat_keys_check.cmake.
function(okay_ac_check_trusted_keys keys)
    if(keys STREQUAL "")
        return()
    endif()
    string(REPLACE "," ";" _okay_ac_list "${keys}")
    foreach(_okay_ac_key IN LISTS _okay_ac_list)
        string(LENGTH "${_okay_ac_key}" _okay_ac_len)
        if(NOT _okay_ac_len EQUAL 64 OR NOT _okay_ac_key MATCHES "^[0-9a-fA-F]+$")
            message(FATAL_ERROR "OKAY_AC_TRUSTED_KEYS: \"${_okay_ac_key}\" isn't an ed25519 public key "
                                "(exactly 64 hex digits; separate keys with commas, no spaces)")
        endif()
    endforeach()
    # "a,,b" or a trailing comma leaves an empty entry the loop above skips.
    if(keys MATCHES ",," OR keys MATCHES "^," OR keys MATCHES ",$")
        message(FATAL_ERROR "OKAY_AC_TRUSTED_KEYS: empty entry in the key list")
    endif()
endfunction()
