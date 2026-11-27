#include "source_routing.hpp"

#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/render/output_window_host.hpp>

#include <QTimer>

#include <algorithm>
#include <utility>

namespace seventhslide::app {

namespace {

/// The same sources, with the same pictures.
[[nodiscard]] bool same_pictures(const render::SourceFrames& a, const render::SourceFrames& b)
{
    return std::ranges::equal(a, b, [](const auto& x, const auto& y) {
        return x.first == y.first && x.second.image.cacheKey() == y.second.image.cacheKey();
    });
}

}  // namespace

SourceRouting::SourceRouting(const session::Session& session,
                             audio::Mixer* mixer,
                             std::string provider,
                             std::string sound_type,
                             QObject* parent)
    : QObject(parent)
    , session_(session)
    , mixer_(mixer)
    , provider_(std::move(provider))
    , sound_type_(std::move(sound_type))
{
    clock_.start();
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, [this] { publish(false); });
}

SourceRouting::~SourceRouting()
{
    // The mixer stops reading each sound before its source goes.
    if (mixer_ != nullptr) {
        for (const auto& [source, placed] : placed_) {
            mixer_->remove_source(placed.sound);
        }
    }
}

void SourceRouting::set_output_host(render::OutputWindowHost* host)
{
    host_ = host;
    publish(false);
    if (host_) {
        host_->set_source_frames(provider_, output_frames_);
    }
}

void SourceRouting::set_on_air(std::map<std::string, std::string, std::less<>> on_air)
{
    const qint64 now = clock_.elapsed();
    for (const auto& [source, stack] : on_air_) {
        if (on_air.contains(source)) {
            continue;
        }
        if (const int change_ms = session_.transition_ms_on(stack); change_ms > 0) {
            going_until_[source] = now + change_ms;
        }
    }
    // On the air again before it had gone: it is simply on the air.
    for (const auto& [source, stack] : on_air) {
        going_until_.erase(source);
        gone_.erase(source);
    }
    on_air_ = std::move(on_air);
}

void SourceRouting::set_picture(const std::string& source, QImage picture)
{
    if (picture.isNull()) {
        frames_.erase(source);
    } else {
        frames_[source].image = std::move(picture);
    }
}

void SourceRouting::stop(const std::string& source)
{
    if (const auto it = placed_.find(source); it != placed_.end()) {
        if (mixer_ != nullptr) {
            mixer_->remove_source(it->second.sound);
        }
        placed_.erase(it);
    }
    if (const auto frame = frames_.find(source); frame != frames_.end()) {
        if (going_until_.contains(source)) {
            gone_[source] = frame->second;
        }
        frames_.erase(frame);
    }
}

void SourceRouting::route_sound(const std::string& source,
                                audio::AudioSource* sound,
                                const std::string& strip)
{
    if (sound == nullptr || mixer_ == nullptr) {
        return;
    }
    const auto it = placed_.find(source);
    if (it == placed_.end()) {
        mixer_->add_source(strip, sound, sound_type_);
        placed_.emplace(source, Placed{sound, strip});
    } else if (it->second.strip != strip) {
        mixer_->move_source(sound, strip);
        it->second.strip = strip;
    }
}

std::string SourceRouting::sound_strip(std::string_view source) const
{
    const auto it = placed_.find(source);
    return it != placed_.end() ? it->second.strip : std::string();
}

render::SourceFrames SourceRouting::select()
{
    const qint64 now = clock_.elapsed();
    qint64 next = -1;
    for (auto it = going_until_.begin(); it != going_until_.end();) {
        if (now >= it->second) {
            gone_.erase(it->first);
            it = going_until_.erase(it);
            continue;
        }
        next = next < 0 ? it->second : std::min(next, it->second);
        ++it;
    }
    if (next >= 0) {
        timer_->start(static_cast<int>(next - now));
    } else {
        timer_->stop();
    }

    render::SourceFrames out = gone_;
    for (const auto& [source, frame] : frames_) {
        if (on_air_.contains(source) || going_until_.contains(source)) {
            out[source] = frame;
        }
    }
    return out;
}

void SourceRouting::publish(bool moved)
{
    auto out = select();
    // A cued source's picture is the previews' alone: the outputs are given
    // theirs only when it changed.
    const bool on_outputs = !same_pictures(out, output_frames_);
    output_frames_ = std::move(out);
    if (on_outputs && host_) {
        host_->set_source_frames(provider_, output_frames_);
    }
    if (moved || on_outputs) {
        emit framesChanged();
    }
}

} // namespace seventhslide::app
