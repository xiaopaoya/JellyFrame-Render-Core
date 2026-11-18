add_executable(jellyframe_shaping_catalog_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/shaping_catalog_tests.cpp")
target_link_libraries(jellyframe_shaping_catalog_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_shaping_catalog_tests COMMAND jellyframe_shaping_catalog_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/fixtures/shaping_catalog" "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/fixtures/shaping_resource")

add_executable(jellyframe_shaping_resource_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/shaping_resource_tests.cpp")
target_link_libraries(jellyframe_shaping_resource_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_shaping_resource_tests COMMAND jellyframe_shaping_resource_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/fixtures/shaping_resource")

# Only executables registered by this module receive the test CRT bootstrap.
if(WIN32)
    get_property(_jellyframe_targets_before_tests DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
endif()
add_executable(jellyframe_shaping_normalization_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/shaping_normalization_tests.cpp")
target_link_libraries(jellyframe_shaping_normalization_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_shaping_normalization_tests COMMAND jellyframe_shaping_normalization_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/fixtures")
add_executable(jellyframe_shaped_text_service_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/shaped_text_service_tests.cpp")
target_link_libraries(jellyframe_shaped_text_service_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_shaped_text_service_tests COMMAND jellyframe_shaped_text_service_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/fixtures")
add_executable(jellyframe_shaping_binding_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/shaping_binding_tests.cpp")
target_link_libraries(jellyframe_shaping_binding_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_shaping_binding_tests COMMAND jellyframe_shaping_binding_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/fixtures")
if(JELLYFRAME_ENABLE_TEXT_SHAPING)
    add_executable(jellyframe_shaped_text_binding_api_tests
        "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/shaped_text_binding_api_tests.cpp")
    target_link_libraries(jellyframe_shaped_text_binding_api_tests PRIVATE jellyframe_render_core)
    target_include_directories(jellyframe_shaped_text_binding_api_tests PRIVATE
        "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/src")
    add_test(NAME jellyframe_shaped_text_binding_api_tests
        COMMAND jellyframe_shaped_text_binding_api_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/fixtures")
    add_test(NAME jellyframe_installed_shaped_text_binding_consumer
        COMMAND ${CMAKE_COMMAND}
            "-DCORE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DCORE_SOURCE_ROOT=${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}"
            "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
            "-DTEST_CONFIGURATION=${CMAKE_BUILD_TYPE}"
            -P "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/package/shaped_text_binding_consumer/run_installed.cmake")
endif()
add_executable(jellyframe_shaped_text_packet_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/shaped_text_packet_tests.cpp")
if(JELLYFRAME_ENABLE_CONTEXTUAL_TEXT)
    set(_contextual_tests contextual_text_paragraph)
    if(JELLYFRAME_ENABLE_SHAPED_PACKET_V2)
        list(APPEND _contextual_tests contextual_text_paint)
    endif()
    if(JELLYFRAME_ENABLE_CONTEXTUAL_LAYOUT)
        list(APPEND _contextual_tests contextual_text_layout)
    endif()
    foreach(_test IN LISTS _contextual_tests)
        add_executable(jellyframe_${_test}_tests
            "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/${_test}_tests.cpp")
        target_link_libraries(jellyframe_${_test}_tests PRIVATE jellyframe_render_core)
        target_include_directories(jellyframe_${_test}_tests PRIVATE "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/src")
        if(MSVC)
            target_compile_options(jellyframe_${_test}_tests PRIVATE /UNDEBUG /utf-8)
        else()
            target_compile_options(jellyframe_${_test}_tests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME jellyframe_${_test}_tests COMMAND jellyframe_${_test}_tests
            "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/fixtures")
    endforeach()
    unset(_contextual_tests)
endif()
target_link_libraries(jellyframe_shaped_text_packet_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_shaped_text_packet_tests COMMAND jellyframe_shaped_text_packet_tests)
if(JELLYFRAME_ENABLE_SHAPED_PACKET_V2)
    add_executable(jellyframe_shaped_packet_v2_tests
        "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/shaped_packet_v2_tests.cpp")
    target_link_libraries(jellyframe_shaped_packet_v2_tests PRIVATE jellyframe_render_core)
    add_test(NAME jellyframe_shaped_packet_v2_tests COMMAND jellyframe_shaped_packet_v2_tests)
endif()
if(JELLYFRAME_INSTALL_RENDER_CORE)
    add_test(NAME jellyframe_shaped_packet_v2_installed_consumer
        COMMAND ${CMAKE_COMMAND}
            "-DCORE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DCORE_SOURCE_ROOT=${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}"
            "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
            "-DTEST_CONFIGURATION=$<CONFIG>"
            "-DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DEXPECT_CODEC=${JELLYFRAME_RENDER_CORE_SHAPED_PACKET_V2_ENABLED}"
            "-DEXPECT_TEXT=${JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED}"
            "-DEXPECT_LAYOUT=${JELLYFRAME_RENDER_CORE_CONTEXTUAL_LAYOUT_ENABLED}"
            -P "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/package/shaped_packet_v2_consumer/run_installed.cmake")
endif()
add_executable(jellyframe_shaped_text_painter_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/shaped_text_painter_tests.cpp")
target_link_libraries(jellyframe_shaped_text_painter_tests PRIVATE jellyframe_render_core)
target_include_directories(jellyframe_shaped_text_painter_tests PRIVATE "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/src")
add_test(NAME jellyframe_shaped_text_painter_tests COMMAND jellyframe_shaped_text_painter_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/fixtures")
add_executable(jellyframe_shared_inline_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/shared_inline_tests.cpp")
target_link_libraries(jellyframe_shared_inline_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_shared_inline_tests COMMAND jellyframe_shared_inline_tests)
add_executable(jellyframe_shared_inline_planner_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/shared_inline_planner_tests.cpp")
target_link_libraries(jellyframe_shared_inline_planner_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_shared_inline_planner_tests COMMAND jellyframe_shared_inline_planner_tests)
add_executable(jellyframe_core_text_cluster_map_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/core_text_cluster_map_tests.cpp")
target_link_libraries(jellyframe_core_text_cluster_map_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_core_text_cluster_map_tests COMMAND jellyframe_core_text_cluster_map_tests)
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_core_text_cluster_map_tests)
endif()
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_shared_inline_planner_tests)
endif()
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_shared_inline_tests)
endif()
add_executable(jellyframe_scalar_cascade_cache_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/scalar_cascade_cache_tests.cpp")
target_link_libraries(jellyframe_scalar_cascade_cache_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_scalar_cascade_cache_tests COMMAND jellyframe_scalar_cascade_cache_tests)
add_executable(jellyframe_inset_position_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/inset_position_tests.cpp")
target_link_libraries(jellyframe_inset_position_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_inset_position_tests COMMAND jellyframe_inset_position_tests)
add_executable(jellyframe_layout_mutation_journal_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/layout_mutation_journal_tests.cpp")
target_link_libraries(jellyframe_layout_mutation_journal_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_layout_mutation_journal_tests COMMAND jellyframe_layout_mutation_journal_tests)
add_executable(jellyframe_dom_mutation_observer_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/dom_mutation_observer_tests.cpp")
target_link_libraries(jellyframe_dom_mutation_observer_tests PRIVATE jellyframe_render_core)
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_dom_mutation_observer_tests)
endif()
add_test(NAME jellyframe_dom_mutation_observer_tests COMMAND jellyframe_dom_mutation_observer_tests)
add_executable(jellyframe_flex_auto_min_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/flex_auto_min_tests.cpp")
target_link_libraries(jellyframe_flex_auto_min_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_flex_auto_min_tests COMMAND jellyframe_flex_auto_min_tests)
add_executable(jellyframe_flex_basis_percentage_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/flex_basis_percentage_tests.cpp")
target_link_libraries(jellyframe_flex_basis_percentage_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_flex_basis_percentage_tests COMMAND jellyframe_flex_basis_percentage_tests)
# Shared Render Core regression-test registration.
if(JELLYFRAME_ENABLE_FLEX_GRID)
    add_executable(jellyframe_computed_flex_layout_tests
        "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/computed_flex_layout_tests.cpp")
    target_link_libraries(jellyframe_computed_flex_layout_tests PRIVATE jellyframe_render_core)
    add_test(NAME jellyframe_computed_flex_layout_tests COMMAND jellyframe_computed_flex_layout_tests)
endif()
add_executable(jellyframe_computed_block_layout_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/computed_block_layout_tests.cpp")
target_link_libraries(jellyframe_computed_block_layout_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_computed_block_layout_tests COMMAND jellyframe_computed_block_layout_tests)
add_executable(jellyframe_natural_image_layout_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/natural_image_layout_tests.cpp")
target_link_libraries(jellyframe_natural_image_layout_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_natural_image_layout_tests COMMAND jellyframe_natural_image_layout_tests)
add_executable(jellyframe_mixed_dimension_consumer_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/mixed_dimension_consumer_tests.cpp")
target_link_libraries(jellyframe_mixed_dimension_consumer_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_mixed_dimension_consumer_tests COMMAND jellyframe_mixed_dimension_consumer_tests)
add_executable(jellyframe_c5_mixed_rollback_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/c5_mixed_rollback_tests.cpp")
target_link_libraries(jellyframe_c5_mixed_rollback_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_c5_mixed_rollback_tests COMMAND jellyframe_c5_mixed_rollback_tests)
add_executable(jellyframe_c5_mixed_dimension_storage_oracle_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/c5_mixed_dimension_storage_oracle_tests.cpp")
target_link_libraries(jellyframe_c5_mixed_dimension_storage_oracle_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_c5_mixed_dimension_storage_oracle_tests
    COMMAND jellyframe_c5_mixed_dimension_storage_oracle_tests)
add_executable(jellyframe_computed_size_storage_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/computed_size_storage_tests.cpp")
target_link_libraries(jellyframe_computed_size_storage_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_computed_size_storage_tests COMMAND jellyframe_computed_size_storage_tests)
add_executable(jellyframe_aspect_ratio_literal_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/aspect_ratio_literal_tests.cpp")
target_link_libraries(jellyframe_aspect_ratio_literal_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_aspect_ratio_literal_tests COMMAND jellyframe_aspect_ratio_literal_tests)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME jellyframe_source_hash_private_headers
        COMMAND "${Python3_EXECUTABLE}" "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/tools/source_hash_private_header_tests.py")
    add_test(NAME jellyframe_source_hash_build_refresh
        COMMAND "${Python3_EXECUTABLE}" "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/tools/source_hash_build_refresh_tests.py"
            --cmake "${CMAKE_COMMAND}" --generator "${CMAKE_GENERATOR}")
    set_tests_properties(jellyframe_source_hash_build_refresh PROPERTIES TIMEOUT 45)
endif()

set(JELLYFRAME_RENDER_CORE_TEST_SOURCES
    tests/unit/tokenizer_tests.cpp
    tests/unit/arena_tests.cpp
    tests/unit/animation_timeline_tests.cpp
    tests/unit/budget_stress_tests.cpp
    tests/unit/dom_mutation_tests.cpp
    tests/unit/dom_owner_tests.cpp
    tests/unit/document_script_tests.cpp
    tests/unit/display_invalidation_tests.cpp
    tests/unit/dirty_region_tests.cpp
    tests/unit/embedded_framebuffer_tests.cpp
    tests/unit/css_parser_tests.cpp
    tests/unit/event_tests.cpp
    tests/unit/form_submission_tests.cpp
    tests/unit/frame_loop_tests.cpp
    tests/unit/frame_update_tests.cpp
    tests/unit/hit_test_tests.cpp
    tests/unit/host_tests.cpp
    tests/unit/input_tests.cpp
    tests/unit/layout_tests.cpp
    tests/unit/pipeline_statistics_tests.cpp
    tests/unit/render_tree_tests.cpp
    tests/unit/layer_tree_tests.cpp
    tests/unit/scroll_blit_tests.cpp
    tests/unit/scroll_gesture_tests.cpp
    tests/unit/software_renderer_tests.cpp
    tests/unit/style_repaint_tests.cpp
    tests/unit/text_repaint_tests.cpp
    tests/unit/text_scan_tests.cpp
)
if(JELLYFRAME_ENABLE_CANVAS2D)
    list(APPEND JELLYFRAME_RENDER_CORE_TEST_SOURCES
        tests/unit/canvas2d_tests.cpp)
else()
    list(APPEND JELLYFRAME_RENDER_CORE_TEST_SOURCES
        tests/unit/canvas2d_disabled_tests.cpp)
endif()
if(JELLYFRAME_ENABLE_MODERN_PAINT)
    list(APPEND JELLYFRAME_RENDER_CORE_TEST_SOURCES
        tests/unit/modern_paint_tests.cpp)
endif()

foreach(test_source IN LISTS JELLYFRAME_RENDER_CORE_TEST_SOURCES)
    set(test_source_absolute "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/${test_source}")
    get_filename_component(test_name "${test_source_absolute}" NAME_WE)
    set_source_files_properties("${test_source_absolute}"
        PROPERTIES COMPILE_DEFINITIONS "main=${test_name}_main")
    list(APPEND JELLYFRAME_RENDER_CORE_TEST_SOURCES_ABSOLUTE
        "${test_source_absolute}")
endforeach()

add_executable(jellyframe_render_core_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/render_core_tests.cpp"
    ${JELLYFRAME_RENDER_CORE_TEST_SOURCES_ABSOLUTE})
target_link_libraries(jellyframe_render_core_tests PRIVATE jellyframe_render_core)
target_compile_definitions(jellyframe_render_core_tests PRIVATE JELLYFRAME_TEST_ASSERTS_ENABLED=1)
target_compile_definitions(jellyframe_render_core_tests
    PRIVATE $<$<CONFIG:Debug>:JELLYFRAME_TEST_CONFIG_DEBUG=1>)
if(MSVC)
    target_compile_options(jellyframe_render_core_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(jellyframe_render_core_tests PRIVATE -UNDEBUG)
endif()
if(COMMAND jellyframe_enable_render_core_link_map)
    jellyframe_enable_render_core_link_map(jellyframe_render_core_tests)
endif()
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_render_core_tests)
endif()
add_test(
    NAME jellyframe_render_core_tests
    COMMAND jellyframe_render_core_tests
    WORKING_DIRECTORY "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}")

add_executable(jellyframe_font_resource_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/font_resource_tests.cpp")
target_link_libraries(jellyframe_font_resource_tests PRIVATE jellyframe_render_core)
if(MSVC)
    target_compile_options(jellyframe_font_resource_tests PRIVATE /UNDEBUG /utf-8)
else()
    target_compile_options(jellyframe_font_resource_tests PRIVATE -UNDEBUG)
endif()
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_font_resource_tests)
endif()
add_test(NAME jellyframe_font_resource_tests COMMAND jellyframe_font_resource_tests)

add_executable(jellyframe_text_system_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/text_system_tests.cpp")
target_link_libraries(jellyframe_text_system_tests PRIVATE jellyframe_render_core)
if(MSVC)
    target_compile_options(jellyframe_text_system_tests PRIVATE /UNDEBUG /utf-8)
else()
    target_compile_options(jellyframe_text_system_tests PRIVATE -UNDEBUG)
endif()
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_text_system_tests)
endif()
add_test(NAME jellyframe_text_system_tests COMMAND jellyframe_text_system_tests)

add_executable(jellyframe_rounded_coverage_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/rounded_coverage_tests.cpp")
target_link_libraries(jellyframe_rounded_coverage_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_rounded_coverage_tests COMMAND jellyframe_rounded_coverage_tests)
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_rounded_coverage_tests)
endif()

foreach(geometry IN ITEMS image repeat gradient)
    set(geometry_target "jellyframe_background_${geometry}_geometry_tests")
    add_executable(${geometry_target}
        "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/background_${geometry}_geometry_tests.cpp")
    target_link_libraries(${geometry_target} PRIVATE jellyframe_render_core)
    add_test(NAME ${geometry_target} COMMAND ${geometry_target})
    if(COMMAND jellyframe_stage_sanitizer_runtime)
        jellyframe_stage_sanitizer_runtime(${geometry_target})
    endif()
endforeach()

add_executable(jellyframe_typed_display_command_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/typed_display_command_tests.cpp")
target_link_libraries(jellyframe_typed_display_command_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_typed_display_command_tests COMMAND jellyframe_typed_display_command_tests)
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_typed_display_command_tests)
endif()

add_executable(jellyframe_image_callback_contract_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/image_callback_contract_tests.cpp")
target_link_libraries(jellyframe_image_callback_contract_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_image_callback_contract_tests COMMAND jellyframe_image_callback_contract_tests)
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_image_callback_contract_tests)
endif()

add_executable(jellyframe_background_multilayer_paint_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/background_multilayer_paint_tests.cpp")
target_link_libraries(jellyframe_background_multilayer_paint_tests PRIVATE jellyframe_render_core)
add_test(NAME jellyframe_background_multilayer_paint_tests COMMAND jellyframe_background_multilayer_paint_tests)
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_background_multilayer_paint_tests)
endif()

add_executable(jellyframe_background_multilayer_style_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/background_multilayer_style_tests.cpp")
target_link_libraries(jellyframe_background_multilayer_style_tests PRIVATE jellyframe_render_core)
target_include_directories(jellyframe_background_multilayer_style_tests PRIVATE "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/src")
add_test(NAME jellyframe_background_multilayer_style_tests COMMAND jellyframe_background_multilayer_style_tests)
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_background_multilayer_style_tests)
endif()

# Standalone source queries remain tested in every feature profile.
foreach(query intrinsic_source_query intrinsic_source_query_height wrapped_row_flex)
    add_executable(jellyframe_${query}_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/${query}_tests.cpp")
    target_link_libraries(jellyframe_${query}_tests PRIVATE jellyframe_render_core)
    if(MSVC)
        target_compile_options(jellyframe_${query}_tests PRIVATE /UNDEBUG /utf-8)
    else()
        target_compile_options(jellyframe_${query}_tests PRIVATE -UNDEBUG)
    endif()
    if(COMMAND jellyframe_stage_sanitizer_runtime)
        jellyframe_stage_sanitizer_runtime(jellyframe_${query}_tests)
    endif()
    add_test(NAME jellyframe_${query}_tests COMMAND jellyframe_${query}_tests)
endforeach()

add_executable(shaping_lookup_executor_tests "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/shaping_lookup_executor_tests.cpp")
target_link_libraries(shaping_lookup_executor_tests PRIVATE jellyframe_render_core)
add_test(NAME shaping_lookup_executor_tests COMMAND shaping_lookup_executor_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/fixtures/shaping_resource"
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/fixtures/shaping_executor"
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/fixtures/shaping_executor_catalog")

# Keep failed CTest processes noninteractive on MSVC, with diagnostics on stderr.
# This source belongs to test executables only, never the product library.
if(WIN32)
    get_property(_jellyframe_targets_after_tests DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_jellyframe_test_target IN LISTS _jellyframe_targets_after_tests)
        if(_jellyframe_test_target IN_LIST _jellyframe_targets_before_tests)
            continue()
        endif()
        get_target_property(_jellyframe_test_target_type ${_jellyframe_test_target} TYPE)
        if(_jellyframe_test_target_type STREQUAL "EXECUTABLE")
            target_sources(${_jellyframe_test_target} PRIVATE
                "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/tests/unit/test_crt_diagnostics.cpp")
        endif()
    endforeach()
    unset(_jellyframe_targets_before_tests)
    unset(_jellyframe_targets_after_tests)
    unset(_jellyframe_test_target)
    unset(_jellyframe_test_target_type)
endif()
