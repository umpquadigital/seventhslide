#include <seventhslide/parse/xml_safe.hpp>

#include "seventhslide/util/strings.hpp"

#include <pugixml.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace seventhslide::parse {

bool has_forbidden_doctype(std::string_view data) noexcept
{
    constexpr std::string_view lit = "<!DOCTYPE";
    for (std::size_t i = 0; i + lit.size() <= data.size(); ++i) {
        if (data[i] != '<') {
            continue;
        }
        bool match = true;
        for (std::size_t j = 0; j < lit.size(); ++j) {
            if (util::to_lower(data[i + j]) != util::to_lower(lit[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

bool nests_too_deep(const pugi::xml_document& doc)
{
    // pugixml's traverse keeps its place through parent links, not the call
    // stack. ``depth()`` is 0 for the document element.
    struct DepthCheck : pugi::xml_tree_walker {
        bool too_deep = false;
        bool for_each(pugi::xml_node& node) override
        {
            too_deep = node.type() == pugi::node_element && depth() >= k_max_xml_depth;
            return !too_deep;
        }
    };
    DepthCheck check;
    pugi::xml_node root = doc;
    root.traverse(check);
    return check.too_deep;
}

std::string read_file_bytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open file");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace seventhslide::parse
