# C5 mixed-dimension oracle contract (2026-11-11)

This file records acceptance inputs for the complete mixed-dimension package.
It is a test contract, not a claim that parser or layout behavior is implemented.
Parser admission remains closed until storage, publication, and every consumer
family pass together. The executable storage oracle is
`tests/unit/c5_mixed_dimension_storage_oracle_tests.cpp`.

## Executable storage baseline

The standalone test covers all seven dimension slots, signed Q1000 terms,
zero-percentage provenance, legacy-getter non-hiding behavior, shared snapshot
isolation, and invalid-input atomicity. The existing storage API can compile
this test without changing production code. The existing combined storage test
also covers publication and allocation-failure behavior; this test deliberately
does not edit or duplicate that file.

## Used-value reference cases

Use Q1000 inputs and preserve signed components through combination. At a
100.25px definite basis:

| Property/expression | Expected used result before integer projection |
| --- | ---: |
| width `calc(20px + 50%)` | 70.125px |
| width `calc(50% - 20px)` | 30.125px |
| width `calc(10px - 50%)` | 0px after final used-size clamp |
| width `calc(20px + 0%)` | 20px, still typed as length-percentage |
| min-width `calc(80px - 50%)` | 29.875px |
| max-width `calc(30px + 50%)` | 80.125px |

Resolve the full expression before nonnegative clamping, preferred/min/max
range clamping, and border-box subtraction. If min exceeds max, min wins as in
the established precise axis evaluator. Height and flex-basis use their actual
definite basis; an indefinite percentage-bearing height or flex basis follows
whole-expression auto/content behavior and cannot use its length term alone.

## Parser and publication acceptance

Accept bounded two-term `calc(length +/- percentage)` and the reversed term
order for supported absolute/computed lengths. Require CSS operator whitespace.
Reject nested math, multiplication/division, unsupported units, nonfinite values,
and component bounds atomically. Keep invalid declaration behavior and previous
value intact. Balanced flex shorthand tokenization must treat the whole calc as
one basis token. `calc(20px + 0%)` must retain percentage-bearing provenance.
Publication must atomically update typed components, dimension scalar proxies
to `-1`, percentage proxies to `-1`, FlexBasis kind state where applicable, and
source provenance. Allocation failure leaves every representation unchanged.

## Context matrix

| Context | Required result |
| --- | --- |
| Ordinary and precise non-replaced Block, definite basis | Same signed mixed result; ordinary integer projection occurs only after combination. |
| Qualified nowrap Flex and admitted wrapped-row Flex | Same mixed width/height/min/max/flex-basis evaluation; indefinite main basis uses content behavior. |
| Grid item sizing | Same used-value semantics as Block/Flex within its qualified definite-basis slice. |
| Forced precise-plan refusal | Ordinary replay matches the ordinary result and restores the complete authored typed slot, scalar/kind values, and provenance on rollback. |
| Fixed table size gate | Typed mixed slot is explicitly rejected; it cannot appear absent via the legacy three-output getter. |
| CyclicSameAxis intrinsic preferred width on neutral static non-replaced Block | A percentage-bearing expression is treated wholly as auto, including zero coefficient. |
| Cyclic intrinsic pure-percent min/max | Existing zero/none substitutions remain. |
| Cyclic mixed min/max, vertical authored dimensions, replaced/native/decorated/unknown contexts | Explicit Unsupported and exact caller fallback; never consume scalar proxies as complete values. |
| DefiniteIndependent intrinsic query | Explicit Unsupported until per-block independent-basis contribution semantics exist. |

Cross-family tests should compare typed state and retained fractional geometry
before projection, then compare final integer rectangles where the consumer
boundary requires them. The package must also verify pass refusal before
mutation, complete journal rollback after late refusal, and parity between
ordinary execution and forced fallback. This contract follows the C5 mixed
dimension implementation contract and the 2026-11-11 consumer integration
audit; those define implementation ownership and the parser-admission gate.
