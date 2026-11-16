#include "shaping_lookup_executor.h"
#include <limits>
namespace jellyframe {
namespace shaping_lookup {
namespace {
struct Reader {
  std::string_view bytes;
  Ledger &ledger;
  Status status = Status::ok;
  bool charge(std::uint64_t amount) noexcept {
    if (amount > ledger.work) {
      status = Status::limit;
      return false;
    }
    ledger.work -= amount;
    return true;
  }
  std::uint32_t integer(std::size_t &offset, unsigned width) noexcept {
    if (status != Status::ok || !charge(1))
      return 0;
    if (offset > bytes.size() || width > bytes.size() - offset) {
      status = Status::invalid_resource;
      return 0;
    }
    std::uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i)
      value |= std::uint32_t(static_cast<unsigned char>(bytes[offset++]))
               << (8 * i);
    return value;
  }
  std::uint32_t at(std::size_t offset, unsigned width) noexcept {
    return integer(offset, width);
  }
  std::int32_t signed16(std::size_t &offset) noexcept {
    const auto value = integer(offset, 2);
    return value < 32768 ? std::int32_t(value) : std::int32_t(value) - 65536;
  }
};
struct Adjustment {
  std::int32_t x = 0, y = 0, advance_x = 0, advance_y = 0;
  unsigned mask = 0;
};
Adjustment value_record(Reader &reader, std::size_t &offset) noexcept {
  Adjustment result;
  result.mask = reader.integer(offset, 1);
  if (result.mask & 1)
    result.x = reader.signed16(offset);
  if (result.mask & 2)
    result.y = reader.signed16(offset);
  if (result.mask & 4)
    result.advance_x = reader.signed16(offset);
  if (result.mask & 8)
    result.advance_y = reader.signed16(offset);
  return result;
}
bool add(Reader &reader, std::int32_t &field, std::int64_t delta) noexcept {
  const std::int64_t result = std::int64_t(field) + delta;
  if (result < std::numeric_limits<std::int32_t>::min() ||
      result > std::numeric_limits<std::int32_t>::max()) {
    reader.status = Status::coordinate_overflow;
    return false;
  }
  field = std::int32_t(result);
  return true;
}
void apply(Reader &reader, Glyph &glyph, const Adjustment &value) noexcept {
  add(reader, glyph.offset_x, value.x);
  add(reader, glyph.offset_y, value.y);
  add(reader, glyph.advance_x, value.advance_x);
  add(reader, glyph.advance_y, value.advance_y);
}
struct Plan {
  std::size_t references = 0, directory = 0;
  unsigned first = 0, count = 0;
};
Plan plan(Reader &reader, unsigned selection) noexcept {
  const auto selections = reader.at(80, 4), references = reader.at(84, 4);
  const auto offset = 112 + selection * 16;
  return {112 + selections * 16, 112 + selections * 16 + references * 8,
          reader.at(offset + 8, 2), reader.at(offset + 10, 2)};
}
std::size_t lookup_entry(Reader &reader, const Plan &plan,
                         unsigned reference) noexcept {
  return plan.directory +
         reader.at(plan.references + (plan.first + reference) * 8 + 4, 2) * 16;
}
bool single(Reader &reader, std::size_t body, Glyph &glyph) noexcept {
  const auto count = reader.integer(body, 4);
  for (unsigned row = 0; row < count && reader.status == Status::ok; ++row) {
    const auto source = reader.integer(body, 2),
               target = reader.integer(body, 2);
    if (source == glyph.gid) {
      glyph.gid = std::uint16_t(target);
      return true;
    }
    if (source > glyph.gid)
      break;
  }
  return false;
}
bool ligature(Reader &reader, std::size_t body, Glyph *glyphs,
              std::size_t index, std::size_t &count) noexcept {
  const auto rows = reader.integer(body, 4);
  for (unsigned row = 0; row < rows && reader.status == Status::ok; ++row) {
    const auto components = reader.integer(body, 1),
               target = reader.integer(body, 2);
    bool matches = components <= count - index;
    for (unsigned component = 0;
         component < components && reader.status == Status::ok; ++component) {
      const auto gid = reader.integer(body, 2);
      if (component >= count - index || gid != glyphs[index + component].gid)
        matches = false;
    }
    if (!matches)
      continue;
    glyphs[index].gid = std::uint16_t(target);
    glyphs[index].source_end = glyphs[index + components - 1].source_end;
    for (std::size_t next = index + components; next < count; ++next) {
      if (!reader.charge(1))
        return false;
      glyphs[next - components + 1] = glyphs[next];
    }
    count -= components - 1;
    // Merge the connected provenance region, including retained glyphs on
    // either side sharing a consumed cluster. Preserve identical partitions.
    auto begin = glyphs[index].source_begin;
    auto end = glyphs[index].source_end;
    bool expanded;
    do {
      expanded = false;
      for (std::size_t i = 0; i < count; ++i) {
        if (!reader.charge(1))
          return false;
        if (glyphs[i].source_begin >= end || glyphs[i].source_end <= begin)
          continue;
        if (glyphs[i].source_begin < begin) {
          begin = glyphs[i].source_begin;
          expanded = true;
        }
        if (glyphs[i].source_end > end) {
          end = glyphs[i].source_end;
          expanded = true;
        }
      }
    } while (expanded);
    for (std::size_t i = 0; i < count; ++i) {
      if (!reader.charge(1))
        return false;
      if (glyphs[i].source_begin < end && glyphs[i].source_end > begin) {
        glyphs[i].source_begin = begin;
        glyphs[i].source_end = end;
      }
    }
    return true;
  }
  return false;
}
bool contains(Reader &reader, std::size_t &offset, unsigned count,
              std::uint16_t gid) noexcept {
  bool found = false;
  for (unsigned i = 0; i < count && reader.status == Status::ok; ++i)
    found = reader.integer(offset, 2) == gid || found;
  return found;
}
unsigned class_id(Reader &reader, std::size_t &offset,
                  std::uint16_t gid) noexcept {
  const auto count = reader.integer(offset, 2);
  unsigned result = 0;
  for (unsigned i = 0; i < count && reader.status == Status::ok; ++i) {
    const auto source = reader.integer(offset, 2),
               assigned = reader.integer(offset, 2);
    if (source == gid)
      result = assigned;
  }
  return result;
}
bool pair(Reader &reader, std::size_t body, unsigned format, Glyph &first,
          Glyph &second) noexcept {
  if (format == 1) {
    const auto count = reader.integer(body, 4);
    for (unsigned row = 0; row < count && reader.status == Status::ok; ++row) {
      const auto left = reader.integer(body, 2),
                 right = reader.integer(body, 2);
      const auto value1 = value_record(reader, body),
                 value2 = value_record(reader, body);
      if (value2.mask) {
        reader.status = Status::unsupported;
        return false;
      }
      if (left == first.gid && right == second.gid) {
        apply(reader, first, value1);
        return true;
      }
      if (left > first.gid || (left == first.gid && right > second.gid))
        break;
    }
  } else {
    const auto coverage = reader.integer(body, 2);
    const bool covered = contains(reader, body, coverage, first.gid);
    const auto classes1 = reader.integer(body, 2),
               classes2 = reader.integer(body, 2);
    const auto left = class_id(reader, body, first.gid),
               right = class_id(reader, body, second.gid);
    if (!covered)
      return false;
    for (unsigned row = 0; row < classes1 && reader.status == Status::ok;
         ++row) {
      for (unsigned column = 0;
           column < classes2 && reader.status == Status::ok; ++column) {
        const auto value1 = value_record(reader, body),
                   value2 = value_record(reader, body);
        if (value2.mask) {
          reader.status = Status::unsupported;
          return false;
        }
        if (row == left && column == right) {
          apply(reader, first, value1);
          return true;
        }
      }
    }
  }
  return false;
}
struct Anchor {
  bool present = false;
  std::int32_t x = 0, y = 0;
};
Anchor anchor(Reader &reader, std::size_t &offset) noexcept {
  Anchor result;
  result.present = reader.integer(offset, 1) != 0;
  if (result.present) {
    result.x = reader.signed16(offset);
    result.y = reader.signed16(offset);
  }
  return result;
}
bool mark_member(Reader &reader, std::size_t body, std::uint16_t gid) noexcept {
  reader.integer(body, 2);
  const auto count = reader.integer(body, 2);
  for (unsigned row = 0; row < count && reader.status == Status::ok; ++row) {
    const auto source = reader.integer(body, 2);
    reader.integer(body, 2);
    anchor(reader, body);
    if (source == gid)
      return true;
    if (source > gid)
      break;
  }
  return false;
}
bool selected_mark(Reader &reader, const Plan &plan,
                   std::uint16_t gid) noexcept {
  for (unsigned reference = 0;
       reference < plan.count && reader.status == Status::ok; ++reference) {
    const auto entry = lookup_entry(reader, plan, reference);
    if (reader.at(entry, 1) != 2 || reader.at(entry + 1, 1) != 4)
      continue;
    const auto subtables = reader.at(entry + 2, 2);
    auto payload = reader.at(entry + 8, 4);
    for (unsigned subtable = 0;
         subtable < subtables && reader.status == Status::ok; ++subtable) {
      if (mark_member(reader, payload + 6, gid))
        return true;
      payload += 6 + reader.at(payload + 2, 4);
    }
  }
  return false;
}
bool attach(Reader &reader, std::size_t body, Glyph *glyphs, std::size_t base,
            std::size_t mark) noexcept {
  const auto classes = reader.integer(body, 2), marks = reader.integer(body, 2);
  bool found = false;
  unsigned mark_class = 0;
  Anchor mark_anchor;
  for (unsigned row = 0; row < marks && reader.status == Status::ok; ++row) {
    const auto gid = reader.integer(body, 2), klass = reader.integer(body, 2);
    const auto current = anchor(reader, body);
    if (gid == glyphs[mark].gid) {
      found = true;
      mark_class = klass;
      mark_anchor = current;
    }
  }
  if (!found)
    return false;
  const auto bases = reader.integer(body, 2);
  for (unsigned row = 0; row < bases && reader.status == Status::ok; ++row) {
    const auto gid = reader.integer(body, 2);
    for (unsigned klass = 0; klass < classes && reader.status == Status::ok;
         ++klass) {
      const auto base_anchor = anchor(reader, body);
      if (gid != glyphs[base].gid || klass != mark_class ||
          !base_anchor.present)
        continue;
      std::int64_t distance_x = 0, distance_y = 0;
      for (auto i = base; i < mark; ++i) {
        if (!reader.charge(1))
          return false;
        distance_x += glyphs[i].advance_x;
        distance_y += glyphs[i].advance_y;
      }
      // Attachment replaces a mark's placement relative to the base
      // origin.
      glyphs[mark].offset_x = 0;
      glyphs[mark].offset_y = 0;
      add(reader, glyphs[mark].offset_x,
          std::int64_t(glyphs[base].offset_x) - distance_x + base_anchor.x -
              mark_anchor.x);
      add(reader, glyphs[mark].offset_y,
          std::int64_t(glyphs[base].offset_y) - distance_y + base_anchor.y -
              mark_anchor.y);
      return true;
    }
  }
  return false;
}
void execute(Reader &reader, const Plan &plan, bool positioning, Glyph *glyphs,
             std::size_t &count) noexcept {
  for (unsigned reference = 0;
       reference < plan.count && reader.status == Status::ok; ++reference) {
    const auto entry = lookup_entry(reader, plan, reference);
    const auto table = reader.at(entry, 1), type = reader.at(entry + 1, 1),
               subtables = reader.at(entry + 2, 2);
    if (table != (positioning ? 2U : 1U))
      continue;
    for (std::size_t index = 0; index < count && reader.status == Status::ok;
         ++index) {
      if (!reader.charge(1))
        return;
      std::size_t base = index;
      if (positioning && type == 4) {
        if (!selected_mark(reader, plan, glyphs[index].gid) || !index)
          continue;
        base = index - 1;
        while (base && selected_mark(reader, plan, glyphs[base].gid))
          --base;
      }
      auto payload = reader.at(entry + 8, 4);
      for (unsigned subtable = 0;
           subtable < subtables && reader.status == Status::ok; ++subtable) {
        const auto format = reader.at(payload + 1, 1),
                   size = reader.at(payload + 2, 4);
        const std::size_t body = payload + 6;
        bool matched = false;
        if (!positioning && type == 1)
          matched = single(reader, body, glyphs[index]);
        if (!positioning && type == 4)
          matched = ligature(reader, body, glyphs, index, count);
        if (positioning && type == 2 && index + 1 < count)
          matched =
              pair(reader, body, format, glyphs[index], glyphs[index + 1]);
        if (positioning && type == 4)
          matched = attach(reader, body, glyphs, base, index);
        if (matched)
          break;
        payload += 6 + size;
      }
    }
  }
}
bool overlaps(const Glyph *first, std::size_t first_count, const Glyph *second,
              std::size_t second_count) noexcept {
  const auto left = reinterpret_cast<std::uintptr_t>(first),
             right = reinterpret_cast<std::uintptr_t>(second);
  const auto left_size = first_count * sizeof(Glyph),
             right_size = second_count * sizeof(Glyph);
  if (!left_size || !right_size)
    return false;
  return left <= right ? right - left < left_size : left - right < right_size;
}
Status run(bool positioning, std::string_view resource,
           const shaping_resource::Identity &identity, unsigned selection,
           const Glyph *input, std::size_t count, Glyph *scratch,
           std::size_t scratch_capacity, Glyph *output,
           std::size_t output_capacity, std::size_t &output_count,
           Ledger &ledger) noexcept {
  if (selection >= 5 || count > 4096 ||
      (count && (!input || !scratch || !output)))
    return Status::invalid_input;
  if (scratch_capacity < count)
    return Status::capacity;
  const auto output_span = output_capacity < count ? output_capacity : count;
  if (overlaps(input, count, scratch, count) ||
      overlaps(input, count, output, output_span) ||
      overlaps(scratch, count, output, output_span))
    return Status::overlap;
  const auto resource_address =
      reinterpret_cast<std::uintptr_t>(resource.data());
  const auto aliases_resource = [&](const Glyph *span, std::size_t length) {
    const auto address = reinterpret_cast<std::uintptr_t>(span);
    const auto extent = length * sizeof(Glyph);
    if (!extent || resource.empty())
      return false;
    return address <= resource_address
               ? resource_address - address < extent
               : address - resource_address < resource.size();
  };
  if (aliases_resource(scratch, count) || aliases_resource(output, output_span))
    return Status::overlap;
  shaping_resource::View validated;
  const auto validation = shaping_resource::validate(
      resource, identity, shaping_resource::Limits{}, ledger.work, validated);
  if (validation != shaping_resource::Status::ok)
    return validation == shaping_resource::Status::limit
               ? Status::limit
               : Status::invalid_resource;
  Reader reader{resource, ledger};
  for (std::size_t i = 0; i < count; ++i) {
    if (!reader.charge(1))
      return reader.status;
    const auto &glyph = input[i];
    if (!glyph.gid || glyph.gid >= identity.glyph_count ||
        glyph.source_begin >= glyph.source_end)
      return Status::invalid_input;
    if (i && (glyph.source_begin < input[i - 1].source_begin ||
              glyph.source_end < input[i - 1].source_end ||
              glyph.source_begin > input[i - 1].source_end ||
              (glyph.source_begin < input[i - 1].source_end &&
               (glyph.source_begin != input[i - 1].source_begin ||
                glyph.source_end != input[i - 1].source_end))))
      return Status::invalid_input;
    scratch[i] = glyph;
    if (!positioning)
      scratch[i].advance_x = scratch[i].advance_y = scratch[i].offset_x =
          scratch[i].offset_y = 0;
  }
  // Validate every selected pair record before applying any positioning.
  // A match in an early row must not hide unsupported second-glyph values.
  if (positioning) {
    const auto selected = plan(reader, selection);
    for (unsigned ref = 0; ref < selected.count && reader.status == Status::ok;
         ++ref) {
      const auto entry = lookup_entry(reader, selected, ref);
      if (reader.at(entry, 1) != 2 || reader.at(entry + 1, 1) != 2)
        continue;
      const auto subtables = reader.at(entry + 2, 2);
      auto payload = reader.at(entry + 8, 4);
      for (unsigned sub = 0; sub < subtables && reader.status == Status::ok;
           ++sub) {
        const auto format = reader.at(payload + 1, 1),
                   size = reader.at(payload + 2, 4);
        std::size_t body = payload + 6;
        std::uint64_t records = 0;
        if (format == 1)
          records = reader.integer(body, 4);
        else {
          const auto coverage = reader.integer(body, 2);
          contains(reader, body, coverage, 0);
          const auto rows = reader.integer(body, 2),
                     columns = reader.integer(body, 2);
          class_id(reader, body, 0);
          class_id(reader, body, 0);
          records = std::uint64_t(rows) * columns;
        }
        for (std::uint64_t row = 0;
             row < records && reader.status == Status::ok; ++row) {
          if (format == 1) {
            reader.integer(body, 2);
            reader.integer(body, 2);
          }
          value_record(reader, body);
          if (value_record(reader, body).mask)
            reader.status = Status::unsupported;
        }
        payload += 6 + size;
      }
    }
    if (reader.status != Status::ok)
      return reader.status;
  }
  auto result_count = count;
  const auto selected = plan(reader, selection);
  execute(reader, selected, positioning, scratch, result_count);
  if (reader.status != Status::ok)
    return reader.status;
  if (result_count > output_capacity)
    return Status::capacity;
  if (result_count > ledger.output_glyphs)
    return Status::limit;
  ledger.output_glyphs -= result_count;
  if (!reader.charge(result_count))
    return reader.status;
  for (std::size_t i = 0; i < result_count; ++i)
    output[i] = scratch[i];
  output_count = result_count;
  return Status::ok;
}
} // namespace
static Status admitted_run(bool positioning,
                           const shaping_catalog::View &catalog,
                           unsigned selection, const Glyph *input,
                           std::size_t count, Glyph *scratch,
                           std::size_t scratch_capacity, Glyph *output,
                           std::size_t output_capacity,
                           std::size_t &output_count, Ledger &ledger) noexcept {
  const auto resource = catalog.companion_bytes();
  if (resource.size() < 112 || (count && !input))
    return Status::invalid_resource;
  if (count > 4096)
    return Status::invalid_input;
  const auto catalog_bytes = catalog.bytes();
  const auto catalog_begin =
      reinterpret_cast<std::uintptr_t>(catalog_bytes.data());
  const auto aliases_catalog = [&](const Glyph *span, std::size_t length) {
    const auto address = reinterpret_cast<std::uintptr_t>(span);
    const auto extent = length * sizeof(Glyph);
    if (!extent || catalog_bytes.empty())
      return false;
    return address <= catalog_begin
               ? catalog_begin - address < extent
               : address - catalog_begin < catalog_bytes.size();
  };
  if (aliases_catalog(scratch, count) ||
      aliases_catalog(output,
                      output_capacity < count ? output_capacity : count))
    return Status::overlap;
  for (std::size_t i = 0; i < count; ++i) {
    // Each binary search is bounded by the 16-bit GID directory ceiling.
    if (ledger.work < 17) {
      ledger.work = 0;
      return Status::limit;
    }
    ledger.work -= 17;
    FontGlyphView metric;
    if (!catalog.find_gid(input[i].gid, metric))
      return Status::invalid_input;
  }
  shaping_resource::Identity identity{};
  for (unsigned i = 0; i < 32; ++i) {
    identity.source[i] = static_cast<unsigned char>(resource[16 + i]);
    identity.profile[i] = static_cast<unsigned char>(resource[48 + i]);
  }
  for (unsigned i = 0; i < 4; ++i)
    identity.glyph_count |=
        std::uint32_t(static_cast<unsigned char>(resource[92 + i])) << (8 * i);
  return run(positioning, resource, identity, selection, input, count, scratch,
             scratch_capacity, output, output_capacity, output_count, ledger);
}
Status substitute(const shaping_catalog::View &catalog, unsigned selection,
                  const Glyph *input, std::size_t count, Glyph *scratch,
                  std::size_t scratch_capacity, Glyph *output,
                  std::size_t output_capacity, std::size_t &output_count,
                  Ledger &ledger) noexcept {
  return admitted_run(false, catalog, selection, input, count, scratch,
                      scratch_capacity, output, output_capacity, output_count,
                      ledger);
}
Status position(const shaping_catalog::View &catalog, unsigned selection,
                const Glyph *input, std::size_t count, Glyph *scratch,
                std::size_t scratch_capacity, Glyph *output,
                std::size_t output_capacity, std::size_t &output_count,
                Ledger &ledger) noexcept {
  return admitted_run(true, catalog, selection, input, count, scratch,
                      scratch_capacity, output, output_capacity, output_count,
                      ledger);
}
} // namespace shaping_lookup
} // namespace jellyframe
