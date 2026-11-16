#include "asset.h"
#include <zstd.h>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace stars::renderer {
namespace { constexpr std::size_t limit = 64*1024*1024; }
std::vector<std::uint8_t> unpack(std::span<const std::uint8_t> compressed) {
    detail::valid(!compressed.empty() && compressed.size()<=limit);
    std::unique_ptr<ZSTD_DStream,decltype(&ZSTD_freeDStream)> stream(ZSTD_createDStream(),ZSTD_freeDStream);
    if (!stream) throw std::bad_alloc();
    detail::valid(!ZSTD_isError(ZSTD_DCtx_setParameter(stream.get(),ZSTD_d_windowLogMax,26)));
    detail::valid(!ZSTD_isError(ZSTD_initDStream(stream.get())));
    ZSTD_inBuffer input{compressed.data(),compressed.size(),0};
    std::array<std::uint8_t,65536> chunk;
    std::vector<std::uint8_t> result;
    for (;;) {
        ZSTD_outBuffer output{chunk.data(),chunk.size(),0};
        const auto before=input.pos;
        const auto remaining=ZSTD_decompressStream(stream.get(),&output,&input);
        detail::valid(!ZSTD_isError(remaining) && output.pos<=limit-result.size());
        result.insert(result.end(),chunk.begin(),chunk.begin()+output.pos);
        if (!remaining) { detail::valid(input.pos==input.size); break; }
        detail::valid(input.pos!=before || output.pos!=0);
    }
    return result;
}
std::vector<std::uint8_t> unpack(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if (!input) throw std::runtime_error("cannot open bitmap bundle: " + path.string());
    auto size=input.tellg(); detail::valid(size>0 && size<=std::streamoff(limit));
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    input.seekg(0); input.read(reinterpret_cast<char*>(data.data()),size);
    detail::valid(bool(input)); return unpack(data);
}
namespace detail {
void valid(bool condition) { if (!condition) throw std::runtime_error("invalid STARS bitmap bundle"); }
std::span<const std::uint8_t> Reader::bytes(std::size_t count) {
    valid(count<=data_.size()); auto result=data_.first(count); data_=data_.subspan(count); return result;
}
std::uint32_t Reader::u32() {
    auto b=bytes(4); return std::uint32_t(b[0]) | std::uint32_t(b[1])<<8 | std::uint32_t(b[2])<<16 | std::uint32_t(b[3])<<24;
}
std::int32_t Reader::i32() { return std::bit_cast<std::int32_t>(u32()); }
unsigned Reader::header(const char* magic,unsigned maximum) {
    valid(std::memcmp(bytes(8).data(),magic,8)==0); valid(u32()==1);
    auto count=u32(); valid(count>0 && count<=maximum); return count;
}
void Reader::end() const { valid(data_.empty()); }
}
} // namespace stars::renderer
