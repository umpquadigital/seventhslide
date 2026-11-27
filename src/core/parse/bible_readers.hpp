#pragma once

// The Bible readers bible.cpp hands a parsed document to, beside its own
// Zefania and OpenSong one; see bible.hpp.

#include <seventhslide/parse/bible.hpp>

#include <pugixml.hpp>

namespace seventhslide::parse {

/// OSIS: an ``<osis>`` root whose ``<osisText>`` is in it or in its
/// ``<osisCorpus>`` (the first, if several). The document is parsed with
/// ``parse_ws_pcdata``: the spaces between marked-up words are text.
[[nodiscard]] ParsedBible read_osis(pugi::xml_node root);

}  // namespace seventhslide::parse
