#include "web_sources.hpp"

#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/model/content.hpp>

#include <utility>

namespace seventhslide::app {

WebSources::WebSources(const session::Session& session, audio::Mixer* mixer, QObject* parent)
    : QObject(parent)
    , session_(session)
    , routing_(session, mixer, "web", std::string(model::k_item_type_web))
{
    connect(&routing_, &SourceRouting::framesChanged, this, &WebSources::framesChanged);
}

WebSources::~WebSources() = default;

void WebSources::set_factory(WebRendererFactory factory)
{
    factory_ = std::move(factory);
}

void WebSources::set_output_host(render::OutputWindowHost* host)
{
    routing_.set_output_host(host);
}

WebRenderer* WebSources::renderer(std::string_view source) const
{
    const auto it = renderers_.find(source);
    return it != renderers_.end() ? it->second.get() : nullptr;
}

void WebSources::sync()
{
    // Each tab shown, the stack it is on, and whether that is its Program.
    struct Shown {
        scene::SceneWebMedia web;
        std::string stack_id;
        bool live = false;
    };
    std::map<std::string, Shown, std::less<>> shown;
    for (const auto& stack : session_.stacks()) {
        const auto add = [&](const session::StackRuntime& st, bool live) {
            const auto& web = st.scene_extras().web;
            if (st.web_mode_active() && web && !web->source.empty()) {
                shown.try_emplace(web->source, Shown{*web, stack.id(), live});
            }
        };
        add(stack.program, true);
        if (stack.preview) {
            add(*stack.preview, false);
        }
    }

    // What has left a Program goes out with the change its stack makes now.
    std::map<std::string, std::string, std::less<>> on_air;
    for (const auto& [source, tab] : shown) {
        if (tab.live) {
            on_air.emplace(source, tab.stack_id);
        }
    }
    routing_.set_on_air(std::move(on_air));

    bool changed = false;
    for (auto it = renderers_.begin(); it != renderers_.end();) {
        if (shown.contains(it->first)) {
            ++it;
            continue;
        }
        routing_.stop(it->first);
        it = renderers_.erase(it);
        changed = true;
    }
    if (factory_) {
        for (const auto& [source, tab] : shown) {
            const auto& web = tab.web;
            if (renderers_.contains(source)) {
                continue;
            }
            auto renderer = factory_();
            if (!renderer) {
                continue;
            }
            auto* r = renderer.get();
            connect(r, &WebRenderer::frameChanged, this, [this, source] { take_frame(source); });
            connect(r, &WebRenderer::problemChanged, this, &WebSources::stateChanged);
            connect(r, &WebRenderer::navigated, this, &WebSources::stateChanged);
            renderers_.emplace(source, std::move(renderer));
            r->load(QUrl(QString::fromStdString(web.url)), QSize(web.width, web.height), web.zoom);
            changed = true;
        }
    }
    // Each sound on its stack's strip, or its cue strip while cued.
    for (const auto& [source, tab] : shown) {
        if (auto* r = renderer(source); r != nullptr) {
            routing_.route_sound(source, r->sound(),
                                 tab.live ? tab.stack_id : audio::cue_strip_id(tab.stack_id));
        }
    }
    routing_.publish(changed);
    if (changed) {
        emit stateChanged();
    }
}

void WebSources::take_frame(const std::string& source)
{
    const auto* r = renderer(source);
    if (r == nullptr) {
        return;
    }
    const bool first = !frames().contains(source);
    routing_.set_picture(source, r->frame());
    routing_.publish(true);
    if (first && frames().contains(source)) {
        emit stateChanged();  // loaded
    }
}

} // namespace seventhslide::app
