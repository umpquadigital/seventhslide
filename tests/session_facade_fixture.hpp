#pragma once

// What the SessionFacade tests share: a Main output, a demo song, song theme
// maps with a TH1 chip, a library Freeform and a document store with no
// database. Split by controller like the app (test_facade_*.cpp).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "qvariant_json.hpp"
#include "freeform_model_test_helpers.hpp"
#include "library_controller.hpp"
#include "media_import_helpers.hpp"
#include "facade_all.hpp"
#include "theme_controller.hpp"
#include "slide_edit_session.hpp"
#include "text_edit_tools.hpp"
#include "scene_test_helpers.hpp"
#include "theme_test_helpers.hpp"

#include <seventhslide/theme/theme_elements.hpp>
#include <seventhslide/theme/theme_resolver.hpp>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/fonts/font_registry.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/render/background_painter.hpp>
#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/render/virtual_camera.hpp>
#include <seventhslide/session/session.hpp>

#include <nlohmann/json.hpp>

#include <QColor>
#include <QCoreApplication>
#include <QFont>
#include <QImage>
#include <QMetaObject>
#include <QMetaProperty>
#include <QPainter>
#include <QSignalSpy>
#include <QRectF>
#include <QSize>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextDocument>
#include <QThread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using seventhslide::app::MediaBrowserModel;
using seventhslide::app::SessionFacade;
using seventhslide::model::OutputConfig;
using seventhslide::model::ServiceItem;
using seventhslide::model::Song;

namespace seventhslide::test::facade {

using seventhslide::test::element_text;

inline OutputConfig make_main()
{
    OutputConfig oc;
    oc.id = "out-main";
    oc.name = "Main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    return oc;
}

/// Objects on the slide the slide editor is showing.
inline int edited_object_count(const SessionFacade& facade)
{
    const auto& editor = facade.slideEdit()->freeform_editor();
    const auto& doc = editor.document();
    const auto* deck = seventhslide::freeform::find_deck(doc, editor.output_id());
    if (deck == nullptr) {
        return 0;
    }
    const int slide = editor.slide_index();
    if (slide < 0 || slide >= static_cast<int>(deck->slides.size())) {
        return 0;
    }
    return static_cast<int>(deck->slides[static_cast<std::size_t>(slide)].objects.size());
}

/// A text box with ``text`` on the slide the slide editor is showing, selected.
inline QString add_text(SessionFacade& facade, const QString& text)
{
    auto* objects = facade.slideEdit()->objects();
    const QString err = objects->insert(QStringLiteral("text"));
    if (!err.isEmpty()) {
        return err;
    }
    return objects->setText(objects->selection().value(QStringLiteral("id")).toString(), text, false,
                            false, false);
}

/// The id of ``facade``'s first output (a new library's Main has a fresh one).
inline QString first_output_id(const SessionFacade& facade)
{
    return QString::fromStdString(facade.session().outputs().front().id);
}

inline Song demo_song()
{
    Song song;
    song.title = "Test Song";
    song.lyrics = "[V1]\nPraise to the Lord\nThe Almighty\n\n[V2]\nO my soul praise Him\nFor He is thy health";
    song.verse_order = "v1 v2";
    return song;
}

inline ServiceItem make_song_item(std::string title, std::string lyrics)
{
    ServiceItem item;
    item.item_type = "song";
    item.data = nlohmann::json{
        {"title", title},
        {"lyrics", lyrics},
        {"verse_order", "v1"},
        {"theme_map", nlohmann::json::object()},
    };
    return item;
}

/// One output's entry in a theme map as QML holds it: ``pick`` ({theme, bg})
/// for ``category``.
inline QVariantMap in_category(const QString& category, const QVariantMap& pick)
{
    QVariantMap entry;
    entry.insert(category, pick);
    return entry;
}

/// A song theme map with one arrangement whose TH1 chip selects ``chip``.
inline QVariantMap song_theme_map_with_chip(const QVariantMap& chip)
{
    QVariantMap chips;
    chips.insert(QStringLiteral("TH1"), chip);
    QVariantMap arrangement;
    arrangement.insert(QStringLiteral("id"), QStringLiteral("a1"));
    arrangement.insert(QStringLiteral("name"), QStringLiteral("Default"));
    arrangement.insert(QStringLiteral("order"), QStringLiteral("TH1 V1"));
    arrangement.insert(QStringLiteral("chips"), chips);
    QVariantMap theme_map;
    theme_map.insert(QStringLiteral("active"), QStringLiteral("a1"));
    theme_map.insert(QStringLiteral("arrangements"), QVariantList{arrangement});
    return theme_map;
}

/// The TH1 chip's per-output map in a song theme map as QML receives it.
inline QVariantMap first_chip(const QVariant& theme_map)
{
    const auto arrangements = theme_map.toMap().value(QStringLiteral("arrangements")).toList();
    if (arrangements.isEmpty()) {
        return {};
    }
    return arrangements.front()
        .toMap()
        .value(QStringLiteral("chips"))
        .toMap()
        .value(QStringLiteral("TH1"))
        .toMap();
}

/// ``song_theme_map_with_chip`` for a ``Song::theme_map``.
inline nlohmann::json song_theme_json_with_chip(nlohmann::json chip)
{
    return {{"active", "a1"},
            {"arrangements",
             nlohmann::json::array({{{"id", "a1"},
                                     {"name", "Default"},
                                     {"order", "TH1 V1"},
                                     {"chips", {{"TH1", std::move(chip)}}}}})}};
}

/// A library Freeform with ``slides`` slides, each holding one text box.
inline qint64 add_library_freeform(SessionFacade& facade, const std::string& name, int slides)
{
    auto doc = seventhslide::freeform::make_document(name, {facade.session().primary_output()->id});
    for (int i = 1; i < slides; ++i) {
        REQUIRE(seventhslide::freeform::insert_slide(doc, i).ok);
    }
    for (auto& slide : doc.decks[0].slides) {
        seventhslide::freeform::Object box;
        box.type = seventhslide::freeform::ObjectType::Text;
        box.w = 400;
        box.h = 100;
        box.text = name;
        slide.objects.push_back(box);
    }
    seventhslide::model::FreeformItem item;
    item.name = name;
    item.document = doc.to_json();
    const auto id = facade.freeform_repo()->create_item(item);
    facade.refresh_library();
    return static_cast<qint64>(id);
}

/// A document store with no database behind it: the one seam the Freeform
/// editor keeps, so it runs without a library.
class FakeDocumentStore final : public seventhslide::app::IFreeformDocumentStore {
public:
    std::optional<seventhslide::freeform::Document> stored;
    std::optional<seventhslide::freeform::Document> committed;
    std::int64_t committed_id = -1;
    QString commit_error;

    [[nodiscard]] std::optional<seventhslide::freeform::Document> load_freeform_document(
        std::int64_t) override
    {
        return stored;
    }
    [[nodiscard]] QString commit_freeform_document(
        std::int64_t id, const seventhslide::freeform::Document& doc) override
    {
        if (!commit_error.isEmpty()) {
            return commit_error;
        }
        committed_id = id;
        committed = doc;
        return {};
    }
};

}  // namespace seventhslide::test::facade
