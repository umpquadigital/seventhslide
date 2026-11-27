#include "seventhslide/layout/text_measurer.hpp"

#include "seventhslide/util/strings.hpp"

namespace seventhslide::layout {

double approx_text_width(std::string_view text, const FontStyle& style) noexcept
{
    if (text.empty()) {
        return 0.0;
    }
    const double size = style.size_px > 0.0 ? style.size_px : 1.0;
    return static_cast<double>(util::utf8_length(text)) * size * 0.5
        + tracking_extra_px(text, style.letter_spacing);
}

}  // namespace seventhslide::layout
