# Our own test-key signing library (docs/own_signing_plan.md). NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY.
# Only added when OKAY_OWN_SIGNING=ON (default OFF; releases refuse it). Not linked by the engine or the network code.
add_library(okay_sign STATIC src/BigInt.cpp src/Prime.cpp src/Der.cpp src/Rsa.cpp src/Pem.cpp src/Cert.cpp src/Cms.cpp)
target_include_directories(okay_sign PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(okay_sign PUBLIC cxx_std_17)
target_link_libraries(okay_sign PUBLIC okay_crypto)   # RandomBytes only
add_library(okay::sign ALIAS okay_sign)

if(OKAY_BUILD_TESTS)
    foreach(t test_sign_bigint test_sign_prime test_sign_der test_sign_openssl test_sign_rsa test_sign_rsa_openssl test_sign_cert test_sign_cert_openssl test_sign_cms test_sign_cms_openssl)
        add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} PRIVATE okay::sign)
    endforeach()
    add_test(NAME sign_bigint COMMAND test_sign_bigint)
    add_test(NAME sign_prime COMMAND test_sign_prime)
    add_test(NAME sign_der COMMAND test_sign_der)
    add_test(NAME sign_rsa COMMAND test_sign_rsa)
    add_test(NAME sign_cert COMMAND test_sign_cert)
    add_test(NAME sign_cms COMMAND test_sign_cms)
    add_test(NAME sign_cms_openssl_crosscheck COMMAND test_sign_cms_openssl)   # skips itself (exit 0) when openssl is missing
    add_test(NAME sign_cert_openssl_crosscheck COMMAND test_sign_cert_openssl)   # skips itself (exit 0) when openssl is missing
    add_test(NAME sign_rsa_openssl_crosscheck COMMAND test_sign_rsa_openssl)   # skips itself (exit 0) when openssl is missing
    add_test(NAME sign_openssl_crosscheck COMMAND test_sign_openssl)   # skips itself (exit 0) when openssl is missing
endif()
