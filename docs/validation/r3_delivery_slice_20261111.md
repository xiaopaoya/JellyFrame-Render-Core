# R3 native lifecycle and Submit delivery slice

This independently replayed slice starts at Core master
`b951751a23901e6e89560c5954b6b143be48db8c`. It contains no C5 layout,
mixed-dimension or overflow implementation changes. It does not change Runtime
production locks or select Runtime adoption of these APIs.

## Implementation provenance

| Source commit | Replayed commit | Contract |
| --- | --- | --- |
| 05095ea4 | ec2d7596 | Node protected lifetime notification and Event dispatch completion |
| a47cb145 | 7749fb29 | Noncancelable native dispatch failure and once-listener unwind cleanup |
| 83e3381b | 93499b55 | Detach listener cleanup before invoking reentrant hooks |
| 15169c02 | d8d85cbf | RTTI-free SubmitEvent kind, including forms-disabled implementation |

All four cherry-picks applied without conflict. The existing two-file installed
failure consumer is extracted unchanged from `c941f69a`; none of that joined
commit's layout changes are included. The new installed Submit consumer checks
the discriminator and submitter in both profiles, plus real form dispatch and
cancellation when advanced forms are enabled. Full/cropped CI now runs completion,
failure and Submit installed consumers with `-fno-rtti`, checking exact installed
source/profile identities. These gates extend the existing installed-package job.

`9cbf1e97` is omitted: its framebuffer size expectations fix 32-bit test
portability; this slice's fresh Clang x64 full/cropped suites pass without it.
No fresh Win32 qualification is claimed.

## Exact package identity

- Package: `0.9.0-dev`, engine ABI `4`.
- Source hash: `fdf9a7b0e04da9dc730642db078c6bca6f3143ef684e33f6175d15f854cf57f8`.
- Source inputs: `145`.
- Full and no-RTTI profile: `render-core-default`.
- Cropped profile: `render-core-minimal-no-forms-advanced`.

The source manifest hashes implementation, public/private headers and package
CMake support; its identity is distinct from the Git revision and excludes these
documentation, workflow and package-consumer changes. All three fresh build and
installed manifests agree on the source hash and input count.

## Fresh local qualification

Windows x64 Clang `22.1.8`, Ninja, Release, CMake default ABI4. All builds are
under `E:/Builds/core-r3-delivery-*-20261111` and installation prefixes are
`E:/Builds/core-r3-delivery-install-{full,cropped,no-rtti}-20261111`.

| Configuration | Result |
| --- | --- |
| Full Core CTest | 24/24, 11.43 seconds |
| Cropped Core CTest | 23/23, 11.24 seconds |
| Full installed completion/failure/Submit consumers, each `-fno-rtti` | 1/1 each |
| Cropped installed completion/failure/Submit consumers, each `-fno-rtti` | 1/1 each |
| Entire full Core library compiled with `-fno-rtti`; same three installed consumers | 1/1 each |

Cropped disables Canvas2D, modern paint, Flex/Grid and advanced forms; image file
IO remains enabled, matching the existing full/cropped CI package matrix. The
full/cropped monolithic test executable includes event, form and DOM mutation
groups, including dispatch failure, completion, destruction, listener exceptions
and reentrant cleanup. The no-RTTI library build disables Core's aggregate tests
because unrelated existing input tests still use RTTI; the installed tests
exercise actual completion, failure and full-profile native Submit dispatch.

Reproduction runner and individual configure/build/CTest/install logs, plus
manifest/profile copies, live at
`E:/Experiments/core-r3-delivery-20261111/verify.ps1` and that directory's logs.
Fresh Core configure commands select
`-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang++.exe`,
`-DCMAKE_BUILD_TYPE=Release`, `-DJELLYFRAME_BUILD_TESTS=ON`,
`-DJELLYFRAME_INSTALL_RENDER_CORE=ON`; the separate no-RTTI build uses tests OFF
and `-DCMAKE_CXX_FLAGS=-fno-rtti`. `git diff --check` passes.

## Evidence boundaries

The three replayed historical checkpoint documents retain their original package
identities and observations. Their old target resource numbers, firmware results
and paired Runtime results do not qualify this new slice. This document records
only the fresh checks above. No ASan, Win32, device execution, complete Runtime
integration or remote Actions result is claimed here. A local YAML-parser check
could not run because PyYAML is not installed; the workflow was reviewed as a
diff, and its commands were exercised locally through the package consumers.

No remote refs were updated and no PR was created. Remote CI and paired Runtime
opt-in/legacy compatibility qualification remain delivery follow-ups.
