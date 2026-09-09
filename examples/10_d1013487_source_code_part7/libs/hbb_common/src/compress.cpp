// compress.cpp — see compress.hpp.
#include <hbb_common/compress.hpp>

#include <zstd.h>

#include <memory>

namespace hbb_common {
namespace {

struct CCtxDeleter {
    void operator()(ZSTD_CCtx* c) const { ZSTD_freeCCtx(c); }
};
struct DCtxDeleter {
    void operator()(ZSTD_DCtx* c) const { ZSTD_freeDCtx(c); }
};

// `thread_local! { COMPRESSOR / DECOMPRESSOR }` parity: one reused context
// per thread (zstd contexts are not thread-safe for concurrent use).
thread_local std::unique_ptr<ZSTD_CCtx, CCtxDeleter> t_cctx(ZSTD_createCCtx());
thread_local std::unique_ptr<ZSTD_DCtx, DCtxDeleter> t_dctx(ZSTD_createDCtx());

}  // namespace

std::vector<uint8_t> compress(const uint8_t* data, size_t len, int level) {
    if (!t_cctx) {
        return {};
    }
    std::vector<uint8_t> out(ZSTD_compressBound(len));
    const size_t r = ZSTD_compressCCtx(t_cctx.get(), out.data(), out.size(), data, len, level);
    if (ZSTD_isError(r)) {
        return {};  // `log::debug!` + empty Vec parity
    }
    out.resize(r);
    return out;
}

std::vector<uint8_t> decompress(const uint8_t* data, size_t len) {
    if (!t_dctx) {
        return {};
    }
    constexpr size_t kMax = 1024 * 1024 * 64;
    constexpr size_t kMin = 1024 * 1024;
    size_t n = 30 * len;  // same growth heuristic as upstream
    if (n > kMax) {
        n = kMax;
    }
    if (n < kMin) {
        n = kMin;
    }
    std::vector<uint8_t> out(n);
    const size_t r = ZSTD_decompressDCtx(t_dctx.get(), out.data(), out.size(), data, len);
    if (ZSTD_isError(r)) {
        return {};  // `log::debug!` + empty Vec parity
    }
    out.resize(r);
    return out;
}

}  // namespace hbb_common
