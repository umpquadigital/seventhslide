#pragma once

// Copies a background theme into Freeform slides. A
// Freeform takes nothing from themes: a background theme picked for a slide
// fills the slide's own fields once. Its colour goes in at once; a picture or
// video the theme owns is imported into the library first (off the GUI
// thread, as any import is), because a theme deletes the files it owns when
// it changes, and the slide then names the library's copy.
//
// The editor's "From theme…" copies into one slide, and a song or passage
// saved as Freeform has its slides' theme files copied before it is saved.
// Seam: Qt Core. GUI thread.

#include <seventhslide/freeform/background_copy.hpp>
#include <seventhslide/freeform/document.hpp>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::session {
class Session;
}

namespace seventhslide::app {

class MediaController;
class OpenLibrary;

class BackgroundThemeCopier final : public QObject {
    Q_OBJECT

public:
    BackgroundThemeCopier(const session::Session& session,
                          const OpenLibrary& library,
                          MediaController& media,
                          QObject* parent = nullptr);

    /// Called once with what the theme gives a slide, or with nothing and
    /// why (a theme that is gone, a file that would not copy).
    using Done = std::function<void(std::optional<freeform::BackgroundCopy>, QString error)>;

    /// Copy output ``output_id``'s background theme ``theme_id``. ``done``
    /// runs at once when nothing needs importing, else once the import is in.
    void copy(std::string_view output_id, std::string_view theme_id, Done done);

    /// ``doc`` with every picture or video a theme owns that its slides'
    /// backgrounds name (``/static/theme_backgrounds/...``, as a conversion
    /// names them) copied into the library, the slides naming the copy.
    /// ``done(doc, errors)`` runs at once when there is none, else once all
    /// are in; a file that would not copy leaves its slides with no
    /// background.
    void own_theme_files(freeform::Document doc,
                         std::function<void(freeform::Document, QStringList)> done);

private:
    /// Called once with the library's name for the file, or with none and why.
    using Imported = std::function<void(std::string filename, QString error)>;
    /// Import the file ``url`` names into the library.
    void import_file(const std::string& url, bool video, Imported done);
    void on_import_finished(qint64 request_id, const QVariantMap& result);

    const session::Session& session_;
    const OpenLibrary& library_;
    MediaController& media_;
    std::map<qint64, Imported> pending_;
};

}  // namespace seventhslide::app
