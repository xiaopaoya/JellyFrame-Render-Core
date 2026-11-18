# JFSP v2 packet codec

Render Core uses this wire format internally for the ABI4 shaped-text binding. Configure `JELLYFRAME_ENABLE_SHAPED_PACKET_V2=ON` to also install `<render_core/shaped_packet_v2.h>` and expose the standalone codec. The default installation omits this header. The switch is independent of `JELLYFRAME_ENABLE_TEXT_SHAPING`, so it does not enable a text binding, alter the engine ABI, or change layout, paint, or Runtime transport.

The little-endian header is 80 bytes: `JFSP` at 0, version u16=2 at 4, header size u16=80 at 6, total bytes u32 at 8, glyph count u32 at 12, cluster count u32 at 16, source byte count u32 at 20, positive size Q1000 i32 at 24, selection u16 (0..4) at 28, flags u16=0 at 30, nonzero resource ID u64 at 32, nonzero binding generation u64 at 40, and a 32-byte binding identity at 48. The identity cannot be all zero; decoding does not authenticate it. The exact UTF-8 source follows, then 16-byte clusters, then 36-byte glyphs. There is no padding or trailing data.

Cluster records contain source_begin/source_end/glyph_begin/glyph_end u32. Both interval sets form complete monotone nonempty partitions, with UTF-8 byte boundaries. All glyphs in a cluster name that cluster index. Several scalars per cluster permit ligatures; several glyphs per cluster permit decomposition. Empty source requires zero clusters and glyphs; its header remains a valid packet.

Glyph records contain gid u32, cluster index u32, signed Q1000 pen x/y, advance x/y, offset x/y and reserved u32=0. GID is nonzero and below the caller glyph ceiling (default65536); this numeric test does not establish raster catalog membership. Pen positions are explicit independent coordinates, not a recurrence derived from advances. Negative advances are representable. Both pen+offset and pen+advance must fit signed32. Y is CSS down; a future service converts executor font-up units once. Advances are shaped values, not intrinsic bitmap metrics. Paint must resolve each GID against the immutable registered binding and apply offsets exactly once.

Strict UTF-8 accepts valid Unicode including NUL; control/default-ignorable refusal belongs to service policy. Full source retention proves interval byte boundaries. Normalization provenance must be merged into nonoverlapping source clusters before encoding; this codec does not normalize or verify shaping correctness.

The public limits are at most 65536 source bytes, 4096 clusters, 4096 glyphs, 1 MiB of packet data, and an exclusive GID ceiling of 65536. Each limit must be nonzero. The caller supplies a remaining `uint64_t` work budget. A successful decode charges all bytes plus one unit per cluster and glyph. A successful encode charges draft bytes plus that decode cost. On any refusal, the public codec preserves both output and work; the internal codec retains its existing cumulative charging on refusal.

Decode is allocation-free and supports unaligned input. The view borrows immutable packet bytes, which must outlive it. Encode publishes a temporary string by swap after validation, so the source may alias the previous output. Record pointers must refer to valid typed arrays for the duration of the call.

The codec checks structure and bounds. Catalog membership, packet provenance, and agreement with the selected shaping service must be checked by the binding or consumer.
