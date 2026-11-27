#include <seventhslide/remote/remote_bridge.hpp>

namespace seventhslide::remote {

const std::vector<Section>& all_sections()
{
    static const std::vector<Section> sections{
        Section::Session, Section::Stacks,  Section::Service, Section::Lines,
        Section::Outputs, Section::Images,  Section::Advance, Section::Preview,
        Section::PreviewLines, Section::PreviewImages,
    };
    return sections;
}

std::string_view section_name(Section s) noexcept
{
    switch (s) {
    case Section::Session: return "session";
    case Section::Stacks: return "stacks";
    case Section::Service: return "service";
    case Section::Lines: return "lines";
    case Section::Outputs: return "outputs";
    case Section::Images: return "images";
    case Section::Advance: return "advance";
    case Section::Preview: return "preview";
    case Section::PreviewLines: return "previewLines";
    case Section::PreviewImages: return "previewImages";
    }
    return "unknown";
}

} // namespace seventhslide::remote
