# Public version metadata for the standalone Render Core boundary.
# Keep this file in the Render Core source checkout so a Runtime source override
# carries its own package identity instead of inheriting the Runtime version.

# Identity describes the source, not a configurable build profile. Keep these
# literal declarations readable by the standalone source archive tooling.
function(_jellyframe_load_core_source_identity)
    set(fields JELLYFRAME_RENDER_CORE_PACKAGE_VERSION
        JELLYFRAME_RENDER_CORE_CMAKE_VERSION JELLYFRAME_RENDER_CORE_ENGINE_ABI)
    foreach(field IN LISTS fields)
        if(DEFINED ${field})
            set(previous_${field} "${${field}}")
        endif()
    endforeach()
    set(JELLYFRAME_RENDER_CORE_PACKAGE_VERSION "0.13.0")
    set(JELLYFRAME_RENDER_CORE_CMAKE_VERSION "0.13.0")
    set(JELLYFRAME_RENDER_CORE_ENGINE_ABI "11")
    foreach(field IN LISTS fields)
        if(DEFINED previous_${field} AND NOT "${previous_${field}}" STREQUAL "${${field}}")
            message(FATAL_ERROR "${field} must match the Core source identity (${${field}}); use a fresh build directory")
        endif()
        set(${field} "${${field}}" PARENT_SCOPE)
    endforeach()
endfunction()
_jellyframe_load_core_source_identity()
