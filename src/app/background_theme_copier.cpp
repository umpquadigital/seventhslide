#include "background_theme_copier.hpp"

#include "media_controller.hpp"
#include "open_library.hpp"

#include <seventhslide/db/media_kind.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/static_media.hpp>
#include <seventhslide/session/session.hpp>
#include <seventhslide/theme/theme_library.hpp>
#include <seventhslide/theme/theme_resolver.hpp>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace seventhslide::app {
namespace {

/// Where the file ``url`` names is on disk: a theme's own background, or a
/// library picture or video. Empty for anything else.
[[nodiscard]] std::filesystem::path file_of(const std::filesystem::path& media_root,
                                            std::string_view url)
{
    if (media_root.empty()) {
        return {};
    }
    const auto name = std::string(model::media_basename(url));
    if (url.starts_with(theme::k_theme_bg_url_prefix)) {
        return theme::theme_assets_dir(media_root) / name;
    }
    for (const auto subdir : {model::k_images_subdir, model::k_videos_subdir}) {
        if (url.starts_with("/static/" + std::string(subdir) + "/")) {
            return media_root / std::string(subdir) / name;
        }
    }
    return {};
}

/// The picture or video a theme owns that ``bg`` names, or null.
[[nodiscard]] std::string* theme_file_of(freeform::SlideBackground& bg)
{
    std::string* file = nullptr;
    if (bg.kind == freeform::SlideBackgroundKind::Image) {
        file = &bg.image;
    } else if (bg.kind == freeform::SlideBackgroundKind::Video) {
        file = &bg.video;
    }
    return file != nullptr && file->starts_with(theme::k_theme_bg_url_prefix) ? file : nullptr;
}

}  // namespace

BackgroundThemeCopier::BackgroundThemeCopier(const session::Session& session,
                                             const OpenLibrary& library,
                                             MediaController& media,
                                             QObject* parent)
    : QObject(parent)
    , session_(session)
    , library_(library)
    , media_(media)
{
    connect(&media_, &MediaController::importFinished, this,
            &BackgroundThemeCopier::on_import_finished);
}

void BackgroundThemeCopier::copy(std::string_view output_id, std::string_view theme_id,
                                 std::string owner, Done done)
{
    const model::ThemeEntry* theme = nullptr;
    for (const auto& oc : session_.outputs()) {
        if (oc.id == output_id) {
            theme = theme::find_theme(oc.bg_themes, theme_id);
        }
    }
    if (theme == nullptr) {
        done(std::nullopt, tr("That background theme is gone"));
        return;
    }
    auto copied = freeform::background_copy_of(theme->style);
    if (!copied.waits_for_copy()) {
        done(std::move(copied), {});
        return;
    }
    const auto url = copied.copy_from_url;
    const bool video = copied.background.kind == freeform::SlideBackgroundKind::Video;
    import_file(url, video, owner, theme->name,
                [copied = std::move(copied), done = std::move(done)](std::string filename,
                                                                     QString error) mutable {
                    if (filename.empty()) {
                        done(std::nullopt, std::move(error));
                        return;
                    }
                    freeform::place_copied_file(copied, std::move(filename));
                    done(std::move(copied), {});
                });
}

void BackgroundThemeCopier::own_theme_files(
    freeform::Document doc,
    std::string owner,
    std::function<void(freeform::Document, QStringList)> done)
{
    // Each file once, however many slides name it.
    std::vector<std::pair<std::string, bool>> files;
    for (auto& deck : doc.decks) {
        for (auto& slide : deck.slides) {
            const auto* file = theme_file_of(slide.background);
            if (file != nullptr
                && std::none_of(files.begin(), files.end(),
                                [&](const auto& f) { return f.first == *file; })) {
                files.emplace_back(*file,
                                   slide.background.kind == freeform::SlideBackgroundKind::Video);
            }
        }
    }
    if (files.empty()) {
        done(std::move(doc), {});
        return;
    }
    struct State {
        freeform::Document doc;
        QStringList errors;
        std::size_t left = 0;
        std::function<void(freeform::Document, QStringList)> done;
    };
    auto state = std::make_shared<State>();
    state->doc = std::move(doc);
    state->left = files.size();
    state->done = std::move(done);
    for (const auto& [name, video] : files) {
        import_file(name, video, owner, theme_name_of(name), [state, url = name](std::string filename, QString error) {
            for (auto& deck : state->doc.decks) {
                for (auto& slide : deck.slides) {
                    auto* file = theme_file_of(slide.background);
                    if (file == nullptr || *file != url) {
                        continue;
                    }
                    if (filename.empty()) {
                        slide.background = {};
                    } else {
                        *file = filename;
                    }
                }
            }
            if (!error.isEmpty()) {
                state->errors.push_back(error);
            }
            if (--state->left == 0) {
                state->done(std::move(state->doc), state->errors);
            }
        });
    }
}

void BackgroundThemeCopier::import_file(const std::string& url, bool video,
                                        const std::string& owner, const std::string& title,
                                        Imported done)
{
    const auto path = file_of(library_.media_root(), url);
    if (path.empty()) {
        done({}, tr("The background theme's file could not be found"));
        return;
    }
    const qint64 id = media_.import_freeform_media(
        video ? db::MediaKind::Video : db::MediaKind::Image, {QString::fromStdString(path.string())},
        owner, title.empty() ? std::string("Background") : title);
    pending_.emplace(id, std::move(done));
}

std::string BackgroundThemeCopier::theme_name_of(std::string_view url) const
{
    // The theme that shows it, on any output; its file's own name is no
    // one's to read.
    for (const auto& oc : session_.outputs()) {
        for (const auto& theme : oc.bg_themes) {
            const auto copied = freeform::background_copy_of(theme.style);
            if (copied.copy_from_url == url) {
                return theme.name;
            }
        }
    }
    return {};
}

void BackgroundThemeCopier::on_import_finished(qint64 request_id, const QVariantMap& result)
{
    const auto it = pending_.find(request_id);
    if (it == pending_.end()) {
        return;  // someone else's import
    }
    Imported done = std::move(it->second);
    pending_.erase(it);
    const auto names = result.value(QStringLiteral("filenames")).toStringList();
    if (names.isEmpty()) {
        const auto errors = result.value(QStringLiteral("errors")).toStringList();
        done({}, errors.isEmpty() ? tr("The background could not be copied")
                                  : errors.join(QLatin1Char('\n')));
        return;
    }
    done(names.front().toStdString(), {});
}

}  // namespace seventhslide::app
