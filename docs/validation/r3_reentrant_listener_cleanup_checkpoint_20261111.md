# Reentrant listener cleanup follow-up

Extends local native-dispatch failure commit `a47cb145`. Final Core source
SHA-256 `4ace6b2b02da38dcab3d14bd804bf8bc42798ca6b9f8a16cdcdc2689dcd12b77`/145,
`0.9.0-dev`, ABI4. These changes remain local.

Independent review reproduced an existing heap use-after-free in
`ListenerStore::mark_removed`: a once-listener cleanup hook added32 listeners
of the same type, reallocating the vector, then removal wrote cleared hook
fields into the old entry. ASan reproduction and raw log are
`E:/Temp/r3-once-cleanup-reentrant-review.{cpp,log}`. This defect predates the
native-failure API; the earlier tests did not exercise reentrant vector growth.

Removal now copies the hook/context and clears its registration before calling
the hook, with no later access to that entry. Target destruction closes its
listener store before draining hooks, so new listener admission returns0 and
callback invocation is skipped while closing. Peer removal remains legal and
each hook runs once. The flag belongs to the heap listener store; Node/Event
layouts and the native-failure API are unchanged.

Regression coverage includes32 new callbacks allocated by a once cleanup,
their exclusion from the active listener snapshot and their execution on the
next dispatch. Teardown tests attempt same-type and new-type admission and
recursive peer removal without invalidating drain iterators. Prior exception,
failure, target destruction and dispatch completion regressions still execute.

Final validation at the source above:

- Clang22 x64 Debug24/24, Release24/24 and MSVC Win32 Debug24/24 pass in
  `E:/Builds/Core-r3-integrated-{debug,release}` and `E:/Builds/r3-core-win32`.
- ASan instrumented core test executable1/1 passes in
  `E:/Builds/r3-dispatch-failure-asan-20261111`, with leak detection disabled.
  Other standalone CTest executables were not built/run under this ASan gate.
- Exact installed x64 Release package consumer1/1 passes in
  `E:/Builds/r3-native-failure-reviewed-consumer-20261111` against
  `E:/Builds/r3-native-failure-reviewed-install-20261111`.
  Its CMake configure checks package version, ABI and exact source hash; it
  executes noncancelable native failure, completion observation, later-listener
  suppression and normal redispatch. It does not qualify a scripted Runtime.

The prior non-script linked-image pair uses an older Core source and is not a
target resource measurement of this update. Scripted Runtime wrapper ownership,
engine-fatal OOM and host physical teardown remain separate acceptance work.
