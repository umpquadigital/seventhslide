#include "seventhslide/util/read_file.hpp"

#include <fstream>

namespace seventhslide::util {

std::optional<std::string> read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::nullopt;
    }
    const auto size = static_cast<std::streamoff>(in.tellg());
    if (size < 0 || !in.seekg(0)) {
        return std::nullopt;
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    if (!in.read(out.data(), size)) {
        return std::nullopt;
    }
    return out;
}

}  // namespace seventhslide::util
