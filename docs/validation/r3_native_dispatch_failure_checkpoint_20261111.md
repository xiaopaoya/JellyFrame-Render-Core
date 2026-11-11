# Native dispatch failure prerequisite

Candidate extends the R3 lifecycle branch after `5167177e`. Exact Core source:
`aa62c7ab1f70bb45c91fb754813666be90238902b19e29451cb73a387243b321`/145,
package `0.9.0-dev`, ABI4. This is a local candidate, not a production adoption.

## Contract

`fail_event_dispatch(Event&) noexcept` returns true only while that Event has
an active, non-closing dispatch. It records a native infrastructure failure
and stops remaining listeners/propagation. `dispatch_event` returns false for
this failure even when the Event is noncancelable. `Event::dispatch_failed()`
distinguishes this result from `default_prevented()`; native failure does not
set default prevention. The snapshot remains after completion and is reset by
the next admitted dispatch. Closing/completed/inactive failure requests are
refused. No heap allocation is added by this API.

Earlier callbacks and their side effects are not rolled back. This mechanism
supports terminating dispatch after a binding resource failure; it is not a
transaction around all callbacks. Runtime must still define wrapper admission,
record limits, engine-fatal OOM behavior and owner lifetime.

Once-listener exception cleanup now uses a local unwind guard. If both the
listener and its cleanup throw, the original listener exception propagates;
if only normal cleanup throws, that exception propagates. Cleanup runs once,
the listener is removed, transient state is cleared, and subsequent dispatch
remains admitted.

## Verification

- Clang22 x64 Debug and Release:24/24 each, active build trees
  `E:/Builds/Core-r3-integrated-{debug,release}`.
- MSVC Win32 Debug:24/24, `E:/Builds/r3-core-win32`.
- Clang22 ASan RelWithDebInfo core test executable:1/1, including Event,
  destruction, exception, observer, layout and paint unit groups;
  `E:/Builds/r3-dispatch-failure-asan-20261111`. `detect_leaks=0`; no leak
  sanitizer claim and no claim that the other23 CTest executables ran under ASan.
- Xtensa GCC13.2 static assertions pass for `sizeof(Node)==136` and
  `sizeof(Event)==48`; source `E:/Temp/r3-native-failure-target-size-20261111.cpp`.
  This is a header/layout check, not a refreshed full firmware resource comparison.
- Failure regression covers all four listener phases with cancelable and
  noncancelable Events, later listener suppression, distinct nested Event
  isolation, immutable completion snapshot, subsequent reset and target
  destruction. Exception regression covers throwing cleanup and original
  exception preservation.

The first Windows Clang ASan run crashed in the prior nested catch/rethrow
shape. A standalone Event completion case reproduced it; replacing explicit
rethrow cleanup with the unwind guard passes the same instrumented Core suite.
The specific compiler/runtime root cause is not established. Old-path source,
executable and failure log are retained under
`E:/Experiments/r3-asan-exception-repro`. A simpler independent nested-rethrow
program passes, so this is not evidence that nested rethrows fail generally.

The earlier non-script firmware pair in the static resource checkpoint uses
source `1f85f2a3...`, not this updated source. Its size and runtime limitations
must not be transferred to this candidate. Script Runtime consumer qualification
is a separate paired gate.
