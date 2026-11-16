# OkayPlatformSelfTest: a stand-alone program that exercises okay::platform on the machine it runs on and writes
# a plain report (docs/try_it.md, docs/platform_layer.md). Off by default in the sense that nothing starts it: it is
# a separate program. Shipped in the Windows zip as Tools\OkayPlatformSelfTest.exe so the owner can run the own
# Windows backend on a real machine; the Windows CI job runs it with --auto.
add_executable(okay_platform_selftest main.cpp)
target_link_libraries(okay_platform_selftest PRIVATE okay::platform)
set_target_properties(okay_platform_selftest PROPERTIES
    OUTPUT_NAME "OkayPlatformSelfTest"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# On Linux the program itself is tested under a private X server: every step that needs no sound card, microphone,
# controller or window manager runs and must not FAIL (SKIP and NOTSEEN are fine).
if(OKAY_BUILD_TESTS AND NOT WIN32)
    add_test(NAME platform_selftest_own
             COMMAND sh ${CMAKE_SOURCE_DIR}/tests/run_under_xvfb.sh $<TARGET_FILE:okay_platform_selftest>
                     --auto --backend own --report ${CMAKE_CURRENT_BINARY_DIR}/selftest-own-report.txt)
    # ALSA's "null" device lets the audio steps run on a machine with no sound card (it accepts and discards audio at real time).
    set_tests_properties(platform_selftest_own PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 ENVIRONMENT "OKAY_ALSA_DEVICE=null")
endif()
