# ctest driver: cmake -DKEYS=<list> -P anticheat_keys_check.cmake
# Succeeds when the list is accepted, fails (FATAL_ERROR) when it's refused; the
# bad-list tests are marked WILL_FAIL in tests/CMakeLists.txt.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/OkayAntiCheatKeys.cmake)
okay_ac_check_trusted_keys("${KEYS}")
message(STATUS "accepted: ${KEYS}")
