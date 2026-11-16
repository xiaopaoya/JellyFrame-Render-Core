from pathlib import Path
import argparse
import unicodedata as u, struct, hashlib, json

parser = argparse.ArgumentParser(description="Regenerate pinned Unicode14 normalization fixtures")
parser.add_argument("--font-dir", type=Path, required=True,
                    help="Directory containing pinned NotoSansSC-{Regular,Medium,Bold}.otf")
args = parser.parse_args()

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests/fixtures/shaping_normalization"
NORMALIZATION_TEST_SHA256 = "7cb30cc2abe6c29c292b99095865d379ce1213045c78c4ff59c7e9391bbe2331"
DERIVED_CORE_PROPERTIES_SHA256 = "e3eddd7d469cd1b0feed7528defad1a1cc7c6a9ceb0ae4446a6d10921ed2e7bc"
if u.unidata_version != "14.0.0":
    raise SystemExit("Unicode14 required")
D = []
pool = []
C = []
P = []
for cp in range(0x110000):
    d = u.decomposition(chr(cp))
    if d and not d.startswith("<"):
        seq = list(map(ord, u.normalize("NFD", chr(cp))))
        D.append((cp, len(pool), len(seq)))
        pool += seq
        pair = [int(x, 16) for x in d.split()]
        if len(pair) == 2 and u.normalize("NFC", "".join(map(chr, pair))) == chr(cp):
            P.append((*pair, cp))
    if u.combining(chr(cp)):
        C.append((cp, u.combining(chr(cp))))
P.sort()
R = []
raw = (OUT / "DerivedCoreProperties-14.0.0.txt").read_bytes()
assert hashlib.sha256(raw).hexdigest() == DERIVED_CORE_PROPERTIES_SHA256
assert b"DerivedCoreProperties-14.0.0.txt" in raw[:100]
for line in raw.decode().splitlines():
    line = line.split("#")[0].strip()
    if not line:
        continue
    span, prop = map(str.strip, line.split(";"))
    if prop != "Default_Ignorable_Code_Point":
        continue
    a = span.split("..")
    R.append((int(a[0], 16), int(a[-1], 16)))
# header 32: magic8, Unicode major/minor u16, counts D,pool,C,P,R u32
b = struct.pack(
    "<8sHHIIIII", b"JFN14\0\0\0", 14, 0, len(D), len(pool), len(C), len(P), len(R)
)
b += (
    b"".join(struct.pack("<IIH", *x) for x in D)
    + b"".join(struct.pack("<I", x) for x in pool)
    + b"".join(struct.pack("<IB", *x) for x in C)
    + b"".join(struct.pack("<III", *x) for x in P)
    + b"".join(struct.pack("<II", *x) for x in R)
)
(OUT / "unicode14.bin").write_bytes(b)
from fontTools.ttLib import TTFont

info = json.loads((ROOT / "tests/fixtures/shaping_resource/manifest.json").read_text())
manifest = {
    "unicode": "14.0.0",
    "normalizationTestSHA256": NORMALIZATION_TEST_SHA256,
    "normalizationTestURL": "https://www.unicode.org/Public/14.0.0/ucd/NormalizationTest.txt",
    "tableSHA256": hashlib.sha256(b).hexdigest(),
    "tableBytes": len(b),
    "generatorSHA256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    "propertiesSHA256": hashlib.sha256(raw).hexdigest(),
    "propertiesURL": "https://www.unicode.org/Public/14.0.0/ucd/DerivedCoreProperties.txt",
    "faces": {},
}
for face, v in info["faces"].items():
    f = args.font_dir / ("NotoSansSC-" + face + ".otf")
    fb = f.read_bytes()
    assert hashlib.sha256(fb).hexdigest() == v["sourceSha256"]
    cmap = TTFont(f).getBestCmap()
    bits = bytearray(118)
    # composition sorted PAIR order; one bit per941 pairs, no duplicate results in Unicode14.
    assert len(P) == 941 and len(set(c for a, b, c in P)) == 941
    for i, (a, bb, c) in enumerate(P):
        if c in cmap:
            bits[i // 8] |= 1 << (i % 8)
    # 80 header magic8 source32 table32 flags4 payloadbytes4; Hangul0=none,1=all,2=bitset1397
    hangul = [x in cmap for x in range(0xAC00, 0xD7A4)]
    mode = 1 if all(hangul) else 0 if not any(hangul) else 2
    hb = bytearray(1397) if mode == 2 else bytearray()
    for i, vv in enumerate(hangul):
        if mode == 2 and vv:
            hb[i // 8] |= 1 << (i % 8)
    e = (
        struct.pack(
            "<8s32s32sII",
            b"JFNE14\0\0",
            bytes.fromhex(v["sourceSha256"]),
            hashlib.sha256(b).digest(),
            mode,
            len(bits) + len(hb),
        )
        + bits
        + hb
    )
    (OUT / (face.lower() + ".eligibility")).write_bytes(e)
    manifest["faces"][face] = {
        "sourceSHA256": v["sourceSha256"],
        "eligibilitySHA256": hashlib.sha256(e).hexdigest(),
        "bytes": len(e),
    }
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
# Synthetic algorithm fixtures explicitly use a synthetic source identity and
# trusted membership, never claim actual Noto font cmap/raster correctness.
import zlib

base = (ROOT / "tests/fixtures/shaping_catalog/regular.jfcat1").read_bytes()
comp = bytearray((ROOT / "tests/fixtures/shaping_resource/regular.jfshr2").read_bytes())
synth = hashlib.sha256(b"normalizer synthetic algorithm source v1").digest()
comp[16:48] = synth
(OUT / "synthetic.jfshr2").write_bytes(comp)
n = struct.unpack_from("<I", base, 116)[0]
maps = {
    struct.unpack_from("<I", base, 172 + i * 8)[0]: struct.unpack_from(
        "<H", base, 176 + i * 8
    )[0]
    for i in range(n)
}
gid = maps[65]
normalization_tests = (OUT / "NormalizationTest-14.0.0.txt").read_bytes()
assert hashlib.sha256(normalization_tests).hexdigest() == NORMALIZATION_TEST_SHA256
assert b"NormalizationTest-14.0.0.txt" in normalization_tests[:100]
for line in normalization_tests.decode("utf-8").splitlines():
    line = line.split("#", 1)[0].strip()
    if not line or line.startswith("@"):
        continue
    for column in line.split(";")[:5]:
        for text in column.split():
            cp = int(text, 16)
            maps[cp] = gid
for cp in [0x302, 0x303, 0x323, 0x327, 0x1100, 0x1161, 0x11A8, 0xAC00, 0xAC01]:
    maps[cp] = gid
payload = (
    b"".join(struct.pack("<IHH", cp, g, 3) for cp, g in sorted(maps.items()))
    + base[172 + n * 8 :]
)
header = bytearray(base[:172])
header[16:48] = synth
header[80:112] = hashlib.sha256(comp).digest()
struct.pack_into("<I", header, 12, 172 + len(payload))
struct.pack_into("<I", header, 116, len(maps))
struct.pack_into("<I", header, 156, zlib.crc32(payload))
(OUT / "synthetic.jfcat1").write_bytes(header + payload)
bits = bytearray([255] * 118)
bits[-1] = 0x1f  # 941 canonical composition pairs, unused high bits zero.
# Unrestricted synthetic Unicode algorithm policy, never a Noto cmap claim.
e = (
    struct.pack("<8s32s32sII", b"JFNE14\0\0", synth, hashlib.sha256(b).digest(), 1, 118)
    + bits
)
(OUT / "synthetic.eligibility").write_bytes(e)
# Canonical core oracle cases with no font-dependent missing composite, using
# Python14 NFD/NFC. Provenance intentionally merges interaction segments.
cases = [
    "\u1100\u1161\u11a8",
    "\uac00\u11a8",
    "\uac01",
    "B\u0301\u0327",
    "\u0301\u0327",
    "B\u0301\u0300",
    "A\u0301\u0301",
    "\u00e9",
]
(OUT / "oracle.txt").write_text(
    "\n".join(
        t.encode().hex() + " " + ",".join(str(ord(x)) for x in u.normalize("NFC", t))
        for t in cases
    )
    + "\n"
)
manifest["synthetic"] = {
    "sourceSHA256": synth.hex(),
    "purpose": "Unicode14 algorithm tests only; artificial complete test map/composition eligibility; not real font raster evidence",
}
for name in [
    "synthetic.jfcat1",
    "synthetic.jfshr2",
    "synthetic.eligibility",
    "oracle.txt",
]:
    data = (OUT / name).read_bytes()
    manifest["synthetic"][name] = {
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
    }
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
