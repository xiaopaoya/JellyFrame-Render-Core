#include "shaping_normalization.h"
#include "shaping_catalog_sha256.h"
#include <cstring>
namespace jellyframe {
namespace shaping_normalization {
namespace {
std::uint32_t rd(std::string_view b, std::size_t p, unsigned n = 4) noexcept {
  std::uint32_t v = 0;
  for (unsigned i = 0; i < n; ++i)
    v |= std::uint32_t(static_cast<unsigned char>(b[p + i])) << (8 * i);
  return v;
}
bool take(std::uint64_t &w, std::uint64_t n) noexcept {
  if (n > w)
    return false;
  w -= n;
  return true;
}
bool scalar(std::uint32_t x) noexcept {
  return x <= 0x10ffff && !(x >= 0xd800 && x <= 0xdfff);
}
struct Data {
  std::string_view b;
  std::size_t nd, np, nc, nq, nr, d = 32, p, c, q, r;
  explicit Data(std::string_view x)
      : b(x), nd(rd(x, 12)), np(rd(x, 16)), nc(rd(x, 20)), nq(rd(x, 24)),
        nr(rd(x, 28)) {
    p = d + nd * 10;
    c = p + np * 4;
    q = c + nc * 5;
    r = q + nq * 12;
  }
  std::size_t find(std::size_t base, std::size_t count, std::size_t stride,
                   std::uint32_t key) const noexcept {
    std::size_t lo = 0, hi = count;
    while (lo < hi) {
      auto m = lo + (hi - lo) / 2;
      if (rd(b, base + m * stride) < key)
        lo = m + 1;
      else
        hi = m;
    }
    return lo;
  }
  unsigned ccc(std::uint32_t x) const noexcept {
    auto i = find(c, nc, 5, x);
    return i < nc && rd(b, c + i * 5) == x ? rd(b, c + i * 5 + 4, 1) : 0;
  }
};
bool alias(const void *a, std::size_t an, const void *b,
           std::size_t bn) noexcept {
  auto x = reinterpret_cast<std::uintptr_t>(a),
       y = reinterpret_cast<std::uintptr_t>(b);
  return an && bn && (x <= y ? y - x < an : x - y < bn);
}
bool merge(Scalar *s, std::size_t n, std::uint32_t a, std::uint32_t b,
           std::uint64_t &w) noexcept {
  bool changed;
  do {
    changed = false;
    if (!take(w, n))
      return false;
    for (std::size_t i = 0; i < n; ++i)
      if (s[i].source_begin < b && a < s[i].source_end) {
        if (s[i].source_begin < a) {
          a = s[i].source_begin;
          changed = true;
        }
        if (s[i].source_end > b) {
          b = s[i].source_end;
          changed = true;
        }
      }
  } while (changed);
  if (!take(w, n))
    return false;
  for (std::size_t i = 0; i < n; ++i)
    if (s[i].source_begin < b && a < s[i].source_end) {
      s[i].source_begin = a;
      s[i].source_end = b;
    }
  return true;
}
} // namespace
Status validate_tables(std::string_view b, const Hash &expected,
                       const Limits &l, std::uint64_t &w,
                       Tables &out) noexcept {
  if (alias(&out, sizeof(out), b.data(), b.size()) ||
      alias(&out, sizeof(out), &w, sizeof(w)) ||
      alias(&out, sizeof(out), &expected, sizeof(expected)) ||
      alias(&out, sizeof(out), &l, sizeof(l)) ||
      alias(&w, sizeof(w), &expected, sizeof(expected)) ||
      alias(&w, sizeof(w), &l, sizeof(l)) ||
      alias(&w, sizeof(w), b.data(), b.size()))
    return Status::overlap;
  if (b.size() > l.bytes || !take(w, b.size()))
    return Status::limit;
  if (shaping_catalog::sha256(b) != expected)
    return Status::identity;
  if (b.size() < 32 || b.substr(0, 8) != std::string_view("JFN14\0\0\0", 8) ||
      rd(b, 8, 2) != 14 || rd(b, 10, 2) != 0)
    return Status::malformed;
  auto nd = rd(b, 12), np = rd(b, 16), nc = rd(b, 20), nq = rd(b, 24),
       nr = rd(b, 28);
  if (nd != 2061 || np != 3406 || nc != 912 || nq != 941 || nr > 100 ||
      32ULL + nd * 10ULL + np * 4ULL + nc * 5ULL + nq * 12ULL + nr * 8ULL !=
          b.size())
    return Status::malformed;
  Data t(b);
  std::uint32_t prev = 0;
  for (std::size_t i = 0; i < t.nd; ++i) {
    if (!take(w, 1))
      return Status::limit;
    auto p = t.d + i * 10;
    auto cp = rd(b, p), off = rd(b, p + 4), len = rd(b, p + 8, 2);
    if (!scalar(cp) || (i && cp <= prev) || !len || len > 4 || off > np ||
        len > np - off)
      return Status::malformed;
    prev = cp;
  }
  for (std::size_t i = 0; i < t.np; ++i) {
    if (!take(w, 1))
      return Status::limit;
    if (!scalar(rd(b, t.p + i * 4)))
      return Status::malformed;
  }
  prev = 0;
  for (std::size_t i = 0; i < t.nc; ++i) {
    if (!take(w, 1))
      return Status::limit;
    auto cp = rd(b, t.c + i * 5);
    if (!scalar(cp) || (i && cp <= prev) || !rd(b, t.c + i * 5 + 4, 1))
      return Status::malformed;
    prev = cp;
  }
  std::uint64_t keyprev = 0;
  for (std::size_t i = 0; i < t.nq; ++i) {
    if (!take(w, 1))
      return Status::limit;
    auto p = t.q + i * 12;
    auto a = rd(b, p), bb = rd(b, p + 4), c = rd(b, p + 8);
    auto key = (std::uint64_t(a) << 32) | bb;
    if (!scalar(a) || !scalar(bb) || !scalar(c) || (i && key <= keyprev))
      return Status::malformed;
    keyprev = key;
  }
  prev = 0;
  for (std::size_t i = 0; i < t.nr; ++i) {
    if (!take(w, 1))
      return Status::limit;
    auto a = rd(b, t.r + i * 8), bb = rd(b, t.r + i * 8 + 4);
    if (a > bb || bb > 0x10ffff || (i && a <= prev))
      return Status::malformed;
    prev = bb;
  }
  Tables v;
  v.bytes_ = b;
  v.hash_ = expected;
  out = v;
  return Status::ok;
}
Status validate_eligibility(std::string_view b, const Hash &expected,
                            const Tables &t, const shaping_catalog::View &cat,
                            std::uint64_t &w, Eligibility &out) noexcept {
  if (alias(&out, sizeof(out), b.data(), b.size()) ||
      alias(&out, sizeof(out), &w, sizeof(w)) ||
      alias(&w, sizeof(w), b.data(), b.size()) ||
      alias(&out, sizeof(out), &t, sizeof(t)) ||
      alias(&out, sizeof(out), &cat, sizeof(cat)) ||
      alias(&out, sizeof(out), &expected, sizeof(expected)) ||
      alias(&w, sizeof(w), &t, sizeof(t)) ||
      alias(&w, sizeof(w), &cat, sizeof(cat)) ||
      alias(&w, sizeof(w), &expected, sizeof(expected)) ||
      alias(&w, sizeof(w), t.bytes().data(), t.bytes().size()) ||
      alias(&w, sizeof(w), cat.bytes().data(), cat.bytes().size()) ||
      alias(&w, sizeof(w), cat.companion_bytes().data(),
            cat.companion_bytes().size()) ||
      alias(&out, sizeof(out), t.bytes().data(), t.bytes().size()) ||
      alias(&out, sizeof(out), cat.bytes().data(), cat.bytes().size()) ||
      alias(&out, sizeof(out), cat.companion_bytes().data(),
            cat.companion_bytes().size()))
    return Status::overlap;
  if (b.size() > 2048 || !take(w, b.size()))
    return Status::limit;
  if (shaping_catalog::sha256(b) != expected)
    return Status::identity;
  if (t.bytes().empty() || cat.bytes().empty() || b.size() < 198 ||
      b.substr(0, 8) != std::string_view("JFNE14\0\0", 8))
    return Status::malformed;
  auto source = cat.metadata().source_sha256;
  auto hash = t.identity();
  if (std::memcmp(b.data() + 8, source.data(), 32) ||
      std::memcmp(b.data() + 40, hash.data(), 32))
    return Status::identity;
  auto mode = rd(b, 72), size = rd(b, 76);
  if (mode > 2 || size != 118 + (mode == 2 ? 1397 : 0) ||
      b.size() != 80 + size || (rd(b, 197, 1) & 0xe0) ||
      (mode == 2 && (rd(b, b.size() - 1, 1) & 0xf0)))
    return Status::malformed;
  Eligibility v;
  v.bytes_ = b;
  v.tables_identity_ = t.identity();
  v.catalog_identity_ = shaping_catalog::sha256(cat.bytes());
  out = v;
  return Status::ok;
}
bool Eligibility::matches(const Tables &tables,
                          const shaping_catalog::View &catalog) const noexcept {
  return !bytes_.empty() && !tables.bytes().empty() && !catalog.bytes().empty() &&
         tables.identity() == tables_identity_ &&
         shaping_catalog::sha256(catalog.bytes()) == catalog_identity_;
}
Status normalize(const Tables &tables, const Eligibility &e,
                 const shaping_catalog::View &cat, std::string_view src,
                 Scalar *s, std::size_t cap, Scalar *out, std::size_t outcap,
                 std::size_t &count, const Limits &l,
                 std::uint64_t &w) noexcept {
  if (tables.bytes().empty() || e.bytes().empty() || cat.bytes().empty())
    return Status::identity;
  auto eb = e.bytes();
  auto hash = tables.identity();
  auto source = cat.metadata().source_sha256;
  if (std::memcmp(eb.data() + 8, source.data(), 32) ||
      std::memcmp(eb.data() + 40, hash.data(), 32))
    return Status::identity;
  if (src.size() > l.source_bytes || src.size() > UINT32_MAX ||
      cap > SIZE_MAX / sizeof(Scalar) || outcap > SIZE_MAX / sizeof(Scalar))
    return Status::limit;
  if ((cap && !s) || (outcap && !out))
    return Status::capacity;
  auto sn = cap * sizeof(Scalar), on = outcap * sizeof(Scalar);
  if (alias(s, sn, out, on) || alias(s, sn, &count, sizeof(count)) ||
      alias(out, on, &count, sizeof(count)) || alias(s, sn, &w, sizeof(w)) ||
      alias(out, on, &w, sizeof(w)) ||
      alias(&count, sizeof(count), &w, sizeof(w)))
    return Status::overlap;
  for (auto bytes :
       {src, tables.bytes(), eb, cat.bytes(), cat.companion_bytes(),
        std::string_view(reinterpret_cast<const char *>(&tables), sizeof(tables)),
        std::string_view(reinterpret_cast<const char *>(&e), sizeof(e)),
        std::string_view(reinterpret_cast<const char *>(&cat), sizeof(cat)),
        std::string_view(reinterpret_cast<const char *>(&l), sizeof(l))})
    if (alias(s, sn, bytes.data(), bytes.size()) ||
        alias(out, on, bytes.data(), bytes.size()) ||
        alias(&count, sizeof(count), bytes.data(), bytes.size()) ||
        alias(&w, sizeof(w), bytes.data(), bytes.size()))
      return Status::overlap;
  if (!take(w, src.size()))
    return Status::limit;
  Data t(tables.bytes());
  std::size_t n = 0, inputs = 0;
  for (std::size_t p = 0; p < src.size();) {
    auto begin = p;
    auto c = static_cast<unsigned char>(src[p++]);
    std::uint32_t cp = c;
    unsigned more = 0;
    std::uint32_t minimum = 0;
    if (c >= 0x80) {
      if (c >= 0xc2 && c <= 0xdf) {
        more = 1;
        cp = c & 31;
        minimum = 128;
      } else if (c >= 0xe0 && c <= 0xef) {
        more = 2;
        cp = c & 15;
        minimum = 2048;
      } else if (c >= 0xf0 && c <= 0xf4) {
        more = 3;
        cp = c & 7;
        minimum = 65536;
      } else
        return Status::malformed;
      if (more > src.size() - p)
        return Status::malformed;
      while (more--) {
        auto v = static_cast<unsigned char>(src[p++]);
        if ((v & 0xc0) != 0x80)
          return Status::malformed;
        cp = (cp << 6) | (v & 63);
      }
      if (cp < minimum || !scalar(cp))
        return Status::malformed;
    }
    if (++inputs > l.input_scalars)
      return Status::limit;
    if (!take(w, 17 + t.nr))
      return Status::limit;
    if (cp < 32 || (cp >= 127 && cp <= 159))
      return Status::unsupported;
    for (std::size_t i = 0; i < t.nr; ++i)
      if (cp >= rd(t.b, t.r + i * 8) && cp <= rd(t.b, t.r + i * 8 + 4))
        return Status::unsupported;
    std::uint16_t gid;
    if (!cat.map_scalar(cp, true, gid))
      return Status::unsupported;
    std::uint32_t seq[4] = {cp};
    std::size_t len = 1;
    if (cp >= 0xac00 && cp <= 0xd7a3) {
      auto idx = cp - 0xac00;
      seq[0] = 0x1100 + idx / 588;
      seq[1] = 0x1161 + (idx % 588) / 28;
      len = 2;
      if (idx % 28)
        seq[len++] = 0x11a7 + idx % 28;
    } else {
      auto i = t.find(t.d, t.nd, 10, cp);
      if (i < t.nd && rd(t.b, t.d + i * 10) == cp) {
        auto off = rd(t.b, t.d + i * 10 + 4);
        len = rd(t.b, t.d + i * 10 + 8, 2);
        for (std::size_t j = 0; j < len; ++j)
          seq[j] = rd(t.b, t.p + (off + j) * 4);
      }
    }
    if (!take(w, 12 + len))
      return Status::limit;
    if (len > l.expanded_scalars || n > l.expanded_scalars - len)
      return Status::limit;
    if (len > cap || n > cap - len)
      return Status::capacity;
    for (std::size_t j = 0; j < len; ++j)
      s[n++] = {seq[j], static_cast<std::uint32_t>(begin),
                static_cast<std::uint32_t>(p), 0};
  }
  // Merge a whole canonical interaction segment before stable reorder.
  for (std::size_t begin = 0; begin < n;) {
    auto end = begin + 1;
    while (end < n) {
      if (!take(w, 11))
        return Status::limit;
      if (t.ccc(s[end].scalar) == 0)
        break;
      ++end;
    }
    auto a = s[begin].source_begin, b = s[end - 1].source_end;
    // A segment already belonging to one source scalar needs no global
    // provenance merge. In particular plain ASCII runs stay linear here.
    if ((s[begin].source_begin != s[end - 1].source_begin ||
         s[begin].source_end != s[end - 1].source_end) &&
        !merge(s, n, a, b, w))
      return Status::limit;
    for (auto i = begin + 1; i < end; ++i) {
      auto cur = s[i];
      auto j = i;
      while (j > begin) {
        if (!take(w, 22))
          return Status::limit;
        auto left = t.ccc(s[j - 1].scalar), right = t.ccc(cur.scalar);
        if (!left || left <= right)
          break;
        s[j] = s[j - 1];
        --j;
      }
      s[j] = cur;
    }
    begin = end;
  }
  std::size_t starter = SIZE_MAX;
  unsigned last = 0;
  for (std::size_t i = 0; i < n;) {
    if (!take(w, 11))
      return Status::limit;
    auto cc = t.ccc(s[i].scalar);
    std::uint32_t composite = 0;
    std::size_t pair = SIZE_MAX;
    if (starter != SIZE_MAX && (last < cc || last == 0)) {
      auto a = s[starter].scalar, b = s[i].scalar;
      if (a >= 0x1100 && a < 0x1113 && b >= 0x1161 && b < 0x1176)
        composite = 0xac00 + ((a - 0x1100) * 21 + b - 0x1161) * 28;
      else if (a >= 0xac00 && a <= 0xd7a3 && (a - 0xac00) % 28 == 0 &&
               b > 0x11a7 && b < 0x11c3)
        composite = a + b - 0x11a7;
      else {
        if (!take(w, 11))
          return Status::limit;
        std::size_t lo = 0, hi = t.nq;
        auto key = (std::uint64_t(a) << 32) | b;
        while (lo < hi) {
          auto m = lo + (hi - lo) / 2;
          auto p = t.q + m * 12;
          auto k = (std::uint64_t(rd(t.b, p)) << 32) | rd(t.b, p + 4);
          if (k < key)
            lo = m + 1;
          else
            hi = m;
        }
        if (lo < t.nq) {
          auto p = t.q + lo * 12;
          if (rd(t.b, p) == a && rd(t.b, p + 4) == b) {
            composite = rd(t.b, p + 8);
            pair = lo;
          }
        }
      }
      bool allowed = false;
      if (composite) {
        if (pair != SIZE_MAX)
          allowed = (rd(eb, 80 + pair / 8, 1) >> (pair % 8)) & 1;
        else {
          auto mode = rd(eb, 72);
          auto idx = composite - 0xac00;
          allowed =
              mode == 1 ||
              (mode == 2 && ((rd(eb, 198 + idx / 8, 1) >> (idx % 8)) & 1));
        }
      }
      if (allowed) {
        auto begin = s[starter].source_begin, end = s[i].source_end;
        if (!take(w, n + (n - i - 1)))
          return Status::limit;
        s[starter].scalar = composite;
        if (!merge(s, n, begin, end, w))
          return Status::limit;
        for (auto j = i + 1; j < n; ++j)
          s[j - 1] = s[j];
        --n;
        continue;
      }
    }
    if (cc == 0)
      starter = i;
    last = cc;
    ++i;
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (!take(w, 17))
      return Status::limit;
    if (!cat.map_scalar(s[i].scalar, false, s[i].gid))
      return Status::unsupported;
  }
  if (n > outcap)
    return Status::capacity;
  if (!take(w, n))
    return Status::limit;
  for (std::size_t i = 0; i < n; ++i)
    out[i] = s[i];
  count = n;
  return Status::ok;
}
} // namespace shaping_normalization
} // namespace jellyframe
