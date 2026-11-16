# Selected-Font Shaping Normalization

The shaping normalizer is a private Core primitive. It is not part of the ABI4
installed C++ surface and is not registered with `TextSystem`, layout, paint,
or Runtime. Production text therefore continues to use the existing scalar
path until those integrations and their device acceptance are completed.

The primitive consumes one already-admitted UTF-8 run, performs strict UTF-8
decoding, Unicode 14 canonical decomposition, canonical combining-class
reordering, and font-aware canonical composition, then resolves normalized
scalars through an identity-bound raster catalog. It retains the original
UTF-8 byte interval on every output scalar so later shaping can produce source
clusters. It refuses controls, default-ignorable scalars, missing source-map
entries, and any normalized scalar outside the selected catalog. It does not
apply compatibility normalization, whitespace collapse, line breaking,
grapheme segmentation, or host font fallback.

Composition eligibility is generated from the selected source font's full
cmap, rather than inferred from the finite raster catalog. Hangul decomposition
and composition are algorithmic; product faces that do not contain Hangul
syllables refuse those candidates. Exact Unicode tables and per-face
eligibility are SHA-256 bound. Generation inputs and oracle cases are pinned
under `tests/fixtures/shaping_normalization/`; `tools/README.md` documents
regeneration.

Caller-provided typed scratch and cumulative work budgets bound processing.
Published output/count remain unchanged on refusal; scratch and consumed work
may change. Provenance groups may be coarser than grapheme or caret clusters.
The official Unicode 14 normalization corpus qualifies the canonical
algorithm against an explicitly synthetic complete mapping. Separate tests
exercise the actual pinned Noto faces and the selected catalog refusal policy;
synthetic glyph membership is never presented as product-font coverage.
