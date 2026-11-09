# Literal ratio state port

Local development identity: Core 0.9.0-dev / ABI 4. Mainline remains 0.8.0 / ABI 3.
The new `Style::aspect_ratio_auto` fits within the existing Windows Release
776-byte guardrail. The ABI changes explicitly because clients must rebuild
against the new public definition, even when total holder size stays bounded.

The literal ratio parser and allocation-free identifier decoder are ported from
the experiment. A small scanner subset supplies number boundaries and comments;
conditional-rule and CSS math machinery are not imported. Declarations and
`@supports` share the literal parser. HTML ratios also use it, replacing the
pilot's separate arithmetic. Exact reduced ratios, auto in either order,
CSS-wide initial/unset/inherit, comments/escaped identifiers, zero components,
decimal/exponent cancellation and bounded atomic refusal follow the frozen
80-syntax/2660-number corpus. Explicit inheritance finalizes parent-first in the
render tree, including root fallback. Layout equality includes automatic-ratio
identity; ordinary non-replaced ratio geometry continues using its components.
Natural replaced-image preference is not implemented by storing this flag.

Core Release full and minimal each pass 5/5 CTest targets, including the frozen
literal matrix, shared declaration parser, targeted supports cases, inheritance
and layout-reuse refusal. The installed package consumer builds/runs using only
installed headers/library and verifies ABI 4, 16/9 auto and sizeof(Style)=776.
An input-hash regression now ensures private src/*.h and src/*.hpp affect source
identity; these headers were previously omitted from the package fingerprint.

`html-replay.json` compares all 61 HTML cases including the new automatic flag:
244/244 scalar-projected checks pass; the 18 size-precision differences remain
explicit. It does not qualify fractional size layout, CSS math expressions or
MCU resource costs. `validation.json` records source hashes and host outcomes.

Runtime 95296515 builds against the new Core source override, which deliberately
does not enforce the published 0.8.0/ABI3 dependency lock. Its first regression
probe passes 40/57 targets and fails 17 application/desktop targets because the
unchanged manifests require the pre-1.0 0.8 line and the new binary selects 0.9.
This is not the older matched ABI3 consumer's passing 56/56 result. Coordinated
manifest/tooling/lock qualification is pending; no version refusal was bypassed.
The private-header fingerprint test was added after this initial Runtime probe.

Next: preserve fractional dimensions through a common computed-size owner and
general percentage/layout consumers, then qualify matched Runtime/package data.
No source archive, dependency lock, remote branch or release has been published.
