// compress.hpp — translation of libs/hbb_common/src/compress.rs (zstd block API).
//
// Levels 1..22 mirror ZSTD_maxCLevel(); 0 means "default" (level 3).
// Failure mapping: upstream logs at debug (silent without a logger) and
// returns an empty Vec, so failures surface as empty vectors here too.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hbb_common {

std::vector<uint8_t> compress(const uint8_t* data, size_t len, int level);
inline std::vector<uint8_t> compress(const std::vector<uint8_t>& data, int level) {
    return compress(data.data(), data.size(), level);
}

std::vector<uint8_t> decompress(const uint8_t* data, size_t len);
inline std::vector<uint8_t> decompress(const std::vector<uint8_t>& data) {
    return decompress(data.data(), data.size());
}

}  // namespace hbb_common
