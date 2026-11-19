cmake_minimum_required(VERSION 3.16)
project(ShapedPacketV2Consumer LANGUAGES CXX)

# Exercise incompatible minor versions without EXACT to verify the export's
# version-selection policy.
foreach(incompatible_version IN ITEMS 0.9.0 0.10.0 0.10.1)
    find_package(JellyFrameRenderCore ${incompatible_version} CONFIG QUIET
        PATHS "${CMAKE_PREFIX_PATH}" NO_DEFAULT_PATH)
    if(JellyFrameRenderCore_FOUND)
        message(FATAL_ERROR "Core ${EXPECT_CORE_VERSION} accepted an incompatible request for ${incompatible_version}")
    endif()
endforeach()
find_package(JellyFrameRenderCore ${EXPECT_CORE_VERSION} EXACT CONFIG REQUIRED
    PATHS "${CMAKE_PREFIX_PATH}" NO_DEFAULT_PATH)
if(NOT JellyFrameRenderCore_ENGINE_ABI STREQUAL EXPECT_CORE_ABI OR
   NOT JellyFrameRenderCore_PACKAGE_VERSION STREQUAL EXPECT_CORE_VERSION)
    message(FATAL_ERROR "Expected the Core ${EXPECT_CORE_VERSION} / ABI${EXPECT_CORE_ABI} producer identity")
endif()
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.19")
    file(READ "${JellyFrameRenderCore_PROFILE_FILE}" profile)
    string(JSON abi GET "${profile}" engineAbi)
    string(JSON version GET "${profile}" packageVersion)
    if(NOT abi STREQUAL JellyFrameRenderCore_ENGINE_ABI OR
       NOT version STREQUAL JellyFrameRenderCore_PACKAGE_VERSION)
        message(FATAL_ERROR "Installed profile disagrees with package identity")
    endif()
    string(JSON count LENGTH "${profile}" features)
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
        string(JSON feature GET "${profile}" features ${index})
        list(APPEND features "${feature}")
    endforeach()
    foreach(pair IN ITEMS "text.contextual-jfsp2|${EXPECT_TEXT}" "text.contextual-layout|${EXPECT_LAYOUT}" "text.shaping|0")
        string(REPLACE "|" ";" fields "${pair}")
        list(GET fields 0 feature)
        list(GET fields 1 enabled)
        if((enabled AND NOT feature IN_LIST features) OR (NOT enabled AND feature IN_LIST features))
            message(FATAL_ERROR "Unexpected installed profile feature: ${feature}")
        endif()
    endforeach()
endif()
add_executable(shaped_packet_v2_consumer main.cpp)
target_link_libraries(shaped_packet_v2_consumer PRIVATE JellyFrame::jellyframe_render_core)
target_compile_definitions(shaped_packet_v2_consumer PRIVATE
    EXPECT_CODEC=${EXPECT_CODEC} EXPECT_TEXT=${EXPECT_TEXT} EXPECT_LAYOUT=${EXPECT_LAYOUT})

enable_testing()
add_test(NAME installed_shaped_packet_v2_contract COMMAND shaped_packet_v2_consumer)
