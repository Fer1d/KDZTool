#ifndef PARTITION_LAYOUT_HPP
#define PARTITION_LAYOUT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "dz_parser.hpp"

// Naming and splitting of the files an extraction writes.
//
// LG stores a dense partition as one contiguous run of chunks and a sparse one
// (userdata is the typical example) as a few small chunks scattered over the whole
// partition. Writing one image per partition would create a 10 GiB file that is
// empty almost everywhere, so a partition whose data covers less than half of its
// extent is written as one small file per chunk instead - the layout the reference
// 9008 packages use - while a dense partition becomes a single <partition>.img.
//
// LUN 0 files carry no prefix at all; the other LUNs use the letter prefix the
// reference tools use (LUN 1 -> "B.", LUN 2 -> "C.", ...), which is what keeps the
// seven PrimaryGPT/BackupGPT images apart.

struct PartitionRun {
    uint64_t start_sector = 0;   // first sector on the device
    uint64_t sectors = 0;        // sectors the run covers
    std::string file_name;       // file holding the run
    std::size_t chunk_index = 0; // chunk this run was made from (sparse layout)
};

struct PartitionLayout {
    bool dense = true;           // one image for the whole partition
    uint64_t base_sector = 0;    // sector the image starts at
    uint64_t total_sectors = 0;  // extent of the image
    std::vector<PartitionRun> runs;
};

inline uint64_t chunk_data_sectors(const DzHeader::Chunk& chunk, uint32_t sector_size) {
    return (static_cast<uint64_t>(chunk.data_size) + sector_size - 1) / sector_size;
}

inline std::string lun_prefix(uint32_t lun) {
    if (lun == 0) return std::string();
    return std::string(1, static_cast<char>('A' + lun)) + ".";
}

inline std::string partition_image_name(uint32_t lun, const std::string& partition) {
    return lun_prefix(lun) + partition + ".img";
}

inline std::string chunk_file_name(uint32_t lun, const std::string& chunk_name) {
    return lun_prefix(lun) + chunk_name;
}

inline PartitionLayout compute_partition_layout(const std::vector<DzHeader::Chunk>& chunks,
                                                uint32_t sector_size, uint32_t lun,
                                                const std::string& partition) {
    PartitionLayout layout;
    if (chunks.empty() || sector_size == 0) return layout;

    layout.base_sector = chunks.front().part_start_sector;

    uint64_t data_sectors = 0;
    uint64_t highest_end = layout.base_sector;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const uint64_t sectors = chunk_data_sectors(chunks[i], sector_size);
        data_sectors += sectors;
        const uint64_t end = static_cast<uint64_t>(chunks[i].start_sector) + sectors;
        if (end > highest_end) highest_end = end;
    }

    layout.total_sectors = (highest_end > layout.base_sector) ? highest_end - layout.base_sector : 0;
    layout.dense = (layout.total_sectors == 0) || (data_sectors * 2 >= layout.total_sectors);

    if (layout.dense) {
        PartitionRun run;
        run.start_sector = layout.base_sector;
        run.sectors = layout.total_sectors;
        run.file_name = partition_image_name(lun, partition);
        layout.runs.push_back(run);
    } else {
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            const uint64_t sectors = chunk_data_sectors(chunks[i], sector_size);
            if (sectors == 0) continue;
            PartitionRun run;
            run.start_sector = chunks[i].start_sector;
            run.sectors = sectors;
            run.file_name = chunk_file_name(lun, chunks[i].name);
            run.chunk_index = i;
            layout.runs.push_back(run);
        }
    }
    return layout;
}

// ---------------------------------------------------------------------------
// A/B slots
//
// An A/B device stores both slots as separate partitions (X_a and X_b). Most of
// them hold exactly the same bytes, and LG's own packages do not ship the image
// twice: the B entry points at the image of the A slot. A 9008 package does the
// same and does not extract the B partitions at all, unless --keep-b asks for a
// complete extraction.
// ---------------------------------------------------------------------------

using DzParts = std::vector<std::pair<uint32_t, std::vector<std::pair<std::string, std::vector<DzHeader::Chunk>>>>>;

// "system_b" -> "system_a"
inline bool is_b_slot(const std::string& name, std::string& a_slot) {
    if (name.size() < 3u) return false;
    if (name.compare(name.size() - 2, 2, "_b") != 0) return false;
    a_slot = name.substr(0, name.size() - 2) + "_a";
    return true;
}

inline const std::vector<DzHeader::Chunk>* find_partition_chunks(const DzParts& parts, uint32_t lun,
                                                                const std::string& name) {
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].first != lun) continue;
        for (std::size_t k = 0; k < parts[i].second.size(); ++k) {
            if (parts[i].second[k].first == name) return &parts[i].second[k].second;
        }
    }
    return nullptr;
}

inline uint64_t gpt_partition_sectors(const std::optional<GptInfo>& gpt, const std::string& name) {
    if (!gpt.has_value()) return 0;
    for (std::size_t i = 0; i < gpt->entries.size(); ++i) {
        const GptPartitionEntry& entry = gpt->entries[i];
        if (!entry.empty && entry.name == name && entry.end_lba >= entry.start_lba) {
            return entry.end_lba - entry.start_lba + 1;
        }
    }
    return 0;
}

// The chunk headers carry the MD5 of every chunk, so two partitions that are cut
// the same way can be compared without reading a single byte of data. The A and B
// slot live at different places on the device, so the offsets are compared
// relative to the start of their own partition.
inline bool same_chunk_signature(const std::vector<DzHeader::Chunk>& a,
                                 const std::vector<DzHeader::Chunk>& b) {
    if (a.size() != b.size() || a.empty()) return false;
    const uint64_t a_base = a.front().part_start_sector;
    const uint64_t b_base = b.front().part_start_sector;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if ((uint64_t)a[i].start_sector - a_base != (uint64_t)b[i].start_sector - b_base) return false;
        if (a[i].data_size != b[i].data_size) return false;
        if (a[i].hash != b[i].hash) return false;
    }
    return true;
}

// The same layout, moved to another partition: the files stay the ones of the A
// slot, only the sector the data lands on changes.
inline PartitionLayout mirror_layout(const PartitionLayout& source, uint64_t new_base) {
    PartitionLayout layout = source;
    for (std::size_t i = 0; i < layout.runs.size(); ++i) {
        layout.runs[i].start_sector = new_base + (layout.runs[i].start_sector - source.base_sector);
    }
    layout.base_sector = new_base;
    return layout;
}

struct SlotDecision {
    bool reuse_a = false;          // flash this partition from the A slot image
    bool identical = true;         // the data the DZ stores for it matches the A slot
    std::string a_name;            // the A partition the data comes from
    PartitionLayout layout;        // layout to describe it with (A files, B position)
};

inline SlotDecision decide_slot(const DzParts& parts, uint32_t lun, const std::string& name,
                                uint32_t sector_size, bool flash_layout, bool keep_b,
                                const std::optional<GptInfo>& gpt) {
    SlotDecision decision;
    if (!flash_layout || keep_b) return decision;

    std::string a_name;
    if (!is_b_slot(name, a_name)) return decision;

    const std::vector<DzHeader::Chunk>* a_chunks = find_partition_chunks(parts, lun, a_name);
    const std::vector<DzHeader::Chunk>* b_chunks = find_partition_chunks(parts, lun, name);
    if (a_chunks == nullptr || b_chunks == nullptr || a_chunks->empty() || b_chunks->empty()) return decision;

    const PartitionLayout a_layout = compute_partition_layout(*a_chunks, sector_size, lun, a_name);
    if (a_layout.runs.empty() || a_layout.total_sectors == 0) return decision;

    // Flashing the A image into the B slot must not run past the B partition.
    const uint64_t b_sectors = gpt_partition_sectors(gpt, name);
    if (b_sectors != 0 && b_sectors < a_layout.total_sectors) return decision;

    decision.reuse_a = true;
    decision.a_name = a_name;
    decision.layout = mirror_layout(a_layout, b_chunks->front().part_start_sector);
    decision.identical = same_chunk_signature(*a_chunks, *b_chunks);
    return decision;
}

#endif // PARTITION_LAYOUT_HPP
