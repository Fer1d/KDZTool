#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <thread>
#include <algorithm>
#include <stdexcept>

// --- Headers required for unpacking ---
#include "kdz_parser.hpp"
#include "diagnostics.hpp"
#include "secure_partition_parser.hpp"
#include "dz_parser.hpp"
#include "extractor.hpp"
#include "metadata_generator.hpp"
#include "rawprogram_generator.hpp"

// --- Headers required for repacking ---
#include "secure_partition_builder.hpp"
#include "kdz_builder.hpp"
#include "dz_builder.hpp"

namespace fs = std::filesystem;

void printUsage(const char* progName) {
    std::cerr << "A tool to extract and repack LG KDZ firmware." << std::endl;
    std::cerr << "Usage: " << progName << " <command> [options]" << std::endl << std::endl;
    std::cerr << "Commands:" << std::endl;
    std::cerr << "  extract    Extract a KDZ file to a folder." << std::endl;
    std::cerr << "  repack     Repack an extracted folder into a KDZ file." << std::endl << std::endl;
    std::cerr << "Options for 'extract':" << std::endl;
    std::cerr << "  " << progName << " extract <kdz_file> [-d <path>] [--no-verify] [--strict]" << std::endl;
    std::cerr << "                                      [--rawprogram] [--sector-size <bytes>]" << std::endl;
    std::cerr << "    <kdz_file>           Path to the input KDZ firmware file." << std::endl;
    std::cerr << "    -d, --dest <path>    The directory to extract files to." << std::endl;
    std::cerr << "                         (If not specified, only header info will be printed)." << std::endl;
    std::cerr << "    --no-verify          Skip DZ data hash verification for faster startup." << std::endl;
    std::cerr << "    --strict             Abort on consistency problems (checksum mismatches," << std::endl;
    std::cerr << "                         unexpected field values) instead of warning about them." << std::endl;
    std::cerr << "    --rawprogram         Also write rawprogram<N>.xml and patch<N>.xml for a 9008/EDL" << std::endl;
    std::cerr << "                         (QFIL) flash. Requires -d." << std::endl;
    std::cerr << "    --keep-b             Extract the B slots of an A/B device as well instead of" << std::endl;
    std::cerr << "                         flashing them from the A slot image (--rawprogram only)." << std::endl;
    std::cerr << "    --sector-size <n>    Override the detected sector size (power of two, 512..65536)." << std::endl << std::endl;
    std::cerr << "Options for 'repack':" << std::endl;
    std::cerr << "  " << progName << " repack <input_dir> <output_file> [--compression <n>]" << std::endl;
    std::cerr << "    <input_dir>          Path to the directory containing extracted files and metadata.json." << std::endl;
    std::cerr << "    <output_file>        Path for the new output KDZ file." << std::endl;
    std::cerr << "    --compression <n>    Compression level from 1 (fastest) to 22 (smallest)." << std::endl;
    std::cerr << "                         The default is the compression library's own default." << std::endl << std::endl;
    std::cerr << "General Options:" << std::endl;
    std::cerr << "  -h, --help           Show this help message and exit." << std::endl;
}

int main(int argc, char* argv[]) {
    // Handle help options in priority
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (argc < 2) {
        std::cerr << "Error: No command specified. Use 'extract' or 'repack'." << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    try {
        std::string command = argv[1];
        size_t num_threads = std::max(1u, std::thread::hardware_concurrency() / 2);
        ThreadPool pool(num_threads);
        
        if (command == "extract") {
            std::string file_path;
            std::optional<std::string> extract_path;
            bool skip_verification = false;
            bool strict = false;
            bool rawprogram = false;
            bool keep_b = false;
            std::optional<uint32_t> sector_size_override;

            for (int i = 2; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--no-verify") {
                    skip_verification = true;
                } else if (arg == "--strict") {
                    strict = true;
                } else if (arg == "--rawprogram") {
                    rawprogram = true;
                } else if (arg == "--keep-b") {
                    keep_b = true;
                } else if (arg == "--sector-size") {
                    if (i + 1 < argc) {
                        try {
                            const unsigned long value = std::stoul(argv[++i]);
                            if (value < 512 || value > 65536 || (value & (value - 1)) != 0) {
                                std::cerr << "Error: --sector-size must be a power of two between 512 and 65536." << std::endl;
                                return 1;
                            }
                            sector_size_override = static_cast<uint32_t>(value);
                        } catch (const std::exception&) {
                            std::cerr << "Error: --sector-size expects a number." << std::endl;
                            return 1;
                        }
                    } else {
                        std::cerr << "Error: " << arg << " option requires an argument." << std::endl;
                        printUsage(argv[0]);
                        return 1;
                    }
                } else if (arg == "-d" || arg == "--dest") {
                    if (i + 1 < argc) {
                        extract_path = argv[++i];
                    } else {
                        std::cerr << "Error: " << arg << " option requires an argument." << std::endl;
                        printUsage(argv[0]);
                        return 1;
                    }
                } else {
                    if (!file_path.empty()) {
                        std::cerr << "Error: Multiple input files specified for extract. Only one is allowed." << std::endl;
                        printUsage(argv[0]);
                        return 1;
                    }
                    file_path = arg;
                }
            }

            if (file_path.empty()) {
                std::cerr << "Error: Input KDZ file not specified for extract command." << std::endl;
                printUsage(argv[0]);
                return 1;
            }

            if (rawprogram && !extract_path.has_value()) {
                std::cerr << "Error: --rawprogram requires an output directory (-d)." << std::endl;
                return 1;
            }

            std::ifstream in_file(file_path, std::ios::binary);
            if (!in_file) {
                throw std::runtime_error("Cannot open file " + file_path);
            }

            // 1. Parse all headers and store the object. `diag` collects every
            // consistency problem so that parsing no longer stops on the first one.
            Diagnostics diag(strict);
            KdzHeader kdz_header(in_file, diag);
            kdz_header.print_info(in_file);

            std::optional<SecurePartition> sec_part = SecurePartition::parse(in_file, diag);
            if (sec_part.has_value()) {
                sec_part->print_info();
            } else {
                std::cout << "No secure partition found\n" << std::endl;
            }
            
            const KdzHeader::Record* dz_record_ptr = nullptr;
            for (const auto& record : kdz_header.records) {
                if (record.name.size() >= 3 && record.name.substr(record.name.size() - 3) == ".dz") {
                    dz_record_ptr = &record;
                    break;
                }
            }
            if (!dz_record_ptr) {
                throw std::runtime_error("No DZ record in KDZ file");
            }

            DzHeader dz_hdr(in_file, *dz_record_ptr, skip_verification, diag);
            dz_hdr.detect_sector_size(file_path, sector_size_override);
            dz_hdr.print_info();

            // 2. If unpacking is requested, extract all embedded objects and their metadata.
            if (extract_path.has_value()) {
                fs::create_directories(*extract_path);

                // Unpacking DLLs and other components
                extract_kdz_components(in_file, kdz_header, *extract_path);
                
                // Use thread pool to unpack DZ partitions
                std::cout << "Initializing thread pool with " << num_threads << " threads for extraction." << std::endl << std::endl;
                extract_dz_parts(file_path, dz_hdr, *extract_path, pool, rawprogram, keep_b, diag);

                // Unpacking V3's additional information
                extract_additional_data(in_file, kdz_header, *extract_path);

                // 3. Optionally write the 9008/EDL (QFIL) flashing metadata. This
                //    runs before metadata.json so that its findings end up in the
                //    diagnostics list as well.
                if (rawprogram) {
                    generate_rawprogram_files(*extract_path, dz_hdr, keep_b, diag);
                }

                // 4. Generate and store metadata.json
                generate_metadata(*extract_path, kdz_header, sec_part, dz_hdr);
            
            } else {
                 // If not unpacked, only print detailed information
                 for (const auto& hw_pair : dz_hdr.parts) {
                    std::cout << "Partition " << hw_pair.first << ":" << std::endl;
                    for (const auto& name_pair : hw_pair.second) {
                        std::cout << "  " << name_pair.first << std::endl;
                        int i = 0;
                        for (const auto& chunk : name_pair.second) {
                            std::cout << "    " << i++ << ". " << chunk.name 
                                      << " (" << std::max<uint64_t>(chunk.data_size, (uint64_t)chunk.sector_count * dz_hdr.sector_size()) << " bytes, sparse: "
                                      << (chunk.is_sparse ? "true" : "false") << ")" << std::endl;
                        }
                        std::cout << std::endl;
                    }
                 }
            }

            // Compact summary of everything that looked suspicious. The details
            // are in the diagnostics section of metadata.json.
            if (diag.warning_count() > 0) {
                std::cerr << "[!] Finished with " << diag.warning_count()
                          << " consistency warning(s); re-run with --strict to turn them into errors."
                          << std::endl;
            }
        } else if (command == "repack") {
            if (argc < 4) {
                std::cerr << "Error: Invalid number of arguments for repack command." << std::endl;
                std::cerr << "Usage: " << argv[0] << " repack <input_dir> <output_file> [--compression <1-22>]" << std::endl;
                return 1;
            }

            fs::path input_dir(argv[2]);
            fs::path output_file(argv[3]);
            int compression_level = -1;

            for (int i = 4; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--compression") {
                    if (i + 1 >= argc) {
                        std::cerr << "Error: --compression requires an argument." << std::endl;
                        return 1;
                    }
                    try {
                        const int value = std::stoi(argv[++i]);
                        if (value < 1 || value > 22) {
                            std::cerr << "Error: --compression must be between 1 (fastest) and 22 (smallest)." << std::endl;
                            return 1;
                        }
                        compression_level = value;
                    } catch (const std::exception&) {
                        std::cerr << "Error: --compression expects a number." << std::endl;
                        return 1;
                    }
                } else {
                    std::cerr << "Error: Unknown option '" << arg << "' for repack." << std::endl;
                    return 1;
                }
            }

            auto metadata_path = input_dir / "metadata.json";
            if (!fs::exists(metadata_path)) {
                throw std::runtime_error("ERROR: metadata.json not found in '" + input_dir.string() + "'");
            }

            std::ifstream meta_file(metadata_path);
            json metadata = json::parse(meta_file);

            // 1. Create Secure Partition data (if it exists)
            SecurePartitionBuilder sec_part_builder(metadata);

            // 2. Use the thread pool to create the DZ archive. It is streamed straight
            //    into the output file while the KDZ is assembled, so the archive never
            //    has to fit in memory nor in a second copy on disk.
            std::cout << "Using " << num_threads << " threads for parallel processing." << std::endl;
            if (compression_level >= 0) {
                std::cout << "Using compression level " << compression_level << "." << std::endl;
            }

            DzBuilder dz_builder(metadata, compression_level);

            // 3. Creating the final KDZ profile
            KdzBuilder kdz_builder(metadata);
            kdz_builder.build(output_file, input_dir,
                              [&](std::ofstream& out, uint64_t offset, const fs::path& path)
                              {
                                  return dz_builder.build(input_dir, pool, out, offset, path);
                              },
                              sec_part_builder.data);
        } else {
            std::cerr << "Error: Unknown command '" << command << "'. Use 'extract' or 'repack'." << std::endl;
            printUsage(argv[0]);
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}