#ifndef DZ_BUILDER_HPP
#define DZ_BUILDER_HPP

#include <vector>
#include <filesystem>
#include <cstdint>
#include <mutex>
#include "utils.hpp"
#include "thread_pool.hpp"
#include "shared_structure.hpp"

class DzBuilder {
private:
    const json& meta;
    std::mutex cout_mutex; // Mutex for protecting std::cout
    std::vector<char> compress_data(const std::vector<char>& input) const;
    std::vector<char> md5_hash(const void* data, size_t size) const;

public:
    explicit DzBuilder(const json& metadata) : meta(metadata["dz"]) {}
    // Writes the DZ (main header, chunk headers and compressed data) to temp_path and
    // returns its size. The payload is streamed instead of being collected in memory,
    // so the archive never has to fit in RAM.
    uint64_t build(const std::filesystem::path& input_dir, ThreadPool& pool,
                   const std::filesystem::path& temp_path);
};

#endif