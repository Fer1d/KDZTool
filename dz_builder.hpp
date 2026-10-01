#ifndef DZ_BUILDER_HPP
#define DZ_BUILDER_HPP

#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <mutex>
#include "utils.hpp"
#include "thread_pool.hpp"
#include "shared_structure.hpp"

class DzBuilder {
private:
    const json& meta;
    int compression_level_; // -1 = library default
    std::mutex cout_mutex; // Mutex for protecting std::cout
    std::vector<char> compress_data(const std::vector<char>& input) const;
    std::vector<char> md5_hash(const void* data, size_t size) const;

public:
    // compression_level is handed to zlib/zstd; -1 keeps their default.
    explicit DzBuilder(const json& metadata, int compression_level = -1)
        : meta(metadata["dz"]), compression_level_(compression_level) {}

    // Streams the DZ (main header, chunk headers and compressed data) into an already
    // open output file at the given offset and returns its size. The payload is never
    // held in memory as a whole, and no second copy of it is written to disk.
    uint64_t build(const std::filesystem::path& input_dir, ThreadPool& pool, std::ofstream& out,
                   uint64_t offset, const std::filesystem::path& output_path);
};

#endif