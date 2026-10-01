#include "rawprogram_generator.hpp"
#include "partition_layout.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "md5.hpp"

namespace fs = std::filesystem;

namespace {

// One <program> element: a run of a partition that is written from one file.
struct ProgramEntry {
    std::string label;
    std::string file;
    uint32_t hw_partition = 0;
    uint64_t start_sector = 0;
    uint64_t sectors = 0;
    bool sparse = false;             // the file really is an Android sparse image
    uint64_t expanded_sectors = 0;   // what that sparse image expands to
};

struct PatchEntry {
    std::string filename;
    uint32_t hw_partition = 0;
    uint64_t start_sector = 0;
    uint64_t byte_offset = 0;
    uint32_t size_in_bytes = 0;
    std::string value;
    std::string what;
};

// A partition as it was extracted, including the files its data was written to.
struct PartitionInfo {
    uint32_t lun = 0;
    std::string name;
    PartitionLayout layout;
};

std::string xml_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

std::string hex_value(uint64_t value) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << value;
    return oss.str();
}

std::string lower_case(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

uint64_t size_in_kb(uint64_t sectors, uint32_t sector_size) {
    return (sectors * sector_size) / 1024;
}

std::vector<PartitionInfo> collect_partitions(const DzHeader& dz_hdr, uint32_t sector_size) {
    std::vector<PartitionInfo> partitions;
    for (const auto& hw_pair : dz_hdr.parts) {
        for (const auto& name_pair : hw_pair.second) {
            if (name_pair.second.empty()) continue;
            PartitionInfo info;
            info.lun = hw_pair.first;
            info.name = name_pair.first;
            info.layout = compute_partition_layout(name_pair.second, sector_size, info.lun, info.name);
            partitions.push_back(info);
        }
    }
    return partitions;
}

// The Android sparse magic, so an image that really is sparse can say so; the
// is_sparse flag of a chunk header only means that the partition is stored with
// holes, which is why it is not used here.
void probe_sparse_image(const fs::path& path, ProgramEntry& entry) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return;

    unsigned char header[20] = {0};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    if (in.gcount() < static_cast<std::streamsize>(sizeof(header))) return;

    const uint32_t magic = static_cast<uint32_t>(header[0]) |
                           (static_cast<uint32_t>(header[1]) << 8) |
                           (static_cast<uint32_t>(header[2]) << 16) |
                           (static_cast<uint32_t>(header[3]) << 24);
    if (magic != 0xED26FF3Au) return;

    const uint32_t block_size = static_cast<uint32_t>(header[12]) |
                                (static_cast<uint32_t>(header[13]) << 8) |
                                (static_cast<uint32_t>(header[14]) << 16) |
                                (static_cast<uint32_t>(header[15]) << 24);
    const uint32_t total_blocks = static_cast<uint32_t>(header[16]) |
                                  (static_cast<uint32_t>(header[17]) << 8) |
                                  (static_cast<uint32_t>(header[18]) << 16) |
                                  (static_cast<uint32_t>(header[19]) << 24);
    entry.sparse = true;
    entry.expanded_sectors = (static_cast<uint64_t>(total_blocks) * block_size) / 4096;
}

std::string file_md5(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();

    MD5 md5;
    std::vector<char> buffer(1 << 20);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) md5.update(buffer.data(), static_cast<MD5::size_type>(got));
    }
    md5.finalize();
    return md5.hexdigest();
}

void write_rawprogram(const fs::path& path, uint32_t hw_partition,
                      const std::vector<ProgramEntry>& entries, uint32_t sector_size,
                      uint64_t disk_sectors) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("failed to open " + path.string());

    out << "<?xml version=\"1.0\" ?>\n";
    out << "<data>\n";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ProgramEntry& entry = entries[i];

        // A run that ends exactly at the end of the disk has to move when the
        // device offers more capacity - that is what the "grow" partition is for -
        // so its position is written relative to NUM_DISK_SECTORS. On a device that
        // matches the firmware the expression evaluates to the same value a literal
        // would have, exactly like Qualcomm's own rawprogram templates.
        const bool at_disk_end = disk_sectors != 0 &&
                                 entry.start_sector + entry.sectors == disk_sectors;

        const uint64_t image_sectors = (entry.sparse && entry.expanded_sectors != 0)
                                           ? entry.expanded_sectors
                                           : entry.sectors;

        std::string start_sector;
        std::string start_byte_hex;
        if (at_disk_end) {
            start_sector = "NUM_DISK_SECTORS-" + std::to_string(entry.sectors);
            start_byte_hex = "(" + std::to_string(sector_size) + "*NUM_DISK_SECTORS)-" +
                             std::to_string(entry.sectors * sector_size);
        } else {
            start_sector = std::to_string(entry.start_sector);
            start_byte_hex = hex_value(entry.start_sector * sector_size);
        }

        out << "\t<program"
            << " SECTOR_SIZE_IN_BYTES=\"" << sector_size << "\""
            << " file_sector_offset=\"0\""
            << " filename=\"" << xml_escape(entry.file) << "\""
            << " label=\"" << xml_escape(entry.label) << "\""
            << " num_partition_sectors=\"" << image_sectors << "\""
            << " physical_partition_number=\"" << hw_partition << "\""
            << " size_in_KB=\"" << size_in_kb(image_sectors, sector_size) << "\""
            << " sparse=\"" << (entry.sparse ? "true" : "false") << "\""
            << " start_byte_hex=\"" << start_byte_hex << "\""
            << " start_sector=\"" << start_sector << "\""
            << " />\n";
    }
    out << "</data>\n";
}

void write_patches(const fs::path& path, const std::vector<PatchEntry>& patches, uint32_t sector_size) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("failed to open " + path.string());

    out << "<?xml version=\"1.0\" ?>\n";
    out << "<patches>\n";
    for (std::size_t i = 0; i < patches.size(); ++i) {
        const PatchEntry& patch = patches[i];
        out << "\t<patch"
            << " SECTOR_SIZE_IN_BYTES=\"" << sector_size << "\""
            << " byte_offset=\"" << patch.byte_offset << "\""
            << " filename=\"" << xml_escape(patch.filename) << "\""
            << " physical_partition_number=\"" << patch.hw_partition << "\""
            << " size_in_bytes=\"" << patch.size_in_bytes << "\""
            << " start_sector=\"" << patch.start_sector << "\""
            << " value=\"" << xml_escape(patch.value) << "\""
            << " what=\"" << xml_escape(patch.what) << "\""
            << " />\n";
    }
    out << "</patches>\n";
}

const PartitionInfo* find_partition(const std::vector<PartitionInfo>& partitions, uint32_t lun,
                                    const std::string& name) {
    for (std::size_t i = 0; i < partitions.size(); ++i) {
        if (partitions[i].lun == lun && partitions[i].name == name) return &partitions[i];
    }
    return nullptr;
}

const PartitionInfo* find_backup_partition(const std::vector<PartitionInfo>& partitions,
                                           const GptInfo& gpt) {
    const PartitionInfo* fallback = nullptr;
    for (std::size_t i = 0; i < partitions.size(); ++i) {
        const PartitionInfo& info = partitions[i];
        if (info.lun != gpt.owner_hw_partition) continue;
        if (info.name == gpt.owner_partition_name) continue;

        const std::string lower = lower_case(info.name);
        if (lower.find("gpt") == std::string::npos) continue;
        if (lower.find("backup") != std::string::npos) return &info;
        if (fallback == nullptr || info.layout.base_sector > fallback->layout.base_sector) fallback = &info;
    }
    return fallback;
}

// Builds the patch entries that resize the last partition to the real disk.
// Values that depend on the target disk use the NUM_DISK_SECTORS placeholder,
// which QFIL substitutes while flashing.
std::vector<PatchEntry> build_patches(const DzHeader& dz_hdr,
                                      const std::vector<PartitionInfo>& partitions,
                                      Diagnostics& diag) {
    std::vector<PatchEntry> patches;
    if (!dz_hdr.gpt_info().has_value()) {
        diag.warn("patch", "no GPT was found in the DZ archive, so no patch entries were generated");
        return patches;
    }

    const GptInfo& gpt = dz_hdr.gpt_info().value();
    if (gpt.entries.empty()) {
        diag.warn("patch", "the GPT partition entry array was not available, so no patch entries were generated");
        return patches;
    }

    std::size_t grow_index = 0;
    if (!gpt.find_grow_entry(grow_index)) {
        diag.info("patch", "no partition ends at the last usable LBA, so the GPT already matches the disk size");
        return patches;
    }

    const PartitionInfo* owner = find_partition(partitions, gpt.owner_hw_partition, gpt.owner_partition_name);
    if (owner == nullptr) {
        diag.warn("patch", "the partition holding the primary GPT could not be located, so no patch entries were generated");
        return patches;
    }
    if (!owner->layout.dense) {
        diag.warn("patch", "the partition holding the primary GPT was extracted as chunk files, so no patch entries were generated");
        return patches;
    }
    if (owner->layout.base_sector > 1 || gpt.entry_lba < owner->layout.base_sector) {
        diag.warn("patch", "the primary GPT does not start at the beginning of its partition, so no patch entries were generated");
        return patches;
    }

    // The GPT states where the disk ends (the backup header sits in the last
    // sector) and where the last usable sector is; the difference is the area the
    // firmware reserves at the end for the backup GPT. Written relative to
    // NUM_DISK_SECTORS the patch stays correct on a device with more capacity.
    if (gpt.alt_lba == 0 || gpt.last_usable_lba >= gpt.alt_lba + 1) {
        diag.warn("patch", "the GPT does not describe the end of the disk, so no patch entries were generated");
        return patches;
    }
    const uint64_t tail_reserved = gpt.alt_lba + 1 - gpt.last_usable_lba;
    if (tail_reserved < 2) {
        diag.warn("patch", "the GPT leaves no room for a backup table, so no patch entries were generated");
        return patches;
    }
    const std::string last_usable = "NUM_DISK_SECTORS-" + std::to_string(tail_reserved);

    const std::string owner_file = owner->layout.runs.front().file_name;
    const uint64_t owner_base = owner->layout.base_sector;

    auto add_patch = [&patches, &gpt](const std::string& file, uint32_t lun, uint64_t byte_in_image,
                                      uint32_t size_in_bytes, const std::string& value,
                                      const std::string& what) {
        PatchEntry entry;
        entry.filename = file;
        entry.hw_partition = lun;
        entry.start_sector = byte_in_image / gpt.sector_size;
        entry.byte_offset = byte_in_image % gpt.sector_size;
        entry.size_in_bytes = size_in_bytes;
        entry.value = value;
        entry.what = what;
        patches.push_back(entry);
    };

    const std::string grow_name = gpt.entries[grow_index].name.empty()
                                      ? ("partition " + std::to_string(grow_index))
                                      : gpt.entries[grow_index].name;

    add_patch(owner_file, owner->lun, (1 - owner_base) * gpt.sector_size + 48, 8, last_usable,
              "Update LastUsableLBA in the primary GPT header");
    add_patch(owner_file, owner->lun,
              (gpt.entry_lba - owner_base) * gpt.sector_size + grow_index * gpt.entry_size + 40,
              8, last_usable,
              "Update the last partition '" + grow_name + "' with its actual size in the primary GPT");

    const PartitionInfo* backup = find_backup_partition(partitions, gpt);
    if (backup != nullptr && backup->layout.dense) {
        // The backup header is the last sector of its partition image and the entry
        // array starts right after the last usable sector.
        const uint64_t header_byte = (backup->layout.total_sectors - 1) * gpt.sector_size;
        const uint64_t entries_lba = gpt.last_usable_lba + 1;
        const uint64_t entries_byte = (entries_lba >= backup->layout.base_sector)
                                          ? (entries_lba - backup->layout.base_sector) * gpt.sector_size
                                          : 0;
        const std::string backup_file = backup->layout.runs.front().file_name;

        add_patch(backup_file, backup->lun, header_byte + 24, 8, "NUM_DISK_SECTORS-1",
                  "Update MyLBA in the backup GPT header");
        add_patch(backup_file, backup->lun, header_byte + 48, 8, last_usable,
                  "Update LastUsableLBA in the backup GPT header");
        add_patch(backup_file, backup->lun, header_byte + 72, 8,
                  "NUM_DISK_SECTORS-" + std::to_string(tail_reserved - 1),
                  "Update PartitionEntryLBA in the backup GPT header");
        add_patch(backup_file, backup->lun,
                  entries_byte + grow_index * gpt.entry_size + 40, 8, last_usable,
                  "Update the last partition '" + grow_name + "' with its actual size in the backup GPT");
    } else {
        diag.info("patch", "the DZ archive carries no separate backup GPT partition");
    }

    diag.info("patch", "generated " + std::to_string(patches.size()) +
                           " patch entries for partition '" + grow_name + "'");
    return patches;
}

} // namespace

void generate_rawprogram_files(const std::string& out_dir, const DzHeader& dz_hdr, Diagnostics& diag) {
    const uint32_t sector_size = static_cast<uint32_t>(dz_hdr.sector_size());
    if (sector_size == 0) {
        diag.warn("rawprogram", "the sector size is unknown, so no rawprogram files were generated");
        return;
    }

    const std::vector<PartitionInfo> partitions = collect_partitions(dz_hdr, sector_size);
    if (partitions.empty()) {
        diag.warn("rawprogram", "the DZ archive contains no partitions to describe");
        return;
    }

    // One entry per run: a dense partition is a single entry, a sparse one is one
    // entry per chunk file.
    std::vector<ProgramEntry> entries;
    for (std::size_t i = 0; i < partitions.size(); ++i) {
        for (std::size_t r = 0; r < partitions[i].layout.runs.size(); ++r) {
            const PartitionRun& run = partitions[i].layout.runs[r];
            if (run.sectors == 0) continue;
            ProgramEntry entry;
            entry.label = partitions[i].name;
            entry.file = run.file_name;
            entry.hw_partition = partitions[i].lun;
            entry.start_sector = run.start_sector;
            entry.sectors = run.sectors;
            entries.push_back(entry);
        }
    }
    if (entries.empty()) {
        diag.warn("rawprogram", "no partition data was found to describe");
        return;
    }

    // A file that is missing, or an image that really is an Android sparse image.
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const fs::path path = fs::path(out_dir) / entries[i].file;
        if (!fs::exists(path)) {
            diag.warn("rawprogram", "the file " + entries[i].file + " was not extracted");
            continue;
        }
        probe_sparse_image(path, entries[i]);
    }

    // Files that hold exactly the same bytes are written once and referenced from
    // every entry that needs them: LG ships one image for the A and B slot, so the
    // package would otherwise carry the same 2 GiB twice. Only files of equal size
    // are compared, which keeps the hashing cheap.
    std::map<uint64_t, std::vector<std::size_t>> by_size;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::error_code ec;
        const uint64_t size = fs::file_size(fs::path(out_dir) / entries[i].file, ec);
        if (ec) continue;
        by_size[size].push_back(i);
    }
    std::map<std::string, std::string> file_by_hash;
    std::size_t shared = 0;
    for (auto& group : by_size) {
        std::map<std::string, std::size_t> first_of_hash;
        for (std::size_t k = 0; k < group.second.size(); ++k) {
            const std::size_t index = group.second[k];
            const fs::path path = fs::path(out_dir) / entries[index].file;
            const std::string hash = file_md5(path);
            if (hash.empty()) continue;

            auto it = first_of_hash.find(hash);
            if (it == first_of_hash.end()) {
                first_of_hash.emplace(hash, index);
            } else if (entries[index].file != entries[it->second].file) {
                entries[index].file = entries[it->second].file;
                ++shared;
            }
        }
    }
    if (shared > 0) {
        diag.info("rawprogram", std::to_string(shared) +
                                   " entry/entries share a file with an identical counterpart");
    }

    std::vector<uint32_t> luns;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (std::find(luns.begin(), luns.end(), entries[i].hw_partition) == luns.end()) {
            luns.push_back(entries[i].hw_partition);
        }
    }
    std::sort(luns.begin(), luns.end());

    const std::vector<PatchEntry> patches = build_patches(dz_hdr, partitions, diag);
    const uint32_t patch_lun = dz_hdr.gpt_info().has_value() ? dz_hdr.gpt_info()->owner_hw_partition : 0;
    const uint64_t disk_sectors =
        dz_hdr.gpt_info().has_value() ? dz_hdr.gpt_info()->alt_lba + 1 : 0;

    std::cout << "Generating 9008/EDL flashing metadata..." << std::endl;
    for (std::size_t i = 0; i < luns.size(); ++i) {
        const uint32_t lun = luns[i];

        std::vector<ProgramEntry> lun_entries;
        for (std::size_t k = 0; k < entries.size(); ++k) {
            if (entries[k].hw_partition == lun) lun_entries.push_back(entries[k]);
        }

        const fs::path raw_path = fs::path(out_dir) / ("rawprogram" + std::to_string(lun) + ".xml");
        const fs::path patch_path = fs::path(out_dir) / ("patch" + std::to_string(lun) + ".xml");

        try {
            write_rawprogram(raw_path, lun, lun_entries, sector_size, disk_sectors);
            std::cout << "  " << raw_path.filename().string() << ": "
                      << lun_entries.size() << " program entries" << std::endl;

            if (lun == patch_lun) {
                write_patches(patch_path, patches, sector_size);
            } else {
                write_patches(patch_path, std::vector<PatchEntry>(), sector_size);
            }
            std::cout << "  " << patch_path.filename().string() << ": "
                      << ((lun == patch_lun) ? patches.size() : 0) << " patch entries" << std::endl;
        } catch (const std::exception& e) {
            diag.warn("rawprogram", std::string("could not write the XML files for physical partition ") +
                                       std::to_string(lun) + ": " + e.what());
        }
    }
}
