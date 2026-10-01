#include "decompressor.hpp"

#include <fstream>
#include <zlib.h>
#include <zstd.h>

namespace {

bool read_compressed(const std::string& path, uint64_t offset, uint32_t size, std::vector<char>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(static_cast<std::streamoff>(offset));
    out.resize(size);
    file.read(out.data(), static_cast<std::streamsize>(size));
    return file.gcount() == static_cast<std::streamsize>(size);
}

} // namespace

std::vector<char> decompress_chunk_to_memory(const std::string& input_path,
                                             const std::string& compression,
                                             uint64_t file_offset,
                                             uint32_t compressed_size,
                                             uint64_t expected_size,
                                             uint64_t max_output) {
    std::vector<char> result;
    if (compressed_size == 0 || max_output == 0) return result;

    uint64_t limit = max_output;
    if (expected_size != 0 && expected_size < limit) limit = expected_size;

    std::vector<char> input;
    if (!read_compressed(input_path, file_offset, compressed_size, input)) return result;

    std::vector<char> buffer(64 * 1024);

    if (compression == "zlib") {
        z_stream strm = {};
        if (inflateInit(&strm) != Z_OK) return result;
        strm.next_in = reinterpret_cast<Bytef*>(input.data());
        strm.avail_in = static_cast<uInt>(input.size());

        while (true) {
            strm.next_out = reinterpret_cast<Bytef*>(buffer.data());
            strm.avail_out = static_cast<uInt>(buffer.size());
            const int ret = inflate(&strm, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
                inflateEnd(&strm);
                return std::vector<char>();
            }

            const std::size_t have = buffer.size() - strm.avail_out;
            if (have > 0) {
                if (result.size() + have >= limit) {
                    const std::size_t room = static_cast<std::size_t>(limit - result.size());
                    result.insert(result.end(), buffer.begin(), buffer.begin() + room);
                    break;
                }
                result.insert(result.end(), buffer.begin(), buffer.begin() + have);
            }

            if (ret == Z_STREAM_END) break;
            if (strm.avail_in == 0) break;
            if (have == 0 && ret == Z_BUF_ERROR) break;
        }

        inflateEnd(&strm);
        return result;
    }

    if (compression == "zstd") {
        ZSTD_DStream* dstream = ZSTD_createDStream();
        if (dstream == nullptr) return result;
        ZSTD_initDStream(dstream);

        ZSTD_inBuffer in = { input.data(), input.size(), 0 };
        while (in.pos < in.size) {
            const std::size_t before = in.pos;
            ZSTD_outBuffer out = { buffer.data(), buffer.size(), 0 };
            const size_t ret = ZSTD_decompressStream(dstream, &out, &in);
            if (ZSTD_isError(ret)) {
                ZSTD_freeDStream(dstream);
                return std::vector<char>();
            }

            if (out.pos > 0) {
                if (result.size() + out.pos >= limit) {
                    const std::size_t room = static_cast<std::size_t>(limit - result.size());
                    result.insert(result.end(), buffer.begin(), buffer.begin() + room);
                    break;
                }
                result.insert(result.end(), buffer.begin(), buffer.begin() + out.pos);
            }

            if (ret == 0) break;                  // the frame is complete
            if (in.pos == before && out.pos == 0) break;   // no progress, give up
        }

        ZSTD_freeDStream(dstream);
        return result;
    }

    return result;
}
