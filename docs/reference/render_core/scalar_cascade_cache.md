# Scalar cascade cache

`StyleResolverOptions::max_scalar_cache_entries` and `max_scalar_cache_bytes` opt into cross-node reuse of pre-inheritance cascade results. Both default to zero. A host can set them to 16 and 32768 for repeated scalar-style workloads. Host activation remains a separate Runtime integration decision. Unique-input lookup overhead makes workload-specific activation appropriate.

The resolver retains at most 16 entries and 32768 accounted payload bytes. Exact keys contain at most 2048 bytes. Keys include tag, ordered complete attributes, document-root-element status, effective viewport, sorted resolved custom properties and ordered matched-rule indices. Matching still observes selector ancestry, siblings and interaction state. Existing context mutation guarantees apply.

Admission accepts scalar Style state and literal pixel/percentage computed-size owners for the six width/height dimensions. Mixed units, flex-basis/inset owners and nonzero transition/animation vector capacity use ordinary resolution. Diagnostics, excessive inputs, exhausted budgets and cache-local allocation failures also use ordinary resolution. Resolver assignment refreshes existing resolution contexts. Copy/move operations refresh cache and resolver-local resource-registry lifetimes.

Statistics expose hits, misses, bypasses, retained entries and accounted payload. Payload includes the cache object, retained entry objects, retained key/string capacities with terminators and a complete computed-size owner per entry. Allocator metadata and temporary allocation peaks are excluded. Copies are completed and charged before entry publication.

The focused standalone regression compares every current Style field, including all four background layers and every resource ID. It exercises contextual selectors, variables, HTML hints, fractional owner isolation, exact boundaries, disabled/diagnostic/unsupported/oversized fallbacks, copy/move, assignment with an existing context, and one-shot allocation failures. Paired performance qualification belongs to the delivery evidence alongside workload and activation limitations.
