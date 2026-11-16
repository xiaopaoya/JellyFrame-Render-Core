# Private selected-profile raster catalog

JFCAT1 is a private borrowed view and joint JFSHR2 binding. It does not activate TextSystem shaping or freeze a public ABI. Registration supplies independently trusted whole catalog and companion SHA256 digests; Core computes both against the supplied immutable bytes. Hash traversal, CRC traversal, companion validation and closure checks consume one caller work ledger. Refusal preserves the previous view.

The catalog shares one original-GID raster record and bitmap extent across scalar aliases. Intermediate-only mappings do not admit raw input. Binding proves the stronger conservative condition that the entire raster superset is closed under every encoded single/ligature substitution; positioning-only references need no raster. The host retains catalog and companion allocations through every prepared result and replay.

The generator verifies pinned source and raster evidence hashes, inspects original selected PairPos subtables for ValueFormat2=0, and proves selected MarkCoverage equals GDEF class 3 within admitted raster GIDs for every selection. This subset proof permits the executor to classify admitted marks from coverage; it does not establish classification for arbitrary source GIDs. Runtime normalization remains separate.

Three fixtures each contain 4694 mapped scalars and 4685 raster GIDs. Regular/Medium/Bold JFCAT1 totals are 733595/734975/744397 bytes. Relative to experimental JFGID totals 695967/697347/706769, the directory/header increase is 37628 bytes per face. These totals replace/shared raster storage and do not establish additive device flash cost.

Generate with tools/noto_shaping_catalog.py --evidence <normalization-report-directory> --fixtures tests/fixtures/shaping_resource --output <directory>. Python dependencies include fontTools and uharfbuzz (the existing selection-plan module imports its oracle dependency). Independently retain the generated manifest as registration metadata. Actual generation was reproduced byte-for-byte for all three fixtures.

Focused CTest and direct AddressSanitizer execution pass. SHA known vectors include empty, abc and one million bytes; malformed checks include framing, identities, flags, GID ceiling, missing closure outputs and cumulative budget refusal. The tests cover unaligned input and unchanged views on refusal. Installed service, generation/resource-ID management and renderer integration remain later work.
`nParent verification also passed full CTest 35/35 and cropped catalog CTest 1/1. Digest-repaired negatives cover stride, overlapping/overflowed extents, odd-width padding, absent mapping targets and duplicate scalar order, preserving metadata and both borrowed views. SHA padding-boundary vectors cover 55/56/63/64 bytes.
