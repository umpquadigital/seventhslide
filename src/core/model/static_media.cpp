#include "seventhslide/model/static_media.hpp"

namespace seventhslide::model {

std::optional<std::string> static_media_url(std::string_view subdir,
                                            std::string_view filename)
{
    if (subdir.empty()) {
        return std::nullopt;
    }
    const auto name = media_basename(filename);
    if (name.empty() || name == "." || name == "..") {
        return std::nullopt;
    }
    std::string out;
    out.reserve(8 + subdir.size() + 1 + name.size());
    out.append("/static/");
    out.append(subdir);
    out.push_back('/');
    out.append(name);
    return out;
}

std::optional<std::string> media_ref_url(std::string_view subdir, std::string_view ref)
{
    if (ref.starts_with("/static/")) {
        return std::string(ref);
    }
    return static_media_url(subdir, ref);
}

} // namespace seventhslide::model
