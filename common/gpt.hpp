#ifndef GPT_HPP
#define GPT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct GptPartitionEntry {
    bool empty = true;
    uint64_t start_lba = 0;
    uint64_t end_lba = 0;
    std::string name;
};

struct GptInfo {
    uint32_t sector_size = 0;
    uint32_t shift = 0;
    bool from_backup = false;
    uint64_t my_lba = 0;
    uint64_t alt_lba = 0;
    uint64_t first_usable_lba = 0;
    uint64_t last_usable_lba = 0;
    uint64_t entry_lba = 0;
    uint32_t entry_count = 0;
    uint32_t entry_size = 0;
    uint32_t header_size = 0;
    std::vector<GptPartitionEntry> entries;

    uint32_t owner_hw_partition = 0;
    std::string owner_partition_name;

    bool find_grow_entry(std::size_t& index) const;
};

// Searches a partition image held in a buffer for a GPT header, trying sector
// sizes from 512 to 65536 bytes. The primary position (the second sector of the
// device) and the backup position (the last sector of the image) are checked, and
// base_sector tells the function which LBA the image starts at so that the
// partition entry array can be located. On success the header is validated
// (magic, header CRC32, revision, entry size) and true is returned.
bool probe_gpt(const char* data, std::size_t size, uint64_t base_sector, GptInfo& out);

#endif // GPT_HPP
