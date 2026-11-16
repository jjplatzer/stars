#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace stars::renderer {
// One bounded Zstandard frame, with no trailing data. Used by the generated
// STARS font/cursor bundles; parsing never depends on native struct layout.
std::vector<std::uint8_t> unpack(std::span<const std::uint8_t> compressed);
std::vector<std::uint8_t> unpack(const std::filesystem::path& path);

namespace detail {
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}
    std::span<const std::uint8_t> bytes(std::size_t count);
    std::uint32_t u32();
    std::int32_t i32();
    unsigned header(const char* magic, unsigned maximum);
    void end() const;
private:
    std::span<const std::uint8_t> data_;
};
void valid(bool condition);
}
} // namespace stars::renderer
