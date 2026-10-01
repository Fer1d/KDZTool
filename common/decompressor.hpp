#ifndef DECOMPRESSOR_HPP
#define DECOMPRESSOR_HPP

#include <cstdint>
#include <string>
#include <vector>

// Decompresses a single DZ chunk into memory. At most max_output bytes are
// produced, and at most expected_size bytes when the chunk header stores a
// non-zero size, so the result may be a prefix of the payload - which is all the
// GPT probe needs. Returns an empty vector when the chunk cannot be decompressed.
std::vector<char> decompress_chunk_to_memory(const std::string& input_path,
                                             const std::string& compression,
                                             uint64_t file_offset,
                                             uint32_t compressed_size,
                                             uint64_t expected_size,
                                             uint64_t max_output);

#endif // DECOMPRESSOR_HPP
