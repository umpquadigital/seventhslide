#include "shipped_bibles.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <exception>
#include <string_view>
#include <utility>

namespace seventhslide::app {
namespace {

[[nodiscard]] QStringList shipped_files(const QString& dir)
{
    return QDir(dir).entryList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name);
}

}  // namespace

std::vector<std::string> shipped_bible_abbreviations(const QString& dir)
{
    std::vector<std::string> out;
    for (const auto& name : shipped_files(dir)) {
        out.push_back(QFileInfo(name).completeBaseName().toStdString());
    }
    return out;
}

std::vector<ShippedBible> read_shipped_bibles(const QString& dir)
{
    std::vector<ShippedBible> out;
    for (const auto& name : shipped_files(dir)) {
        QFile file(QDir(dir).filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning("SeventhSlide: could not open the shipped Bible %s", qPrintable(name));
            continue;
        }
        const auto bytes = file.readAll();
        try {
            auto bible = parse::parse_bible_bytes(
                std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
            if (bible.verses.empty()) {
                qWarning("SeventhSlide: the shipped Bible %s has no verses", qPrintable(name));
                continue;
            }
            out.push_back(ShippedBible{
                .abbreviation = QFileInfo(name).completeBaseName().toStdString(),
                .bible = std::move(bible),
            });
        } catch (const std::exception& ex) {
            qWarning("SeventhSlide: could not read the shipped Bible %s: %s", qPrintable(name),
                     ex.what());
        }
    }
    return out;
}

}  // namespace seventhslide::app
