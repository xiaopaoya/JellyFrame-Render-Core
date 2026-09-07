# Shared Render Core source manifest.
#
# Consumers set JELLYFRAME_RENDER_CORE_SOURCE_ROOT to the Render Core checkout
# before including this file. Keep feature-family boundaries here; do not
# duplicate this list in a port or tool-specific build script.

if(NOT DEFINED JELLYFRAME_RENDER_CORE_SOURCE_ROOT)
    get_filename_component(JELLYFRAME_RENDER_CORE_SOURCE_ROOT
        "${CMAKE_CURRENT_LIST_DIR}/.."
        ABSOLUTE)
endif()

set(JELLYFRAME_RENDER_CORE_CORE_DOCUMENT_SOURCES
    src/arena.cpp
    src/css_parser.cpp
    src/document_script.cpp
    src/document_style.cpp
    src/dom.cpp
    src/dom_owner.cpp
    src/event.cpp
    src/form_control.cpp
    src/html_parser.cpp
    src/html_tokenizer.cpp
    src/html_tree_builder.cpp
    src/input.cpp
    src/text_normalization.cpp
    src/text_scan.cpp
)

set(JELLYFRAME_RENDER_CORE_CORE_PAINT_SOURCES
    src/animation_invalidation.cpp
    src/animation_timeline.cpp
    src/bitmap_font.cpp
    src/bitmap_font_resource.cpp
    src/display_invalidation.cpp
    src/dirty_region.cpp
    src/embedded_framebuffer.cpp
    src/frame_loop.cpp
    src/frame_update.cpp
    src/hit_test.cpp
    src/layer_tree.cpp
    src/layout.cpp
    src/pipeline_statistics.cpp
    src/render_tree.cpp
    src/scroll_blit.cpp
    src/software_renderer.cpp
    src/style.cpp
    src/style_repaint.cpp
    src/text_adapter.cpp
    src/text_backend.cpp
    src/text_layout_reuse.cpp
    src/text_repaint.cpp
)

# These two families are mandatory, but keeping their source ownership explicit
# prevents the baseline library from becoming another unreviewable catch-all.
# Optional families below may depend on either boundary but must not append
# sources directly to this list.
set(JELLYFRAME_RENDER_CORE_BASE_SOURCES
    ${JELLYFRAME_RENDER_CORE_CORE_DOCUMENT_SOURCES}
    ${JELLYFRAME_RENDER_CORE_CORE_PAINT_SOURCES}
)

set(JELLYFRAME_RENDER_CORE_GRAPHICS_CANVAS2D_SOURCES
    src/canvas2d.cpp
)

set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_SOURCES
    src/modern_paint.cpp
)

set(JELLYFRAME_RENDER_CORE_FLEX_GRID_SOURCES
    src/flex_grid_paint.cpp
)

set(JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_SOURCES
    src/form_submission.cpp
)

if(NOT DEFINED JELLYFRAME_ENABLE_CANVAS2D)
    set(JELLYFRAME_ENABLE_CANVAS2D ON)
endif()
if(NOT DEFINED JELLYFRAME_ENABLE_MODERN_PAINT)
    set(JELLYFRAME_ENABLE_MODERN_PAINT ON)
endif()
if(NOT DEFINED JELLYFRAME_ENABLE_FLEX_GRID)
    set(JELLYFRAME_ENABLE_FLEX_GRID ON)
endif()
if(NOT DEFINED JELLYFRAME_ENABLE_ADVANCED_FORMS)
    set(JELLYFRAME_ENABLE_ADVANCED_FORMS ON)
endif()
if(JELLYFRAME_ENABLE_FLEX_GRID)
    set(JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED 1)
else()
    set(JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED 0)
endif()
if(JELLYFRAME_ENABLE_ADVANCED_FORMS)
    set(JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED 1)
    set(JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_IMPLEMENTATION
        ${JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_SOURCES})
else()
    set(JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED 0)
    set(JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_IMPLEMENTATION
        src/form_submission_disabled.cpp)
endif()
if(JELLYFRAME_ENABLE_FLEX_GRID)
    set(JELLYFRAME_RENDER_CORE_FLEX_GRID_IMPLEMENTATION
        ${JELLYFRAME_RENDER_CORE_FLEX_GRID_SOURCES})
else()
    set(JELLYFRAME_RENDER_CORE_FLEX_GRID_IMPLEMENTATION)
endif()
if(JELLYFRAME_ENABLE_MODERN_PAINT)
    set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED 1)
    set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_IMPLEMENTATION
        ${JELLYFRAME_RENDER_CORE_MODERN_PAINT_SOURCES})
else()
    set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED 0)
    set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_IMPLEMENTATION)
endif()

# Keep feature-family source selection explicit. A family must have a safe
# compile-time fallback before it is removed from a profile.
if(JELLYFRAME_ENABLE_CANVAS2D)
    set(JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED 1)
    set(JELLYFRAME_RENDER_CORE_CANVAS2D_IMPLEMENTATION
        ${JELLYFRAME_RENDER_CORE_GRAPHICS_CANVAS2D_SOURCES})
else()
    set(JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED 0)
    set(JELLYFRAME_RENDER_CORE_CANVAS2D_IMPLEMENTATION
        src/canvas2d_disabled.cpp)
endif()

set(JELLYFRAME_RENDER_CORE_SOURCES
    ${JELLYFRAME_RENDER_CORE_BASE_SOURCES}
    ${JELLYFRAME_RENDER_CORE_FLEX_GRID_IMPLEMENTATION}
    ${JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_IMPLEMENTATION}
    ${JELLYFRAME_RENDER_CORE_MODERN_PAINT_IMPLEMENTATION}
    ${JELLYFRAME_RENDER_CORE_CANVAS2D_IMPLEMENTATION}
)

set(JELLYFRAME_RENDER_CORE_SOURCES_ABSOLUTE)
set(_jellyframe_seen_render_core_sources)
foreach(_jellyframe_source IN LISTS JELLYFRAME_RENDER_CORE_SOURCES)
    list(FIND _jellyframe_seen_render_core_sources
        "${_jellyframe_source}" _jellyframe_source_index)
    if(NOT _jellyframe_source_index EQUAL -1)
        message(FATAL_ERROR
            "Render Core source is listed more than once: ${_jellyframe_source}")
    endif()
    if(NOT EXISTS "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/${_jellyframe_source}")
        message(FATAL_ERROR
            "Render Core source does not exist: ${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/${_jellyframe_source}")
    endif()
    list(APPEND JELLYFRAME_RENDER_CORE_SOURCES_ABSOLUTE
        "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/${_jellyframe_source}")
    list(APPEND _jellyframe_seen_render_core_sources "${_jellyframe_source}")
endforeach()

unset(_jellyframe_source)
unset(_jellyframe_source_index)
unset(_jellyframe_seen_render_core_sources)
