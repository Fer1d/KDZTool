#include "gpt.hpp"

#include <zlib.h>
#include <algorithm>
#include <cstring>

namespace {

constexpr std::size_t GPT_HEADER_MIN_SIZE = 92;
constexpr uint32_t GPT_MIN_SHIFT = 9;
constexpr uint32_t GPT_MAX_SHIFT = 16;

uint32_t read_u32(const char* p) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(p);
    return static_cast<uint32_t>(b[0]) |
           (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) |
           (static_cast<uint32_t>(b[3]) << 24);
}

uint64_t read_u64(const char* p) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(p);
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | b[i];
    }
    return value;
}

bool parse_entries(const char* data, std::size_t size, std::size_t offset,
                   uint32_t count, uint32_t entry_size, GptInfo& out) {
    if (count == 0 || entry_size < 128) return false;
    const uint64_t total = static_cast<uint64_t>(count) * entry_size;
    if (offset + total > size) return false;

    out.entries.clear();
    std::size_t i = 0;
    while (i < count) {
        const char* raw = data + offset + i * entry_size;
        GptPartitionEntry entry;

        bool all_zero = true;
        int k = 0;
        while (k < 16) {
            if (raw[k] != 0) {
                all_zero = false;
                break;
            }
            ++k;
        }
        entry.empty = all_zero;
        entry.start_lba = read_u64(raw + 32);
        entry.end_lba = read_u64(raw + 40);

        // The name is UTF-16LE in bytes 56..127; keep the printable ASCII subset.
        std::string name;
        int c = 0;
        while (c < 36) {
            const unsigned char lo = static_cast<unsigned char>(raw[56 + c * 2]);
            const unsigned char hi = static_cast<unsigned char>(raw[57 + c * 2]);
            if (lo == 0 && hi == 0) break;
            if (hi == 0 && lo >= 32 && lo < 127) name.push_back(static_cast<char>(lo));
            ++c;
        }
        entry.name = name;

        out.entries.push_back(entry);
        ++i;
    }
    return true;
}

bool parse_header(const char* header, std::size_t available, uint32_t shift,
                  bool from_backup, GptInfo& out) {
    if (available < GPT_HEADER_MIN_SIZE) return false;
    if (std::memcmp(header, "EFI PART", 8) != 0) return false;

    const uint32_t revision = read_u32(header + 8);
    const uint32_t header_size = read_u32(header + 12);
    const uint32_t stored_crc = read_u32(header + 16);
    if ((revision >> 16) != 1) return false;
    if (header_size < GPT_HEADER_MIN_SIZE || header_size > available) return false;

    std::vector<char> copy(header, header + header_size);
    copy[16] = 0;
    copy[17] = 0;
    copy[18] = 0;
    copy[19] = 0;
    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(copy.data()), static_cast<uInt>(header_size));
    if (crc != stored_crc) return false;

    const uint64_t my_lba = read_u64(header + 24);
    const uint64_t alt_lba = read_u64(header + 32);
    if (my_lba != 1 && alt_lba != 1) return false;

    const uint32_t entry_count = read_u32(header + 80);
    const uint32_t entry_size = read_u32(header + 84);
    if (entry_size < 128 || (entry_size & (entry_size - 1)) != 0) return false;
    if (entry_count == 0 || entry_count > 4096) return false;

    out.sector_size = 1u << shift;
    out.shift = shift;
    out.from_backup = from_backup;
    out.my_lba = my_lba;
    out.alt_lba = alt_lba;
    out.first_usable_lba = read_u64(header + 40);
    out.last_usable_lba = read_u64(header + 48);
    out.entry_lba = read_u64(header + 72);
    out.entry_count = entry_count;
    out.entry_size = entry_size;
    out.header_size = header_size;
    return true;
}

} // namespace

uint32_t GptInfo::backup_sectors() const {
    if (sector_size == 0 || entry_size == 0) return 0;
    const uint64_t entry_bytes = static_cast<uint64_t>(entry_count) * entry_size;
    const uint64_t entry_sectors = (entry_bytes + sector_size - 1) / sector_size;
    return static_cast<uint32_t>(entry_sectors + 1);
}

bool GptInfo::find_grow_entry(std::size_t& index) const {
    std::size_t i = entries.size();
    while (i > 0) {
        --i;
        const GptPartitionEntry& entry = entries[i];
        if (entry.empty) continue;
        if (entry.end_lba == last_usable_lba) {
            index = i;
            return true;
        }
        return false;
    }
    return false;
}

bool probe_gpt(const char* data, std::size_t size, GptInfo& out) {
    if (data == nullptr || size < 1024) return false;

    uint32_t shift = GPT_MIN_SHIFT;
    while (shift <= GPT_MAX_SHIFT) {
        const std::size_t sector = static_cast<std::size_t>(1) << shift;
        GptInfo candidate;

        if (size >= sector + GPT_HEADER_MIN_SIZE &&
            parse_header(data + sector, size - sector, shift, false, candidate)) {
            const uint64_t entry_bytes = static_cast<uint64_t>(candidate.entry_count) * candidate.entry_size;
            const uint64_t entry_offset = candidate.entry_lba << shift;
            if (entry_offset + entry_bytes <= size &&
                parse_entries(data, size, static_cast<std::size_t>(entry_offset),
                              candidate.entry_count, candidate.entry_size, candidate)) {
                out = candidate;
                return true;
            }
            out = candidate;
            return true;
        }

        if (size >= 2 * sector &&
            parse_header(data + size - sector, sector, shift, true, candidate)) {
            const uint64_t entry_bytes = static_cast<uint64_t>(candidate.entry_count) * candidate.entry_size;
            const uint64_t entry_sectors = (entry_bytes + sector - 1) / sector;
            if (static_cast<uint64_t>(entry_sectors) * sector <= size - sector) {
                const std::size_t entry_offset =
                    size - sector - static_cast<std::size_t>(entry_sectors) * sector;
                if (parse_entries(data, size, entry_offset, candidate.entry_count,
                                  candidate.entry_size, candidate)) {
                    out = candidate;
                    return true;
                }
            }
            out = candidate;
            return true;
        }
        ++shift;
    }
    return false;
}
