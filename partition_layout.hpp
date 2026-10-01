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

#endif // PARTITION_LAYOUT_HPP
