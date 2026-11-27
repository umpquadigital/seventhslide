#include "seventhslide/util/crc32.hpp"

#include <array>

namespace seventhslide::util {

namespace {

[[nodiscard]] constexpr std::array<std::uint32_t, 256> make_crc_table() noexcept
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        table[n] = c;
    }
    return table;
}

constexpr auto k_crc_table = make_crc_table();

}  // namespace

void Crc32::update(std::string_view bytes) noexcept
{
    std::uint32_t c = state_;
    for (const char ch : bytes) {
        c = k_crc_table[(c ^ static_cast<unsigned char>(ch)) & 0xFFu] ^ (c >> 8);
    }
    state_ = c;
}

std::uint32_t crc32(std::string_view bytes) noexcept
{
    Crc32 crc;
    crc.update(bytes);
    return crc.value();
}

}  // namespace seventhslide::util
