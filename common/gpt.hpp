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

    uint32_t backup_sectors() const;
    bool find_grow_entry(std::size_t& index) const;
};

bool probe_gpt(const char* data, std::size_t size, GptInfo& out);

#endif // GPT_HPP
