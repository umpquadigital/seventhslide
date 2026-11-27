#include "search_text_qml.hpp"

#include <seventhslide/db/search_text.hpp>

namespace seventhslide::app {

bool SearchText::matches(const QString& text, const QString& query) const
{
    return db::SearchQuery(query).matches(text);
}

} // namespace seventhslide::app
