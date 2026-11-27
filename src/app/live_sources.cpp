#include "live_sources.hpp"

#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/live_source.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace seventhslide::app {

LiveSources::LiveSources(const session::Session& session, audio::Mixer* mixer, QObject* parent)
    : QObject(parent)
    , session_(session)
    , routing_(session, mixer, "live", std::string(model::k_item_type_live))
{
    connect(&routing_, &SourceRouting::framesChanged, this, &LiveSources::framesChanged);
}

LiveSources::~LiveSources() = default;

void LiveSources::set_factory(LiveReceiverFactory factory)
{
    factory_ = std::move(factory);
}

void LiveSources::set_output_host(render::OutputWindowHost* host)
{
    routing_.set_output_host(host);
}

LiveReceiver* LiveSources::receiver(std::string_view source) const
{
    const auto it = receivers_.find(source);
    return it != receivers_.end() ? it->second.get() : nullptr;
}

std::vector<std::string> LiveSources::keys() const
{
    std::vector<std::string> out;
    for (const auto& [key, receiver] : receivers_) {
        out.push_back(key);
    }
    return out;
}

void LiveSources::sync()
{
    // Each source shown, the stacks it is live on, and those it is cued on.
    struct Shown {
        model::LiveSource source;
        std::vector<std::string> live;
        std::vector<std::string> cued;
    };
    std::map<std::string, Shown, std::less<>> shown;
    for (const auto& stack : session_.stacks()) {
        const auto add = [&](const session::StackRuntime& st, bool live) {
            if (!st.live_source_active()) {
                return;
            }
            const auto source = model::live_source_from_item_data(st.media().data);
            const auto key = model::live_source_key(source);
            if (key.empty()) {
                return;
            }
            auto& entry = shown.try_emplace(key, Shown{source, {}, {}}).first->second;
            (live ? entry.live : entry.cued).push_back(stack.id());
        };
        add(stack.program, true);
        if (stack.preview) {
            add(*stack.preview, false);
        }
    }

    // What has left every Program goes out with the change its stack makes.
    std::map<std::string, std::string, std::less<>> on_air;
    for (const auto& [key, item] : shown) {
        if (!item.live.empty()) {
            on_air.emplace(key, item.live.front());
        }
    }
    routing_.set_on_air(std::move(on_air));

    bool changed = false;
    for (auto it = receivers_.begin(); it != receivers_.end();) {
        if (shown.contains(it->first)) {
            ++it;
            continue;
        }
        routing_.stop(it->first);
        it = receivers_.erase(it);
        changed = true;
    }
    if (factory_) {
        for (const auto& [key, item] : shown) {
            if (receivers_.contains(key)) {
                continue;
            }
            emit aboutToReceive(QString::fromStdString(key));
            // At full quality cued too, so the take changes nothing.
            auto receiver = factory_(item.source, LiveQuality::Full);
            if (!receiver) {
                continue;
            }
            auto* r = receiver.get();
            connect(r, &LiveReceiver::frameChanged, this, [this, key] { take_frame(key); });
            connect(r, &LiveReceiver::problemChanged, this, &LiveSources::stateChanged);
            connect(r, &LiveReceiver::lostChanged, this, &LiveSources::stateChanged);
            // A receiver may have a picture already (a source it shares with
            // another): shown from the start, not from its next one.
            routing_.set_picture(key, r->frame());
            receivers_.emplace(key, std::move(receiver));
            changed = true;
        }
    }
    // Its sound on the strip of a stack it is live on, else the cue strip of
    // one it is cued on: where it is while that still holds, else the first.
    for (const auto& [key, item] : shown) {
        std::vector<std::string> strips = item.live;
        if (strips.empty()) {
            for (const auto& stack : item.cued) {
                strips.push_back(audio::cue_strip_id(stack));
            }
        }
        const auto was = sound_strip(key);
        if (auto* r = receiver(key); r != nullptr) {
            routing_.route_sound(key, r->sound(),
                                 std::ranges::find(strips, was) != strips.end() ? was
                                                                                : strips.front());
        }
    }
    routing_.publish(changed);
    if (changed) {
        emit stateChanged();
    }
}

void LiveSources::take_frame(const std::string& source)
{
    const auto* r = receiver(source);
    if (r == nullptr) {
        return;
    }
    routing_.set_picture(source, r->frame());
    routing_.publish(true);
}

} // namespace seventhslide::app
