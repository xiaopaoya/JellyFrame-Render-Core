# SubmitEvent kind portability checkpoint

Date 2026-11-11. Base is Core 272ba1d67567c64080fb27847d797472138c257a. Production source is 98f89f0fd300e631e701b301023e1510026145efc50eb3c76845843eb1c34086 across 146 inputs, Core 0.9.0-dev / ABI4.

EventKind appends Submit after Generic, Mouse and Wheel, preserving their numeric values 0, 1 and 2. JELLYFRAME_EVENT_SUBMIT_KIND_API=1 lets consumers discover this API. SubmitEvent overrides the existing Event::kind virtual in full and forms-disabled implementations. The discriminator permits consumers to identify native submit events under RTTI-disabled builds. An Event constructed with type "submit" retains Generic kind.

The form regression checks direct construction through an Event reference in both profiles, stable existing enum values, and actual request_form_submit dispatch with submitter and cancellation in the advanced forms profile. Its former dynamic_cast now uses a checked kind followed by static_cast.

Fresh Clang 22.1.8 Ninja Release builds under E:/Builds/r3-submit-kind-{full,cropped,asan,no-rtti}-20261111 were used. Full and cropped monolithic CTest each passed 1/1, with 0.73s and 0.62s total time. ASan full monolithic passed 1/1 in 1.37s. Logs are E:/Temp/r3-submit-kind-{full,cropped,asan}-ctest-20261111.log. Cropped disables advanced forms, Canvas2D, Flex/Grid, image file IO and modern paint. ASan adds -fsanitize=address -fno-omit-frame-pointer and the matching address sanitizer linker flag.

The complete Core library compiled with CMAKE_CXX_FLAGS=-fno-rtti. The monolithic test build encountered the existing WheelEvent dynamic_cast in tests/unit/input_tests.cpp:345. Focused tests/unit/form_submission_tests.cpp and tests/unit/event_tests.cpp each compiled with -std=c++17 -fno-rtti against that RTTI-disabled library and both executed successfully. They use -D_DLL -D_MT -Xclang --dependent-lib=msvcrt for the matching Windows CRT. Executables are form_submission_no_rtti.exe and event_no_rtti.exe in the no-rtti build directory. This gate covers native submit dispatch through the normal form API.

A matched x64 Clang sizeof probe against base headers and candidate headers reports Event72 bytes and SubmitEvent80 bytes for both. The change introduces an override of an existing virtual slot and adds no fields. Target compiler and complete Runtime integration qualification are handled separately.

An initial configure command incorrectly quoted the compiler path containing a space. Correcting the compiler argument allowed all fresh configurations to complete. Source diffs pass git diff --check.
