# Scalar cascade cache delivery

The implementation is af6d0490 with resolver-lifetime follow-up0a299e17, based
on Core93744c8f. It ports the accepted scalar/fractional-owner cache from
experimental integration0b32ccf9 to current ComputedSizeStorage and existing
rule/context caches. Exact inputs identify pre-inheritance declaration results.
Limits bound entries, key bytes and retained payload. Diagnostics, unsupported
owners, capacity refusal and cache-local allocation failures use ordinary
resolution. Literal width/height owners retain independent mutation behavior.

Release and Debug pass the scalar, storage, aggregate Core and multilayer style
checks. A public installed consumer passes. Release runs48 one-shot allocation
failure cases. Debug excludes only the global allocation-failure sweep after an
independent STL-only control reproduced termination allocating inside a noexcept
default string constructor. Evidence and the control are under
E:/Experiments/scalar-cache-delivery-20261112.

Current performance evidence is a tests-OFF MSVC19.41 Release binary with both
enabled and disabled modes,20 alternating pairs per workload. Each process
warms10 iterations and times200 iterations over400 cards. Checksums agree and
cache hit counts are recorded. Frozen timing source is af6d0490. Follow-up
0a299e17 changes moved-from resolver cleanup, test configuration and formatting,
with the timed resolution/cache algorithms unchanged.

| Workload | Mean reduction | Paired t95% interval | Retained payload bytes |
| --- | --- | --- | --- |
| Repeated scalar declarations | 81.18% | 80.79–81.57% | 5888 |
| Repeated fractional dimensions | 82.92% | 82.18–83.67% | 6080 |
| Repeated resolved variables | 68.67% | 68.24–69.09% | 6027 |
| Repeated RenderTree construction | 61.81% | 61.02–62.59% | 5888 |
| Unique element IDs | -6.95% | -12.45–-1.44% | 19198 |
| New resolver per400-card pass | 57.86% | 53.21–62.51% | 5888 |

The unique-ID workload loses time because exact-key construction and bounded
lookup continue for mostly distinct inputs. Hosts select this cache explicitly
through StyleResolverOptions limits. Default limits are zero. Runtime activation
requires a workload policy and further application evidence. These synthetic
desktop timings qualify the stated workloads. Device costs remain unmeasured.

Reproducible harness, frozen pairs and result summaries are under
E:/Experiments/scalar-cache-measurement-20261112. The frozen-af6d0490 subdirectory
retains final raw pairs. Earlier exploratory runs are preserved separately.
