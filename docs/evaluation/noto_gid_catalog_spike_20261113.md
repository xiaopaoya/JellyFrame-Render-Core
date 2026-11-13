# Noto GID bitmap catalog spike

> Last updated: 2026-11-13; Applies to: 0.9.0-dev / ABI 4 qualification line

This is an offline raster-resource experiment only. It changes no Core API,
JFFONT decoder, JFTP packet, Runtime behavior, or default font profile. The
prototype serializer is [`noto_gid_catalog_spike.py`](../../tools/noto_gid_catalog_spike.py)
and writes the experimental `JFGID1` format; it is not a production format.

## Reproduction

The product input is Noto Sans SC 2.002 Regular, SHA-256
`A2B93E6C2DB05D6BBBF6F27D413EC73269735B7B679019C8A5AA9670FF0FFBF2`.
Use Python 3.11, fontTools 4.60.1, Pillow 12.3.0 and FreeType 2.14.3. The
fontTools `font_resource_requirements.txt` pins Pillow and fontTools; install
`uharfbuzz==0.57.0` separately for the shaping-oracle probe. The Noto input is
vendored in Runtime at
`ports/esp32s3-idf/font/source/noto-sans-sc-2.002/NotoSansSC-Regular.otf`.

The 15-GID fixture below covers a composed acute, `office` ligature, the
Simplified-Chinese em dash alternate, Chinese punctuation, Bopomofo mark
attachment, and an unencoded `ccmp` output GID. Build it with a sorted text
file containing the listed IDs:

```powershell
python tools/noto_gid_catalog_spike.py `
  --font NotoSansSC-Regular.otf --glyph-ids glyph-ids.txt --size 16 `
  --output sample.jfgid --report sample.json `
  --scalar-baseline-bytes 716274
```

The IDs are `68 70 80 126 251 466 1131 1396 8744 14689 29621 29702 30301
30436 30441`. The tool writes a temporary PUA cmap into an in-memory copy of
the OTF so Pillow/FreeType can rasterize explicit glyph IDs, including glyphs
that have no Unicode cmap entry. The original font bytes and tracked sources
are not modified.

## Result and limits

The deterministic 16 px, 4bpp catalog is 1,454 bytes, including a 96-byte
header, fifteen 32-byte GID records and 878 bytes of coverage. Its SHA-256 is
`d09d8d598d154c954692848c356855cb777c71c58cf6fc6581ad7d91bd4d6caa`. Repeated
generation on the pinned host environment produced identical bytes. The blob
is 0.203% of the existing 716,274-byte scalar JFFONT2 asset, but this is a
15-glyph fixture and must not be extrapolated to a full font or product corpus.

The current scalar asset has 4,539 mappings and 4,538 distinct GIDs. Comparing
the HarfBuzz sample outputs against that catalog found 5 emitted Regular GIDs
without a bitmap entry: 126, 251, 252, 254 and 29621. These are a composed
glyph, combining marks and the `ffi` ligature; scalar cmap lookup alone cannot
draw them. The GID catalog demonstrates that the bitmap resource can address
such outputs, including zero-advance marks, but it does not encode or execute
GSUB/GPOS lookups.

Still unmeasured: a generated normalized shaping-table resource, complete
Script/LangSys closure output coverage, catalog size for all admitted output
GIDs, MCU flash delta, target load peak, scratch, work and latency. The prototype
does not establish a supported shaping profile or choose a JFFONT version.
ABI4 remains a qualification track and ABI3 remains the Runtime production
lock.
