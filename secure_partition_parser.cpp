#include "secure_partition_parser.hpp"
#include "utils.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <algorithm>

std::optional<SecurePartition> SecurePartition::parse(std::ifstream& file, Diagnostics& diag) {
    try {
        file.seekg(SP_OFFSET);
        std::vector<char> data(SP_SIZE);
        file.read(data.data(), SP_SIZE);
        if (!file) return std::nullopt;

        SecurePartitionHeader hdr;
        std::memcpy(&hdr, data.data(), sizeof(SecurePartitionHeader));

        if (hdr.magic != SP_MAGIC) {
            return std::nullopt; // Not a valid secure partition
        }

        SecurePartition sec_part;
        sec_part.magic = hdr.magic;
        sec_part.flags = hdr.flags;
        sec_part.part_count = hdr.part_count;
        sec_part.signature.assign(hdr.signature, hdr.signature + hdr.sig_size);
        
        const char* p_rec = data.data() + sizeof(SecurePartitionHeader);
        for (uint32_t i = 0; i < hdr.part_count; ++i) {
            SecurePartitionRecord rec;
            std::memcpy(&rec, p_rec, sizeof(SecurePartitionRecord));
            p_rec += sizeof(SecurePartitionRecord);

            Part part;
            part.name = decode_asciiz(rec.name, 30);
            part.hw_part = rec.hw_part;
            part.logical_part = rec.logical_part;
            part.start_sect = rec.start_sect;
            part.end_sect = rec.end_sect;
            part.data_sect_cnt = rec.data_sect_cnt;
            part.reserved = rec.reserved;
            part.hash.assign(rec.hash, rec.hash + 32);

            if (part.reserved != 0) {
                // A non-zero reserved field is a consistency finding: the value
                // is preserved as it is and written back on repack, so there is
                // no reason to drop the whole secure partition over it.
                diag.unexpected("Secure partition " + part.name,
                                "reserved field is " + std::to_string(part.reserved) +
                                " instead of 0 (entry " + std::to_string(i) + ")");
            }

            // Logic to populate the vector of pairs, preserving order.
            auto hw_it = std::find_if(sec_part.parts.begin(), sec_part.parts.end(),
                                      [&](const auto& p) { return p.first == part.hw_part; });

            if (hw_it == sec_part.parts.end()) {
                // If hw_part is new, create a new entry for it.
                sec_part.parts.push_back({part.hw_part, {{part.name, {part}}}});
            } else {
                // If hw_part exists, find the partition name vector within it.
                auto& part_name_vec = hw_it->second;
                auto name_it = std::find_if(part_name_vec.begin(), part_name_vec.end(),
                                            [&](const auto& p) { return p.first == part.name; });

                if (name_it == part_name_vec.end()) {
                    // If partition name is new, create a new entry.
                    part_name_vec.push_back({part.name, {part}});
                } else {
                    // If partition name exists, append the part info.
                    name_it->second.push_back(part);
                }
            }
        }

        return sec_part;
    } catch (const std::exception& e) {
        // The secure partition block is optional, so a failure here is not
        // necessarily fatal - but the reason has to be visible, otherwise the
        // whole block (including its signature) silently disappears from
        // metadata.json and from anything repacked from it.
        if (diag.strict()) {
            throw;
        }
        diag.warn("Secure partition", std::string("could not be parsed: ") + e.what());
        return std::nullopt;
    }
}

void SecurePartition::print_info() const {
    size_t total_parts = 0;
    for(const auto& hw_part_pair : this->parts) {
        for(const auto& name_pair : hw_part_pair.second) {
            total_parts += name_pair.second.size();
        }
    }

    std::cout << "Secure Partition" << std::endl;
    std::cout << "================" << std::endl;
    std::cout << "magic = " << std::hex << this->magic << std::dec << std::endl;
    std::cout << "flags = " << std::hex << this->flags << std::dec << std::endl;
    std::cout << "signature = " << bytes_to_hex(this->signature) << std::endl;
    std::cout << "parts = " << total_parts << std::endl << std::endl;
}