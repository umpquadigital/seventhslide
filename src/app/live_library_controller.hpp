#pragma once

// The Live tab: the live sources found now (NDI on the network, capture
// devices and DeckLink inputs on this computer) and the streams added by
// hand, each with a still of its picture as its thumbnail, its frame rate,
// its size once it is live, and its latency; and putting one live or into the
// open service. Sources are looked for every second while the tab shows, off
// the GUI thread (listing devices can take a while). Each still is taken
// once each time the tab shows: from the source's picture when it is live,
// else from a preview stream opened for it and closed as soon as it has one.
// Found sources are not saved; a service remembers a source by its kind and
// name.
// SessionFacade owns one and exposes it as ``session.liveLibrary``.

#include "live_receiver.hpp"
#include "live_source_list_model.hpp"

#include <seventhslide/model/live_source.hpp>
#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::render {
class NdiFinder;
}

namespace seventhslide::app {

class IRundown;
class LiveSources;
class OpenLibrary;

class LiveLibraryController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    /// ``[{kind, kindLabel, name, key, detail, live, signal}]``: the sources
    /// found now, grouped by kind (NDI, capture devices, DeckLink inputs),
    /// by name. ``detail``: "1080p, 59.94 fps" (as received in full; an
    /// NDI source's once it is looked at so). ``signal``: ``ok`` when
    /// its picture last came, ``lost`` when it did not, empty before it is
    /// known.
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    /// ``sources`` that ``search`` matches (db::SearchQuery, by name), as the
    /// tab lists them.
    Q_PROPERTY(seventhslide::app::LiveSourceListModel* rows READ rows CONSTANT)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    /// Look for sources (the Live tab is showing, or a remote has read the
    /// list lately: keep_searching_for); otherwise the list rests. Written by
    /// the tab; read, it says whether sources are looked for at all.
    Q_PROPERTY(bool searching READ searching WRITE setSearching NOTIFY searchingChanged)
    /// Why no sources can be found (no NDI runtime); empty when they can.
    Q_PROPERTY(QString unavailable READ unavailable CONSTANT)

public:
    /// What finds the sources on the network now.
    using Finder = std::function<std::vector<model::LiveSource>()>;

    /// Sources go live on ``session`` and into ``rundown``'s open service;
    /// streams added by hand are kept in ``library``'s settings.
    LiveLibraryController(session::Session& session, IRundown& rundown,
                          OpenLibrary& library, QObject* parent = nullptr);

    /// The library setting the streams are kept in: ``[{name, url}]``.
    static constexpr std::string_view k_streams_setting = "live_streams";
    ~LiveLibraryController() override;

    /// Tests: find with ``finder`` (and say nothing is missing); on the GUI
    /// thread, unless ``in_background`` (as the app's own finder is).
    void set_finder(Finder finder, bool in_background = false);
    /// How the thumbnails' preview streams are received.
    void set_receiver_factory(LiveReceiverFactory factory);
    /// The sources live, for their size and their mark in the list.
    void set_live_sources(LiveSources* sources);
    /// The thumbnail of source ``key``; null until its first picture.
    [[nodiscard]] QImage thumbnail(const QString& key) const;
    /// Counts each time some thumbnail changed (thumbnailsChanged), so a
    /// remote can name the pictures it has.
    [[nodiscard]] std::uint64_t thumbnails_revision() const noexcept { return thumbs_revision_; }
    /// Look for sources for ``lease`` from now, whether or not the tab shows
    /// (a web remote's Live tab, which reads the list again within it). A
    /// later call starts the lease again.
    void keep_searching_for(std::chrono::milliseconds lease);
    /// Preview streams open (tests).
    [[nodiscard]] std::size_t previews() const noexcept { return previews_.size(); }
    /// An NDI source is being looked at in full, for its quality.
    [[nodiscard]] bool measuring() const noexcept { return measuring_.has_value(); }
    /// Looking for sources off the GUI thread now (tests).
    [[nodiscard]] bool scanning() const noexcept { return scanning_; }

    /// At most this many preview streams are open at once.
    static constexpr std::size_t k_max_previews = 16;
    /// A preview stream with no picture by then is closed, and its source
    /// left without a still until the tab shows again.
    static constexpr int k_still_wait_ms = 10000;
    /// A thumbnail's size (16:9; the picture contained).
    static constexpr int k_thumb_w = 320;
    static constexpr int k_thumb_h = 180;

    [[nodiscard]] QVariantList sources() const;
    [[nodiscard]] LiveSourceListModel* rows() noexcept { return &rows_; }
    [[nodiscard]] QString search() const { return search_; }
    void setSearch(const QString& search);
    [[nodiscard]] bool searching() const noexcept { return searching_; }
    void setSearching(bool searching);
    [[nodiscard]] QString unavailable() const;

    /// Look again now (the tab's refresh, and the poll). The list changes
    /// when what is found comes back.
    Q_INVOKABLE void refresh();
    /// Show source ``name`` of ``kind`` on the stack live sources play on:
    /// cued in its Preview when it has one, unless ``live``.
    Q_INVOKABLE bool playSource(const QString& kind, const QString& name, bool live = false);
    /// Add it to the open service, at ``at_index`` (< 0: the end). A
    /// stream's ``name`` is its address.
    Q_INVOKABLE bool addSourceToService(const QString& kind, const QString& name, int at_index = -1);

    // --- streams added by hand ---
    /// The address ``text`` stands for, as it would be saved; empty when it
    /// is not a stream's.
    Q_INVOKABLE QString streamUrlFromText(const QString& text) const;
    /// Save a stream: new (``old_url`` empty) or over the one at ``old_url``.
    /// Returns ``{key}``, or ``{error}`` saying why not.
    Q_INVOKABLE QVariantMap saveStream(const QString& old_url, const QString& name,
                                       const QString& url);
    /// Forget the stream at ``url``. Returns why not; empty when it went.
    Q_INVOKABLE QString deleteStream(const QString& url);

signals:
    void sourcesChanged();
    void searchChanged();
    void searchingChanged();
    /// Some thumbnail has a new picture.
    void thumbnailsChanged();
    /// A source went live (see LibraryController::liveChanged).
    void liveChanged(bool rebuildModels);
    void errorOccurred(const QString& message);

private:
    [[nodiscard]] static model::LiveSource source_of(const QString& kind, const QString& name);
    /// Each source found, as ``sources`` and ``rows`` show it.
    [[nodiscard]] std::vector<LiveSourceListModel::Row> source_rows() const;
    /// ``rows`` from the sources and the search, as they are now.
    void update_rows();
    /// The list from what was found last and the streams kept.
    void take_found();
    /// Take a still of each source found without one this showing: from its
    /// picture when it is live, else from a preview stream (up to the
    /// limit). Close the preview streams with a still, of sources gone or
    /// live, or of the tab hidden.
    void take_stills();
    /// ``take_stills`` soon, once: a preview stream has a picture.
    void take_stills_soon();
    [[nodiscard]] QString detail_of(const model::LiveSource& source) const;
    /// NDI's preview stream is smaller than the source: once its still is
    /// taken, each is looked at in full until its first picture, one at a
    /// time, for its size. Whether a size was learned.
    bool measure();
    /// What ``sources`` says of a source's signal.
    [[nodiscard]] QString signal_of(const std::string& key) const;
    /// Whether that changed it.
    bool set_signal(const std::string& key, bool ok);
    /// The streams the library keeps.
    [[nodiscard]] std::vector<model::LiveSource> saved_streams() const;
    void save_streams(const std::vector<model::LiveSource>& streams);
    /// Start or stop looking, as the tab and the lease say together.
    void update_searching();
    /// A source as the operator names it in a call: a stream by its address.
    [[nodiscard]] model::LiveSource source_from(const QString& kind, const QString& name) const;

    session::Session& session_;
    IRundown& rundown_;
    OpenLibrary& library_;
    Finder finder_;
    bool own_finder_ = true;
    bool in_background_ = true;
    std::unique_ptr<render::NdiFinder> ndi_;
    /// What the finder found last, and the one thread that looks. The thread
    /// stays from one poll's search to the next; the pool lets it go once no
    /// search has come for 30 s.
    std::vector<model::LiveSource> scanned_;
    QThreadPool scan_;
    bool scanning_ = false;
    /// Counts finders set: a search's findings are taken only by the finder
    /// that made them.
    std::uint64_t finder_generation_ = 0;
    std::vector<model::LiveSource> found_;
    LiveReceiverFactory factory_;
    LiveSources* live_sources_ = nullptr;
    struct Preview {
        std::unique_ptr<LiveReceiver> receiver;
        QElapsedTimer opened;
    };
    std::map<std::string, Preview> previews_;
    struct Measuring {
        std::string key;
        std::unique_ptr<LiveReceiver> receiver;
        QElapsedTimer opened;
    };
    std::optional<Measuring> measuring_;
    /// Sources measured this showing (or that gave no picture in time).
    std::set<std::string> measured_;
    /// Each source's picture size, as last received in full.
    std::map<std::string, QSize> sizes_;
    std::map<std::string, QImage> thumbnails_;
    /// Sources with a still this showing, and those whose preview stream
    /// gave none in time.
    std::set<std::string> stilled_;
    std::set<std::string> given_up_;
    /// Each source's signal as last seen: a picture came (true) or did not.
    std::map<std::string, bool> signal_;
    /// Each source's frame rate, as its preview stream said.
    std::map<std::string, double> rates_;
    bool stills_due_ = false;
    LiveSourceListModel rows_;
    QString search_;
    /// Each source's detail as last told, to tell when it changes.
    QStringList details_;
    /// Whether sources are looked for: the tab shows, or the lease runs.
    bool searching_ = false;
    bool tab_searching_ = false;
    QTimer lease_;
    std::uint64_t thumbs_revision_ = 0;
    QTimer poll_;
};

} // namespace seventhslide::app
