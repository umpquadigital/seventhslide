#include "live_library_controller.hpp"

#include "entry_error.hpp"
#include "refusals.hpp"
#include "ffmpeg_live_input.hpp"
#include "rundown.hpp"
#include "live_sources.hpp"
#include "open_library.hpp"
#include "qml_ids.hpp"

#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/db/search_text.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/render/bmd_input.hpp>
#include <seventhslide/render/ndi_receive.hpp>
#include <seventhslide/render/ndi_runtime.hpp>

#include <nlohmann/json.hpp>

#include <QMetaObject>
#include <QPainter>
#include <QVariantMap>

#include <algorithm>
#include <exception>
#include <optional>
#include <utility>

namespace seventhslide::app {
namespace {

/// How often the list is looked at again while the tab shows.
constexpr int k_poll_ms = 1000;

/// A picture's size as its quality: "1080p", "720p", "4K". By its height,
/// or its width's as if 16:9 (a wider picture is no less sharp); turned on
/// its side, the same.
QString quality_of(QSize size)
{
    const int along = std::max(size.width(), size.height());
    const int across = std::min(size.width(), size.height());
    const int lines = std::max(across, along * 9 / 16);
    if (lines >= 4320) {
        return QStringLiteral("8K");
    }
    if (lines >= 2160) {
        return QStringLiteral("4K");
    }
    for (const int standard : {1440, 1080, 720, 576, 480, 360, 240}) {
        if (lines >= standard) {
            return QStringLiteral("%1p").arg(standard);
        }
    }
    return QStringLiteral("%1p").arg(lines);
}

} // namespace

LiveLibraryController::LiveLibraryController(session::Session& session, IRundown& rundown,
                                             OpenLibrary& library, QObject* parent)
    : QObject(parent)
    , session_(session)
    , rundown_(rundown)
    , library_(library)
{
    connect(this, &LiveLibraryController::sourcesChanged, this, &LiveLibraryController::update_rows);
    scan_.setMaxThreadCount(1);
    poll_.setInterval(k_poll_ms);
    connect(&poll_, &QTimer::timeout, this, &LiveLibraryController::refresh);
    lease_.setSingleShot(true);
    connect(&lease_, &QTimer::timeout, this, &LiveLibraryController::update_searching);
    // NDI's finder is made the first time the tab looks: it listens on the
    // network from then on.
    finder_ = [this] {
        std::vector<model::LiveSource> out;
        if (!ndi_) {
            ndi_ = std::make_unique<render::NdiFinder>();
        }
        for (auto& name : ndi_->sources()) {
            out.push_back({std::string(model::k_live_kind_ndi), std::move(name)});
        }
        for (auto& device : capture_devices()) {
            out.push_back({std::string(model::k_live_kind_device), std::move(device.name)});
        }
        for (const auto& card : render::bmd_list_input_devices()) {
            out.push_back({std::string(model::k_live_kind_decklink), card.name.toStdString()});
        }
        return out;
    };
}

LiveLibraryController::~LiveLibraryController()
{
    // A search under way ends before what it looks with (NDI's finder) goes,
    // and the preview streams' threads before what they report to.
    scan_.waitForDone();
    previews_.clear();
}

void LiveLibraryController::set_finder(Finder finder, bool in_background)
{
    ++finder_generation_;  // what the old finder found is not taken
    scanning_ = false;
    finder_ = std::move(finder);
    own_finder_ = false;
    in_background_ = in_background;
    refresh();
}

void LiveLibraryController::set_receiver_factory(LiveReceiverFactory factory)
{
    factory_ = std::move(factory);
    previews_.clear();
    measuring_.reset();
    measured_.clear();
    stilled_.clear();
    given_up_.clear();
    take_stills();
}

void LiveLibraryController::set_live_sources(LiveSources* sources)
{
    if (live_sources_ != nullptr) {
        disconnect(live_sources_, nullptr, this, nullptr);
    }
    live_sources_ = sources;
    if (live_sources_ != nullptr) {
        // A source going live (or off) changes its mark and, once it has a
        // picture, the size it says; live, its still is of its picture.
        connect(live_sources_, &LiveSources::stateChanged, this, [this] {
            // Kept for when it is off again.
            for (const auto& key : live_sources_->keys()) {
                if (const auto said = signal_of(key); !said.isEmpty()) {
                    signal_[key] = said == QStringLiteral("ok");
                }
                if (const auto* r = live_sources_->receiver(key); r && !r->stats().size.isEmpty()) {
                    sizes_[key] = r->stats().size;
                    measured_.insert(key);
                }
            }
            emit sourcesChanged();
            take_stills_soon();
        });
        // A device opens for one reader: its preview stream lets go before
        // it is received live.
        connect(live_sources_, &LiveSources::aboutToReceive, this, [this](const QString& key) {
            previews_.erase(key.toStdString());
            if (measuring_ && measuring_->key == key.toStdString()) {
                measuring_.reset();
            }
        });
    }
}

QImage LiveLibraryController::thumbnail(const QString& key) const
{
    const auto it = thumbnails_.find(key.toStdString());
    return it != thumbnails_.end() ? it->second : QImage();
}

QString LiveLibraryController::detail_of(const model::LiveSource& source) const
{
    const auto key = model::live_source_key(source);
    QStringList parts;
    // The size only from the source received in full: NDI's preview is
    // smaller.
    const auto* live = live_sources_ != nullptr ? live_sources_->receiver(key) : nullptr;
    if (live != nullptr && !live->stats().size.isEmpty()) {
        parts << quality_of(live->stats().size);
    } else if (const auto it = sizes_.find(key); it != sizes_.end()) {
        parts << quality_of(it->second);
    }
    double rate = live != nullptr ? live->stats().frame_rate : 0.0;
    if (rate <= 0.0) {
        if (const auto it = rates_.find(key); it != rates_.end()) {
            rate = it->second;
        }
    }
    if (rate > 0.0) {
        parts << tr("%1 fps").arg(QString::number(rate, 'g', 4));
    }
    return parts.join(QStringLiteral(", "));
}

QString LiveLibraryController::signal_of(const std::string& key) const
{
    // Live: as it is now; otherwise as its still (or live) last said.
    std::optional<bool> ok;
    if (const auto* r = live_sources_ != nullptr ? live_sources_->receiver(key) : nullptr) {
        if (r->lost()) {
            ok = false;
        } else if (!r->frame().isNull()) {
            ok = true;
        }
    }
    if (const auto it = signal_.find(key); !ok && it != signal_.end()) {
        ok = it->second;
    }
    return !ok ? QString() : *ok ? QStringLiteral("ok") : QStringLiteral("lost");
}

std::vector<LiveSourceListModel::Row> LiveLibraryController::source_rows() const
{
    std::vector<LiveSourceListModel::Row> out;
    out.reserve(found_.size());
    for (const auto& s : found_) {
        const auto key = model::live_source_key(s);
        out.push_back({.key = QString::fromStdString(key),
                       .kind = QString::fromStdString(s.kind),
                       .kind_label = s.kind == model::k_live_kind_ndi        ? tr("NDI")
                                     : s.kind == model::k_live_kind_device   ? tr("Capture")
                                     : s.kind == model::k_live_kind_decklink ? tr("DeckLink")
                                                                             : tr("Stream"),
                       .name = QString::fromStdString(s.name),
                       .url = QString::fromStdString(s.url),
                       .detail = detail_of(s),
                       .signal = signal_of(key)});
    }
    return out;
}

QVariantList LiveLibraryController::sources() const
{
    QVariantList out;
    const auto live = live_sources_ != nullptr ? live_sources_->keys() : std::vector<std::string>{};
    for (const auto& row : source_rows()) {
        const bool on = std::ranges::find(live, row.key.toStdString()) != live.end();
        out.push_back(QVariantMap{{QStringLiteral("kind"), row.kind},
                                  {QStringLiteral("name"), row.name},
                                  {QStringLiteral("key"), row.key},
                                  {QStringLiteral("kindLabel"), row.kind_label},
                                  {QStringLiteral("url"), row.url},
                                  {QStringLiteral("detail"), row.detail},
                                  {QStringLiteral("live"), on},
                                  {QStringLiteral("signal"), row.signal}});
    }
    return out;
}

void LiveLibraryController::setSearch(const QString& search)
{
    if (search == search_) {
        return;
    }
    search_ = search;
    update_rows();
    emit searchChanged();
}

void LiveLibraryController::update_rows()
{
    const db::SearchQuery query(search_.trimmed());
    auto rows = source_rows();
    std::erase_if(rows, [&query](const LiveSourceListModel::Row& row) { return !query.matches(row.name); });
    rows_.set_rows(rows);
}

void LiveLibraryController::take_stills()
{
    stills_due_ = false;
    // Contained in 16:9 over black.
    const auto still_of = [](const QImage& picture) {
        QImage thumb(k_thumb_w, k_thumb_h, QImage::Format_RGB32);
        thumb.fill(Qt::black);
        QPainter p(&thumb);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSize fitted = picture.size().scaled(k_thumb_w, k_thumb_h, Qt::KeepAspectRatio);
        p.drawImage(QRect((k_thumb_w - fitted.width()) / 2, (k_thumb_h - fitted.height()) / 2,
                          fitted.width(), fitted.height()),
                    picture);
        p.end();
        return thumb;
    };
    bool moved = false;
    bool told = false;  // what a row says changed
    std::map<std::string, model::LiveSource> wanted;
    for (const auto& s : searching_ ? found_ : std::vector<model::LiveSource>{}) {
        const auto key = model::live_source_key(s);
        if (stilled_.contains(key)) {
            continue;
        }
        // Live: its picture, rather than opening it twice.
        if (const auto* live = live_sources_ != nullptr ? live_sources_->receiver(key) : nullptr) {
            if (const QImage picture = live->frame(); !picture.isNull()) {
                thumbnails_[key] = still_of(picture);
                stilled_.insert(key);
                moved = true;
            }
            continue;
        }
        // A stream has no small preview, and may take one reader only (a
        // listener): its still is of its picture while it is live.
        if (s.kind == model::k_live_kind_stream || given_up_.contains(key) || !factory_) {
            continue;
        }
        if (const auto it = previews_.find(key); it != previews_.end()) {
            const auto* r = it->second.receiver.get();
            if (const QImage picture = r->frame(); !picture.isNull()) {
                thumbnails_[key] = still_of(picture);
                rates_[key] = r->stats().frame_rate;
                // Only NDI's preview stream is smaller than its source.
                if (s.kind != model::k_live_kind_ndi && !r->stats().size.isEmpty()) {
                    sizes_[key] = r->stats().size;
                }
                stilled_.insert(key);
                set_signal(key, true);
                told = true;  // its frame rate, its size
                moved = true;
                continue;  // its stream closes below
            }
            if (it->second.opened.hasExpired(k_still_wait_ms)) {
                given_up_.insert(key);
                told = set_signal(key, false) || told;
                continue;
            }
        }
        if (wanted.size() < k_max_previews) {
            wanted.emplace(key, s);
        }
    }
    std::erase_if(previews_, [&](const auto& entry) { return !wanted.contains(entry.first); });
    bool pictured = false;
    for (const auto& [key, source] : wanted) {
        if (previews_.contains(key)) {
            continue;
        }
        auto receiver = factory_(source, LiveQuality::Preview);
        if (!receiver) {
            given_up_.insert(key);
            told = set_signal(key, false) || told;
            continue;
        }
        connect(receiver.get(), &LiveReceiver::frameChanged, this,
                &LiveLibraryController::take_stills_soon);
        pictured = pictured || !receiver->frame().isNull();
        Preview preview{std::move(receiver), {}};
        preview.opened.start();
        previews_.emplace(key, std::move(preview));
    }
    // Sources gone take what was kept of them.
    const auto gone = [&](const std::string& key) {
        return std::ranges::none_of(found_, [&](const auto& s) { return model::live_source_key(s) == key; });
    };
    const auto gone_entry = [&](const auto& entry) { return gone(entry.first); };
    if (std::erase_if(thumbnails_, gone_entry) > 0) {
        moved = true;
    }
    std::erase_if(rates_, gone_entry);
    std::erase_if(signal_, gone_entry);
    std::erase_if(sizes_, gone_entry);
    std::erase_if(measured_, gone);
    if (measuring_ && gone(measuring_->key)) {
        measuring_.reset();
    }
    told = measure() || told;
    std::erase_if(stilled_, gone);
    std::erase_if(given_up_, gone);
    if (moved) {
        ++thumbs_revision_;
        emit thumbnailsChanged();
    }
    if (told) {
        emit sourcesChanged();
    }
    // A preview stream with a picture as it opened gives its still now.
    if (pictured) {
        take_stills();
    }
}

bool LiveLibraryController::set_signal(const std::string& key, bool ok)
{
    const auto [it, added] = signal_.try_emplace(key, ok);
    if (!added && it->second == ok) {
        return false;
    }
    it->second = ok;
    return true;
}

bool LiveLibraryController::measure()
{
    bool learned = false;
    if (measuring_) {
        const auto* r = measuring_->receiver.get();
        const QSize size = r->stats().size;
        if (r->frame().isNull() || size.isEmpty()) {
            if (!measuring_->opened.hasExpired(k_still_wait_ms)) {
                return false;
            }
        } else {
            learned = sizes_[measuring_->key] != size;
            sizes_[measuring_->key] = size;
        }
        measured_.insert(measuring_->key);
        measuring_.reset();
    }
    if (!searching_ || !factory_) {
        return learned;
    }
    for (const auto& s : found_) {
        const auto key = model::live_source_key(s);
        // One live says its size itself.
        if (s.kind != model::k_live_kind_ndi || !stilled_.contains(key) || measured_.contains(key)
            || (live_sources_ != nullptr && live_sources_->receiver(key) != nullptr)) {
            continue;
        }
        auto receiver = factory_(s, LiveQuality::Full);
        if (!receiver) {
            measured_.insert(key);
            continue;
        }
        connect(receiver.get(), &LiveReceiver::frameChanged, this,
                &LiveLibraryController::take_stills_soon);
        const bool pictured = !receiver->frame().isNull();
        measuring_ = Measuring{key, std::move(receiver), {}};
        measuring_->opened.start();
        // A picture as it opened: its size now, and on to the next.
        if (pictured) {
            learned = measure() || learned;
        }
        break;
    }
    return learned;
}

void LiveLibraryController::take_stills_soon()
{
    if (stills_due_) {
        return;
    }
    stills_due_ = true;
    // Not from inside a preview stream's own signal: it may be closed.
    QTimer::singleShot(0, this, [this] {
        if (stills_due_) {
            take_stills();
        }
    });
}

void LiveLibraryController::setSearching(bool searching)
{
    tab_searching_ = searching;
    update_searching();
}

void LiveLibraryController::keep_searching_for(std::chrono::milliseconds lease)
{
    lease_.start(lease);
    update_searching();
}

void LiveLibraryController::update_searching()
{
    const bool searching = tab_searching_ || lease_.isActive();
    if (searching_ == searching) {
        return;
    }
    searching_ = searching;
    if (searching_) {
        // Stills taken again, each kept until its next one comes.
        stilled_.clear();
        given_up_.clear();
        measured_.clear();
        refresh();
        poll_.start();
    } else {
        poll_.stop();
        measuring_.reset();
        take_stills();  // closes the preview streams: the tab is hidden
    }
    emit searchingChanged();
}

QString LiveLibraryController::unavailable() const
{
    if (!own_finder_ || render::ndi_receive_available()) {
        return {};
    }
    return render::ndi_runtime_available()
               ? tr("This NDI runtime cannot receive sources. Install a newer NDI runtime.")
               : render::ndi_missing_message();
}

void LiveLibraryController::refresh()
{
    if (!in_background_) {
        try {
            scanned_ = finder_ ? finder_() : std::vector<model::LiveSource>{};
        } catch (const std::exception& ex) {
            emit errorOccurred(app::report_entry_error(__func__, ex));
        }
        take_found();
        return;
    }
    // The streams kept are listed at once; what is found when the search
    // comes back. One search at a time: the poll asks again.
    take_found();
    if (scanning_) {
        return;
    }
    scanning_ = true;
    scan_.start([this, find = finder_, generation = finder_generation_] {
        std::vector<model::LiveSource> found;
        std::exception_ptr failed;
        try {
            if (find) {
                found = find();
            }
        } catch (...) {
            failed = std::current_exception();
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, found = std::move(found), failed]() mutable {
                if (generation != finder_generation_) {
                    return;
                }
                scanning_ = false;
                if (failed) {
                    try {
                        std::rethrow_exception(failed);
                    } catch (const std::exception& ex) {
                        emit errorOccurred(app::report_entry_error("refresh", ex));
                    } catch (...) {
                        emit errorOccurred(tr("Live sources could not be looked for"));
                    }
                    return;
                }
                scanned_ = std::move(found);
                take_found();
            },
            Qt::QueuedConnection);
    });
}

void LiveLibraryController::take_found()
try {
    auto found = scanned_;
    for (auto& stream : saved_streams()) {
        found.push_back(std::move(stream));
    }
    // Grouped by kind (NDI, devices, DeckLink inputs, streams), by name
    // within each.
    const auto rank = [](const model::LiveSource& s) {
        return s.kind == model::k_live_kind_ndi        ? 0
               : s.kind == model::k_live_kind_device   ? 1
               : s.kind == model::k_live_kind_decklink ? 2
                                                       : 3;
    };
    std::ranges::sort(found, [&](const auto& a, const auto& b) {
        return rank(a) != rank(b) ? rank(a) < rank(b) : a.name < b.name;
    });
    bool changed = found != found_;
    if (changed) {
        found_ = std::move(found);
    }
    take_stills();
    // A frame rate learned, or a size once live, changes what a row says.
    QStringList details;
    for (const auto& s : found_) {
        details << detail_of(s);
    }
    if (details != details_) {
        details_ = details;
        changed = true;
    }
    if (changed) {
        emit sourcesChanged();
    }
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
}

model::LiveSource LiveLibraryController::source_of(const QString& kind, const QString& name)
{
    return model::live_source_from_item_data(
        model::live_item_data({kind.toStdString(), name.toStdString(), {}}));
}

model::LiveSource LiveLibraryController::source_from(const QString& kind, const QString& name) const
{
    if (kind.toStdString() != model::k_live_kind_stream) {
        return source_of(kind, name);
    }
    // A stream by its address, under the name it is saved with.
    const auto url = model::stream_url_from_text(name.toStdString());
    if (!url) {
        return {};
    }
    for (const auto& s : saved_streams()) {
        if (s.url == *url) {
            return s;
        }
    }
    return {std::string(model::k_live_kind_stream), model::stream_display_name(*url), *url};
}

std::vector<model::LiveSource> LiveLibraryController::saved_streams() const
{
    std::vector<model::LiveSource> out;
    auto* config = library_.config();
    if (config == nullptr) {
        return out;
    }
    const auto stored = config->get_setting(k_streams_setting);
    if (!stored || !stored->is_array()) {
        return out;
    }
    for (const auto& entry : *stored) {
        if (!entry.is_object()) {
            continue;
        }
        auto j = entry;
        j["kind"] = model::k_live_kind_stream;
        if (!j.contains("name")) {
            j["name"] = "";
        }
        if (auto s = model::live_source_from_item_data(j); !s.empty()) {
            out.push_back(std::move(s));
        }
    }
    return out;
}

void LiveLibraryController::save_streams(const std::vector<model::LiveSource>& streams)
{
    auto* config = library_.config();
    if (config == nullptr) {
        return;
    }
    nlohmann::json list = nlohmann::json::array();
    for (const auto& s : streams) {
        list.push_back({{"name", s.name}, {"url", s.url}});
    }
    config->set_setting(k_streams_setting, list);
}

QString LiveLibraryController::streamUrlFromText(const QString& text) const
{
    const auto url = model::stream_url_from_text(text.toStdString());
    return url ? QString::fromStdString(*url) : QString();
}

QVariantMap LiveLibraryController::saveStream(const QString& old_url, const QString& name,
                                              const QString& url)
try {
    if (library_.config() == nullptr) {
        return {{QStringLiteral("error"), refusal::no_library()}};
    }
    const auto address = model::stream_url_from_text(url.toStdString());
    if (!address) {
        return {{QStringLiteral("error"), tr("That is not a stream address")}};
    }
    auto streams = saved_streams();
    const auto old = model::stream_url_from_text(old_url.toStdString());
    std::erase_if(streams, [&](const auto& s) { return old && s.url == *old; });
    if (std::ranges::any_of(streams, [&](const auto& s) { return s.url == *address; })) {
        return {{QStringLiteral("error"), tr("A stream at that address is saved already")}};
    }
    const auto named = name.trimmed().toStdString();
    model::LiveSource stream{std::string(model::k_live_kind_stream),
                             named.empty() ? model::stream_display_name(*address) : named, *address};
    streams.push_back(stream);
    save_streams(streams);
    refresh();
    return {{QStringLiteral("key"), QString::fromStdString(model::live_source_key(stream))}};
} catch (const std::exception& ex) {
    return {{QStringLiteral("error"), app::report_entry_error(__func__, ex)}};
}

QString LiveLibraryController::deleteStream(const QString& url)
try {
    auto streams = saved_streams();
    const auto address = model::stream_url_from_text(url.toStdString());
    const auto before = streams.size();
    std::erase_if(streams, [&](const auto& s) { return address && s.url == *address; });
    if (streams.size() == before) {
        return tr("That stream is no longer saved.");
    }
    save_streams(streams);
    refresh();
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

bool LiveLibraryController::playSource(const QString& kind, const QString& name, bool live)
try {
    const auto source = source_from(kind, name);
    if (source.empty()) {
        return false;
    }
    session::LiveMediaContent media;
    media.kind = model::ContentKind::Live;
    media.data = model::live_item_data(source);
    session_.play_media(std::move(media), {},
                        live ? session::Land::Live : session::Land::Cue);
    emit liveChanged(true);
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool LiveLibraryController::addSourceToService(const QString& kind, const QString& name, int at_index)
try {
    const auto source = source_from(kind, name);
    if (source.empty()) {
        return false;
    }
    return rundown_.append_service_item(model::k_item_type_live, model::live_item_data(source),
                                        std::nullopt, optional_index(at_index));
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

} // namespace seventhslide::app
