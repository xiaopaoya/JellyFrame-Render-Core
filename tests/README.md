# Tests

`unit/` contains the C++ regression target `jellyframe_render_core_tests`. It
is platform-neutral and must not acquire App Runtime, JavaScript, filesystem,
network, RTOS or board dependencies.

`render_core_source_archive_tests.py` validates the release-source artifact:
deterministic packing, line-ending stability, tracked-input isolation, clean
extraction, standalone build, CTest and package installation.
