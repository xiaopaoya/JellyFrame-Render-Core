if(NOT DEFINED CORE_BINARY_DIR OR NOT DEFINED CORE_SOURCE_ROOT OR
   NOT DEFINED TEST_GENERATOR OR NOT DEFINED TEST_CXX_COMPILER OR NOT DEFINED EXPECT_CODEC)
    message(FATAL_ERROR "Missing JFSP v2 installed consumer test configuration")
endif()

if(NOT TEST_CONFIGURATION)
    set(TEST_CONFIGURATION Release)
endif()
set(prefix "${CORE_BINARY_DIR}/shaped-packet-v2-${EXPECT_CODEC}-install")
set(build "${CORE_BINARY_DIR}/shaped-packet-v2-${EXPECT_CODEC}-consumer")

function(run_checked action)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${action} failed: ${result}")
    endif()
endfunction()

run_checked("Installing Core" "${CMAKE_COMMAND}" --install "${CORE_BINARY_DIR}"
    --config "${TEST_CONFIGURATION}" --prefix "${prefix}")
run_checked("Configuring installed consumer" "${CMAKE_COMMAND}"
    -S "${CORE_SOURCE_ROOT}/tests/package/shaped_packet_v2_consumer" -B "${build}"
    -G "${TEST_GENERATOR}" "-DCMAKE_PREFIX_PATH=${prefix}"
    "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}" "-DCMAKE_BUILD_TYPE=${TEST_CONFIGURATION}"
    "-DEXPECT_CODEC=${EXPECT_CODEC}")
run_checked("Building installed consumer" "${CMAKE_COMMAND}" --build "${build}"
    --config "${TEST_CONFIGURATION}")
run_checked("Testing installed consumer" "${CMAKE_CTEST_COMMAND}" --test-dir "${build}"
    -C "${TEST_CONFIGURATION}" --output-on-failure)
