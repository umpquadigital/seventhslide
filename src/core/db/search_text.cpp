#include "seventhslide/db/search_text.hpp"

namespace seventhslide::db {

QString search_fold(const QString& text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(decomposed.size());
    bool space = false;
    for (const QChar ch : decomposed) {
        if (ch.isMark()) {
            continue;
        }
        if (ch.isLetterOrNumber()) {
            if (space && !out.isEmpty()) {
                out.append(QLatin1Char(' '));
            }
            space = false;
            out.append(ch.toLower());
        } else if (ch.isSpace() || ch.category() == QChar::Punctuation_Dash
                   || ch == QLatin1Char('/')) {
            space = true;
        }
    }
    return out;
}

SearchQuery::SearchQuery(const QString& text)
    : typed_(text.trimmed())
    , folded_(search_fold(typed_))
{
}

SearchQuery::SearchQuery(std::string_view text)
    : SearchQuery(QString::fromUtf8(text))
{
}

bool SearchQuery::matches(const QString& text) const
{
    if (empty()) {
        return true;
    }
    return matches(text, folded_.isEmpty() ? QString() : search_fold(text));
}

bool SearchQuery::matches(std::string_view text) const
{
    if (empty()) {
        return true;
    }
    return matches(QString::fromUtf8(text));
}

bool SearchQuery::matches(const QString& text, const QString& folded) const
{
    return empty() || text.contains(typed_, Qt::CaseInsensitive)
           || (!folded_.isEmpty() && folded.contains(folded_));
}

const QCollator& name_collator()
{
    static const QCollator c = [] {
        QCollator made;
        made.setCaseSensitivity(Qt::CaseInsensitive);
        made.setNumericMode(true);
        return made;
    }();
    return c;
}

}  // namespace seventhslide::db
