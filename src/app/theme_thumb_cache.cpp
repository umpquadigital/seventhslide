#include "theme_thumb_cache.hpp"

#include "media_thumb_cache.hpp"

#include <seventhslide/render/background_painter.hpp>
#include <seventhslide/render/static_assets.hpp>
#include <seventhslide/version.hpp>

#include <QCryptographicHash>
#include <QImageIOHandler>
#include <QImageReader>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <system_error>
#include <utility>
#include <vector>

namespace seventhslide::app {
namespace {

/// Bump when thumbs change in a way the version and the style do not show.
constexpr int k_theme_thumb_format = 1;

void add_text(QCryptographicHash& hash, std::string_view text)
{
    hash.addData(QByteArrayView(text.data(), static_cast<qsizetype>(text.size())));
    hash.addData(QByteArrayView("\n", 1));
}

/// Hash what the picture named by ``url`` is now: a file replaced under the same
/// name paints differently.
void add_picture(QCryptographicHash& hash,
                 const std::string& url,
                 const std::filesystem::path& media_dir)
{
    const auto path = render::resolve_static_asset(url, media_dir);
    if (!path) {
        return;
    }
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(*path, ec);
    if (ec) {
        add_text(hash, "missing");
        return;
    }
    const auto written = std::filesystem::last_write_time(*path, ec);
    // In nanoseconds named: libc++ counts file times in a 128-bit integer,
    // which std::to_string has no overload for.
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(written.time_since_epoch()).count();
    add_text(hash, std::to_string(bytes) + ' ' + std::to_string(nanoseconds));
}

/// Walk ``value`` for the pictures the style names.
void add_pictures(QCryptographicHash& hash,
                  const nlohmann::json& value,
                  const std::filesystem::path& media_dir)
{
    if (value.is_structured()) {
        for (const auto& child : value) {
            add_pictures(hash, child, media_dir);
        }
    } else if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        if (text.starts_with("/static/")) {
            add_picture(hash, text, media_dir);
        }
    }
}

} // namespace

std::string theme_thumb_identity(const nlohmann::json& style,
                                 const ThemeThumbView& view,
                                 const std::filesystem::path& media_dir)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    add_text(hash, std::to_string(k_theme_thumb_format));
    add_text(hash, version_string());
    add_text(hash, std::to_string(view.context));
    add_text(hash, std::to_string(view.canvas_width) + 'x' + std::to_string(view.canvas_height));
    add_text(hash, view.output_name);
    add_text(hash, std::to_string(view.size.width()) + 'x' + std::to_string(view.size.height()));
    // Replaced, not thrown: a style is only ever hashed here.
    add_text(hash, style.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    add_pictures(hash, style, media_dir);
    return hash.result().toHex().toStdString();
}

std::optional<QSize> theme_thumb_stored_size(const QSize& want)
{
    if (want.isEmpty() || std::max(want.width(), want.height()) > k_media_thumb_tier_lg) {
        return std::nullopt;
    }
    const int tier = media_thumb_tier_for_request(want.width(), want.height());
    if (std::abs(want.height() - want.width() * 9.0 / 16.0) <= 1.0) {
        return QSize(tier, tier * 9 / 16);
    }
    return want.scaled(tier, tier, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
}

std::filesystem::path theme_thumb_path(const std::filesystem::path& media_dir,
                                       std::string_view identity,
                                       const QSize& size)
{
    if (media_dir.empty() || identity.empty() || size.isEmpty()) {
        return {};
    }
    const int tier = media_thumb_tier_for_request(size.width(), size.height());
    return media_thumbs_dir(media_dir) / "themes" / std::to_string(tier)
         / (std::string(identity) + ".jpg");
}

QImage fit_theme_thumb(const QImage& image, const QSize& size)
{
    if (image.isNull() || size.isEmpty() || image.size() == size) {
        return image;
    }
    return image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage load_theme_thumb(const std::filesystem::path& path, const QSize& size)
{
    QImage img;
    if (path.empty() || !img.load(QString::fromStdString(path.string()))) {
        return {};
    }
    std::error_code ec;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
    return fit_theme_thumb(img, size);
}

QDateTime theme_thumb_clock_time()
{
    return QDateTime(QDate(2000, 1, 1), QTime(12, 0));
}

QImage decode_theme_thumb_picture(const std::filesystem::path& path, const QSize& paint_size)
{
    QImageReader reader(QString::fromStdString(path.string()));
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        return {};
    }
    const QSize orig = reader.size();
    // The scale applies before the turn, so a turned picture covers a turned box.
    const bool turned = reader.transformation().testFlag(QImageIOHandler::TransformationRotate90);
    const QSize box = turned ? paint_size.transposed() : paint_size;
    if (orig.isValid()) {
        const QSize cover = orig.scaled(box, Qt::KeepAspectRatioByExpanding).expandedTo(QSize(1, 1));
        if (cover.width() < orig.width()) {
            reader.setScaledSize(cover);
        }
    }
    const QImage img = reader.read();
    return img.isNull() ? img : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

std::shared_ptr<render::BackgroundImageCache> theme_thumb_pictures(
    const std::filesystem::path& media_dir,
    const QSize& paint_size)
{
    auto pictures = std::make_shared<render::BackgroundImageCache>(
        [paint_size](const std::filesystem::path& path, int) {
            return decode_theme_thumb_picture(path, paint_size);
        });
    pictures->set_static_root(media_dir);
    return pictures;
}

void prune_theme_thumbs(const std::filesystem::path& media_dir, std::size_t keep)
{
    if (media_dir.empty()) {
        return;
    }
    using Written = std::pair<std::filesystem::file_time_type, std::filesystem::path>;
    std::vector<Written> thumbs;
    std::error_code ec;
    const auto root = media_thumbs_dir(media_dir) / "themes";
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code file_ec;
        if (it->is_regular_file(file_ec) && it->path().extension() == ".jpg") {
            const auto written = it->last_write_time(file_ec);
            if (!file_ec) {
                thumbs.emplace_back(written, it->path());
            }
        }
    }
    if (thumbs.size() <= keep) {
        return;
    }
    const auto first_old = thumbs.begin() + static_cast<std::ptrdiff_t>(keep);
    std::nth_element(thumbs.begin(), first_old, thumbs.end(),
                     [](const Written& a, const Written& b) { return a.first > b.first; });
    for (auto it = first_old; it != thumbs.end(); ++it) {
        std::filesystem::remove(it->second, ec);
    }
}

} // namespace seventhslide::app
