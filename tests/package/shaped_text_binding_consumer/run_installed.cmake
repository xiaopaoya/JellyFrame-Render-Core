if(NOT DEFINED CORE_BINARY_DIR OR NOT DEFINED CORE_SOURCE_ROOT OR
   NOT DEFINED TEST_GENERATOR)
    message(FATAL_ERROR "Missing installed consumer test configuration")
endif()
set(install_prefix "${CORE_BINARY_DIR}/shaped-text-binding-install")
set(consumer_build "${CORE_BINARY_DIR}/shaped-text-binding-consumer-build")
if(NOT TEST_CONFIGURATION)
    set(TEST_CONFIGURATION Release)
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${CORE_BINARY_DIR}"
    --prefix "${install_prefix}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Core installation failed: ${result}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S
    "${CORE_SOURCE_ROOT}/tests/package/shaped_text_binding_consumer" -B
    "${consumer_build}" -G "${TEST_GENERATOR}"
    "-DCMAKE_BUILD_TYPE=${TEST_CONFIGURATION}"
    "-DCMAKE_PREFIX_PATH=${install_prefix}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed consumer configuration failed: ${result}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}"
    --config "${TEST_CONFIGURATION}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed consumer build failed: ${result}")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_build}"
    -C "${TEST_CONFIGURATION}" --output-on-failure RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed consumer test failed: ${result}")
endif()
