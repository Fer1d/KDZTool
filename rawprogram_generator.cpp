#include "rawprogram_generator.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {

// One whole-partition image, exactly as the extractor writes it.
struct PartitionImage {
    std::string name;
    uint32_t hw_partition = 0;
    uint64_t base_sector = 0;
    uint64_t total_sectors = 0;
    bool sparse = false;             // the image really is an Android sparse image
    uint64_t expanded_sectors = 0;   // what that sparse image expands to
    std::string filename;
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

// Collects the partition images the extractor produced, in the order in which
// they should be written: physical partition first, then start sector.
// Our extractor writes the decompressed payload unchanged, so an image is only a
// sparse image when the payload starts with the Android sparse magic. The
// is_sparse flag of the chunk header means something else (the partition is
// stored with holes), which is why it is not used here.
void probe_sparse_image(const fs::path& path, uint32_t sector_size, PartitionImage& image) {
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
    image.sparse = true;
    // total_blocks counts blocks of block_size bytes; the entry in the XML counts
    // sectors of the DZ's sector size.
    image.expanded_sectors = (static_cast<uint64_t>(total_blocks) * block_size) / sector_size;
}

std::vector<PartitionImage> collect_partition_images(const DzHeader& dz_hdr, uint32_t sector_size,
                                                     Diagnostics& diag) {
    std::vector<PartitionImage> images;

    for (const auto& hw_pair : dz_hdr.parts) {
        for (const auto& name_pair : hw_pair.second) {
            const std::vector<DzHeader::Chunk>& chunks = name_pair.second;
            if (chunks.empty()) continue;

            PartitionImage image;
            image.name = name_pair.first;
            image.hw_partition = hw_pair.first;
            image.base_sector = chunks.front().part_start_sector;
            image.filename = std::to_string(image.hw_partition) + "." + image.name + ".img";

            // The header's sector_count is a wipe extent, while the payload is
            // data_size bytes long. The image therefore reaches as far as the last
            // chunk's wipe extent and at least as far as the decompressed data of
            // every chunk, exactly like the extractor sizes the file on disk.
            uint64_t declared_end =
                static_cast<uint64_t>(chunks.back().start_sector) + chunks.back().sector_count;
            uint64_t data_end = 0;
            for (std::size_t i = 0; i < chunks.size(); ++i) {
                const uint64_t data_sectors =
                    (static_cast<uint64_t>(chunks[i].data_size) + sector_size - 1) / sector_size;
                const uint64_t chunk_data_end = static_cast<uint64_t>(chunks[i].start_sector) + data_sectors;
                if (chunk_data_end > data_end) data_end = chunk_data_end;
            }
            if (data_end > declared_end) {
                diag.warn("rawprogram", "partition \"" + image.name + "\" carries data up to sector " +
                                         std::to_string(data_end) + " but its last chunk only wipes up to " +
                                         std::to_string(declared_end) + "; using the data extent");
                declared_end = data_end;
            }

            if (declared_end <= image.base_sector) continue;
            image.total_sectors = declared_end - image.base_sector;
            images.push_back(image);
        }
    }

    std::sort(images.begin(), images.end(), [](const PartitionImage& a, const PartitionImage& b) {
        if (a.hw_partition != b.hw_partition) return a.hw_partition < b.hw_partition;
        return a.base_sector < b.base_sector;
    });
    return images;
}

void write_rawprogram(const fs::path& path, uint32_t hw_partition,
                      const std::vector<PartitionImage>& images, uint32_t sector_size,
                      uint64_t disk_sectors) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("failed to open " + path.string());

    out << "<?xml version=\"1.0\" ?>\n";
    out << "<data>\n";
    for (std::size_t i = 0; i < images.size(); ++i) {
        const PartitionImage& image = images[i];

        // A partition that ends exactly at the end of the disk has to move when the
        // device offers more capacity - that is what the "grow" partition is for - so
        // its position is written relative to NUM_DISK_SECTORS. On a device that
        // matches the firmware the expression evaluates to the same value a literal
        // would have, exactly like Qualcomm's own rawprogram templates.
        const bool at_disk_end = disk_sectors != 0 &&
                                 image.base_sector + image.total_sectors == disk_sectors;

        // A sparse image is expanded by the flasher, so its entry describes the
        // expanded partition rather than the file that is on disk.
        const uint64_t image_sectors = (image.sparse && image.expanded_sectors != 0)
                                           ? image.expanded_sectors
                                           : image.total_sectors;

        std::string start_sector;
        std::string start_byte_hex;
        if (at_disk_end) {
            start_sector = "NUM_DISK_SECTORS-" + std::to_string(image.total_sectors);
            start_byte_hex = "(" + std::to_string(sector_size) + "*NUM_DISK_SECTORS)-" +
                             std::to_string(image.total_sectors * sector_size);
        } else {
            start_sector = std::to_string(image.base_sector);
            start_byte_hex = hex_value(image.base_sector * sector_size);
        }

        out << "\t<program"
            << " SECTOR_SIZE_IN_BYTES=\"" << sector_size << "\""
            << " file_sector_offset=\"0\""
            << " filename=\"" << xml_escape(image.filename) << "\""
            << " label=\"" << xml_escape(image.name) << "\""
            << " num_partition_sectors=\"" << image_sectors << "\""
            << " physical_partition_number=\"" << hw_partition << "\""
            << " size_in_KB=\"" << size_in_kb(image_sectors, sector_size) << "\""
            << " sparse=\"" << (image.sparse ? "true" : "false") << "\""
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

} // namespace

namespace {

const PartitionImage* find_backup_image(const std::vector<PartitionImage>& images, const GptInfo& gpt) {
    const PartitionImage* fallback = nullptr;
    for (std::size_t i = 0; i < images.size(); ++i) {
        const PartitionImage& image = images[i];
        if (image.hw_partition != gpt.owner_hw_partition) continue;
        if (image.name == gpt.owner_partition_name) continue;

        const std::string lower = lower_case(image.name);
        if (lower.find("gpt") == std::string::npos) continue;
        if (lower.find("backup") != std::string::npos) return &image;
        if (fallback == nullptr || image.base_sector > fallback->base_sector) fallback = &image;
    }
    return fallback;
}

// Builds the patch entries that resize the last partition to the real disk.
// Values that depend on the target disk use the NUM_DISK_SECTORS placeholder,
// which QFIL substitutes while flashing.
std::vector<PatchEntry> build_patches(const DzHeader& dz_hdr,
                                      const std::vector<PartitionImage>& images,
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

    const PartitionImage* owner = nullptr;
    for (std::size_t i = 0; i < images.size(); ++i) {
        if (images[i].hw_partition == gpt.owner_hw_partition &&
            images[i].name == gpt.owner_partition_name) {
            owner = &images[i];
            break;
        }
    }
    if (owner == nullptr || owner->base_sector > 1 || gpt.entry_lba < owner->base_sector) {
        diag.warn("patch", "the partition holding the primary GPT could not be located, so no patch entries were generated");
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

    auto add_patch = [&patches, &gpt](const PartitionImage& image, uint64_t byte_in_image,
                                      uint32_t size_in_bytes, const std::string& value,
                                      const std::string& what) {
        PatchEntry entry;
        entry.filename = image.filename;
        entry.hw_partition = image.hw_partition;
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

    add_patch(*owner, (1 - owner->base_sector) * gpt.sector_size + 48, 8, last_usable,
              "Update LastUsableLBA in the primary GPT header");
    add_patch(*owner, (gpt.entry_lba - owner->base_sector) * gpt.sector_size +
                          grow_index * gpt.entry_size + 40,
              8, last_usable,
              "Update the last partition '" + grow_name + "' with its actual size in the primary GPT");

    const PartitionImage* backup = find_backup_image(images, gpt);
    if (backup != nullptr) {
        // The backup header is the last sector of its partition image and the
        // entry array starts right after the last usable sector.
        const uint64_t header_byte = (backup->total_sectors - 1) * gpt.sector_size;
        const uint64_t entries_lba = gpt.last_usable_lba + 1;
        const uint64_t entries_byte = (entries_lba >= backup->base_sector)
                                          ? (entries_lba - backup->base_sector) * gpt.sector_size
                                          : 0;

        add_patch(*backup, header_byte + 24, 8, "NUM_DISK_SECTORS-1",
                  "Update MyLBA in the backup GPT header");
        add_patch(*backup, header_byte + 48, 8, last_usable,
                  "Update LastUsableLBA in the backup GPT header");
        add_patch(*backup, header_byte + 72, 8,
                  "NUM_DISK_SECTORS-" + std::to_string(tail_reserved - 1),
                  "Update PartitionEntryLBA in the backup GPT header");
        add_patch(*backup, entries_byte + grow_index * gpt.entry_size + 40, 8, last_usable,
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

    std::vector<PartitionImage> images = collect_partition_images(dz_hdr, sector_size, diag);
    for (std::size_t i = 0; i < images.size(); ++i) {
        probe_sparse_image(fs::path(out_dir) / images[i].filename, sector_size, images[i]);
    }
    if (images.empty()) {
        diag.warn("rawprogram", "the DZ archive contains no partitions to describe");
        return;
    }

    std::vector<uint32_t> luns;
    for (std::size_t i = 0; i < images.size(); ++i) {
        if (std::find(luns.begin(), luns.end(), images[i].hw_partition) == luns.end()) {
            luns.push_back(images[i].hw_partition);
        }
    }
    std::sort(luns.begin(), luns.end());

    const std::vector<PatchEntry> patches = build_patches(dz_hdr, images, diag);
    const uint32_t patch_lun = dz_hdr.gpt_info().has_value() ? dz_hdr.gpt_info()->owner_hw_partition : 0;
    const uint64_t disk_sectors =
        dz_hdr.gpt_info().has_value() ? dz_hdr.gpt_info()->alt_lba + 1 : 0;

    std::cout << "Generating 9008/EDL flashing metadata..." << std::endl;
    for (std::size_t i = 0; i < luns.size(); ++i) {
        const uint32_t lun = luns[i];

        std::vector<PartitionImage> lun_images;
        for (std::size_t k = 0; k < images.size(); ++k) {
            if (images[k].hw_partition == lun) lun_images.push_back(images[k]);
        }

        const fs::path raw_path = fs::path(out_dir) / ("rawprogram" + std::to_string(lun) + ".xml");
        const fs::path patch_path = fs::path(out_dir) / ("patch" + std::to_string(lun) + ".xml");

        try {
            if (disk_sectors != 0) {
                for (std::size_t k = 0; k < lun_images.size(); ++k) {
                    if (lun_images[k].base_sector + lun_images[k].total_sectors == disk_sectors) {
                        diag.info("rawprogram", "placing " + lun_images[k].filename +
                                                    " relative to the end of the disk (NUM_DISK_SECTORS-" +
                                                    std::to_string(lun_images[k].total_sectors) + ")");
                    }
                }
            }

            write_rawprogram(raw_path, lun, lun_images, sector_size, disk_sectors);
            std::cout << "  " << raw_path.filename().string() << ": "
                      << lun_images.size() << " program entries" << std::endl;

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
