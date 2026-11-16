# Private JFSP1 packet prototype

This codec is private. It does not activate shaping, Runtime transport or a public ABI. Existing JFTP v1/v2/v3 remains unchanged.

Little-endian header is 48 bytes: magic JFSP at0, version u16=1 at4, header bytes u16=48 at6, total bytes u32 at8, glyph count u32 at12, cluster count u32 at16, source byte count u32 at20, positive size Q1000 i32 at24, selection u16 (0..4) at28, flags u16=0 at30, nonzero resource ID u64 at32 and nonzero binding generation u64 at40. Exact UTF-8 source follows, then 16-byte clusters, then 36-byte glyphs. No padding or trailing bytes.

Cluster records contain source_begin/source_end/glyph_begin/glyph_end u32. Both interval sets form complete monotone nonempty partitions, with UTF-8 byte boundaries. All glyphs in a cluster name that cluster index. Several scalars per cluster permit ligatures; several glyphs per cluster permit decomposition. Empty source requires zero clusters and glyphs; its header remains a valid packet.

Glyph records contain gid u32, cluster index u32, signed Q1000 pen x/y, advance x/y, offset x/y and reserved u32=0. GID is nonzero and below the caller glyph ceiling (default65536); this numeric test does not establish raster catalog membership. Pen positions are explicit independent coordinates, not a recurrence derived from advances. Negative advances are representable. Both pen+offset and pen+advance must fit signed32. Y is CSS down; a future service converts executor font-up units once. Advances are shaped values, not intrinsic bitmap metrics. Paint must resolve each GID against the immutable registered binding and apply offsets exactly once.

Strict UTF-8 accepts valid Unicode including NUL; control/default-ignorable refusal belongs to service policy. Full source retention proves interval byte boundaries. Normalization provenance must be merged into nonoverlapping source clusters before encoding; this codec does not normalize or verify shaping correctness.

Limits default to 65536 source bytes,4096 clusters/glyphs and1MiB packet bytes. Caller supplies cumulative remaining uint64 work. Decode charges all bytes plus each visited cluster/glyph; encode charges draft bytes then the same decode validation. All offset products use bounded64 arithmetic. UTF-8 traversal is linear and boundary checks constant time. Decode is allocation-free and unaligned safe. Encode owns a temporary string and publishes by swap after validation; source may alias the previous output. Pointer record arrays must be valid typed arrays; callers must not fabricate typed aliases into byte storage. Refusal preserves output, while work may be consumed. View bytes and source are borrowed immutable storage that must outlive it.

Validation includes every truncation, malformed framing/counts, UTF-8 and byte splits, invalid source/glyph partitions, missing identity, numeric GIDs, reserved bytes, coordinate overflow, budgets, unaligned decode, aliasing old output, ligature and multi-glyph clusters, empty source and NUL. No renderer or service acceptance is implied.

Independent parent verification passed fresh Clang Debug full CTest36/36 and a four-optional-family-disabled focused codec CTest1/1. The worker also ran strict warnings and AddressSanitizer. These validate the private codec stage, while catalog membership, service/paint/Runtime admission remain integration work.
