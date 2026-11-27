#include "service_thumbs.hpp"

#include "entry_error.hpp"
#include "library_controller.hpp"
#include "preview_resources.hpp"
#include "requested_size.hpp"

#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/render/scene_compositor.hpp>
#include <seventhslide/session/session.hpp>
#include <seventhslide/session/still_frames.hpp>

#include <QByteArray>
#include <QCryptographicHash>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <utility>

namespace seventhslide::app {

namespace {

/// A row of this kind may show a still picture of its own: a slide or an
/// image. Media, pages, live sources and dividers have none.
[[nodiscard]] bool shows_still(model::ContentKind kind) noexcept
{
    switch (kind) {
    case model::ContentKind::Song:
    case model::ContentKind::Bible:
    case model::ContentKind::Freeform:
    case model::ContentKind::Image:
    case model::ContentKind::ImageFolder:
        return true;
    case model::ContentKind::Empty:
    case model::ContentKind::Video:
    case model::ContentKind::Audio:
    case model::ContentKind::Web:
    case model::ContentKind::Live:
    case model::ContentKind::Divider:
        return false;
    }
    return false;
}

[[nodiscard]] QString cache_key(const QString& key, const QSize& size)
{
    return key + QLatin1Char('@') + QString::number(size.width()) + QLatin1Char('x')
           + QString::number(size.height());
}

}  // namespace

ServiceThumbs::ServiceThumbs(const session::Session& session,
                             LibraryController& library,
                             PreviewResources& previews,
                             QObject* parent)
    : QObject(parent)
    , session_(session)
    , library_(library)
    , previews_(previews)
{
}

QSize ServiceThumbs::requested_or_default(const QSize& requested)
{
    const QSize want = size_or(requested, QSize(128, 72));
    // Never more than a thumbnail: a stray large request paints no slide.
    return want.boundedTo(QSize(512, 512)).expandedTo(QSize(1, 1));
}

std::optional<ServiceThumbs::Source> ServiceThumbs::source_of(qint64 id) const
{
    auto* repo = library_.service_repo();
    const auto* output = session_.primary_output();
    if (repo == nullptr || output == nullptr || id < 0) {
        return std::nullopt;
    }
    const auto service = repo->get_service(id);
    if (!service) {
        return std::nullopt;
    }
    Source src;
    for (auto& row : repo->list_items(id)) {
        if (shows_still(row.kind())) {
            src.rows.push_back(std::move(row));
            if (static_cast<int>(src.rows.size()) == k_rows_tried) {
                break;
            }
        }
    }
    if (src.rows.empty()) {
        return std::nullopt;
    }
    src.output = *output;
    src.theme_priority = session_.theme_priority();
    src.ccli_licence_number = session_.ccli_licence_number();
    src.service_themes = theme::service_themes_from_json(service->theme_map);

    // The key covers what the picture is drawn from: the output's whole
    // saved config, the theme priority, the licence line, the service's
    // themes and its rows; and the themes themselves, by their revision.
    QCryptographicHash hash(QCryptographicHash::Sha1);
    const auto add = [&hash](const std::string& text) {
        hash.addData(QByteArray::fromStdString(text));
        hash.addData(QByteArrayView("\n"));
    };
    add(src.output.to_persist_dict().dump());
    for (const auto& name : src.theme_priority) {
        add(name);
    }
    add(src.ccli_licence_number);
    add(service->theme_map.dump());
    for (const auto& row : src.rows) {
        add(row.item_type);
        add(row.data.dump());
    }
    add(std::to_string(previews_.theme_revision()));
    src.key = QString::fromLatin1(hash.result().toHex().left(20));
    return src;
}

QString ServiceThumbs::thumbUrl(qint64 serviceId, int /*showing*/) const
try {
    const auto src = source_of(serviceId);
    if (!src) {
        return {};
    }
    // ``p`` asks again once a picture the slide shows arrives.
    return QStringLiteral("image://servicethumb/%1?k=%2&p=%3")
        .arg(QString::number(serviceId), src->key, QString::number(previews_.pictures_revision()));
} catch (const std::exception& ex) {
    // A thumbnail: one that cannot be had is left blank and logged, as the
    // image providers leave theirs.
    (void)report_entry_error(__func__, ex);
    return {};
}

QImage ServiceThumbs::cached(const QString& key, const QSize& size)
{
    return key.isEmpty() ? QImage() : cache_.get(cache_key(key, size));
}

PreviewJob ServiceThumbs::prepare(qint64 id, const QSize& size)
{
    auto src = source_of(id);
    if (!src) {
        return {};
    }
    const QSize box = requested_or_default(size);
    const auto canvas = model::output_canvas(src->output);
    QSize paint_size(std::max(1, canvas.width), std::max(1, canvas.height));
    paint_size.scale(box, Qt::KeepAspectRatio);
    paint_size = paint_size.expandedTo(QSize(1, 1));

    // Kept under the key it was asked for at, unless the cache was cleared
    // meanwhile, or a picture the slide shows was still missing as it painted
    // (it is drawn again once that arrives).
    return {
        .store = [this, key = cache_key(src->key, box),
                  generation = cache_.generation()](const std::function<QImage()>& paint) {
            if (QImage hit = cache_.get(key); !hit.isNull()) {
                return hit;
            }
            const auto* images = previews_.image_cache();
            const auto misses_before = images != nullptr ? images->misses() : 0;
            QImage img = paint();
            if (images == nullptr || images->misses() == misses_before) {
                cache_.put_if_current(key, img, generation);
            }
            return img;
        },
        .lay_out = [src = std::move(*src), paint_size,
                    previews = &previews_]() -> std::optional<render::StillPaint> {
            const std::vector<model::OutputConfig> outputs{src.output};
            const session::StackBuildContext ctx{previews->measurer(), outputs, src.theme_priority,
                                                 src.ccli_licence_number, src.service_themes};
            for (const auto& row : src.rows) {
                auto frame = session::first_still_frame(row, ctx, src.output.id);
                if (!frame) {
                    continue;
                }
                render::StillPaint still;
                still.frame = scene::share_frame(std::move(*frame));
                still.size = paint_size;
                still.cache = previews->image_cache();
                still.faces = &previews->measurer().faces();
                return still;
            }
            return std::nullopt;
        },
    };
}

}  // namespace seventhslide::app
