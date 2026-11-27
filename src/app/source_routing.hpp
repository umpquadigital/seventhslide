#pragma once

// Where one kind of source's pictures and sound go: pages by tab
// (WebSources), cameras by device (LiveSources). Each keeps one, beside the
// renderers or receivers it runs, and tells it after every sync which sides
// show what.
//
// Pictures: every source's latest one is the previews' (``frames``). The
// outputs are given those of the sources on a Program now, and of those that
// have left one until the change their stack made then is over, since the
// outgoing side of that change still paints them (``output_frames``). A
// source stopped meanwhile leaves its last picture there for that time. A
// source on a Preview alone is the previews' only, and the outputs are given
// their pictures again only when those change.
//
// Sound: each source's is on the mixer strip it is routed to (its stack's, or
// a cue strip while it is cued), moved there unbroken from where it was.

#include <seventhslide/render/source_frames.hpp>
#include <seventhslide/session/session.hpp>

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>

#include <map>
#include <string>
#include <string_view>

class QTimer;

namespace seventhslide::audio {
class AudioSource;
class Mixer;
}
namespace seventhslide::render {
class OutputWindowHost;
}

namespace seventhslide::app {

class SourceRouting : public QObject {
    Q_OBJECT

public:
    /// ``provider`` names the pictures given to the output host ("web",
    /// "live"); ``sound_type`` is the type of sound the mixer files each
    /// source under. Sound plays on ``mixer``'s strips; null leaves it silent
    /// (tests). ``mixer`` must outlive this.
    SourceRouting(const session::Session& session,
                  audio::Mixer* mixer,
                  std::string provider,
                  std::string sound_type,
                  QObject* parent = nullptr);
    /// Takes every sound off the mixer: the sources must outlive this.
    ~SourceRouting() override;

    /// Where the pictures go; null detaches (tests, headless).
    void set_output_host(render::OutputWindowHost* host);

    /// The sources on a Program now, each with the stack it is on (one of
    /// them, for a source on several). One no longer here goes out with the
    /// change its stack makes now: call after the session has changed.
    void set_on_air(std::map<std::string, std::string, std::less<>> on_air);
    /// ``source``'s latest picture; a null one forgets it. Given to the
    /// outputs at the next ``publish``.
    void set_picture(const std::string& source, QImage picture);
    /// ``source`` stops (nothing shows it any more): its sound comes off the
    /// mixer, and its last picture stays on the outputs while it is still
    /// going out.
    void stop(const std::string& source);
    /// Put ``source``'s sound, ``sound``, on strip ``strip``, moving it there
    /// unbroken from where it is. Nothing with no sound or no mixer.
    void route_sound(const std::string& source, audio::AudioSource* sound, const std::string& strip);
    /// The mixer strip ``source``'s sound is on; empty when none.
    [[nodiscard]] std::string sound_strip(std::string_view source) const;

    /// Work out what the outputs are given and let go of what has gone out
    /// with its change; hand it to the host when it changed, and announce
    /// ``framesChanged`` then or when ``moved`` (a picture, or a source,
    /// changed).
    void publish(bool moved);

    /// Every source's latest picture, live or cued: what the previews paint.
    [[nodiscard]] const render::SourceFrames& frames() const noexcept { return frames_; }
    /// What the outputs are given.
    [[nodiscard]] const render::SourceFrames& output_frames() const noexcept
    {
        return output_frames_;
    }

signals:
    /// A picture changed, a source started or stopped, or what the outputs
    /// are given changed.
    void framesChanged();

private:
    /// What the outputs show of ``frames_``: the sources on the air or still
    /// going out, and the last pictures of those stopped meanwhile. Lets go
    /// of what has gone out, and sets the timer for what goes next.
    [[nodiscard]] render::SourceFrames select();

    const session::Session& session_;
    audio::Mixer* mixer_ = nullptr;
    const std::string provider_;
    const std::string sound_type_;
    QPointer<render::OutputWindowHost> host_;
    render::SourceFrames frames_;
    render::SourceFrames output_frames_;
    /// By source: the stack it is on the air on.
    std::map<std::string, std::string, std::less<>> on_air_;
    /// By source: until when (``clock_``) it is still on the outputs.
    std::map<std::string, qint64, std::less<>> going_until_;
    /// The last pictures of sources stopped while going out.
    render::SourceFrames gone_;
    struct Placed {
        audio::AudioSource* sound = nullptr;
        std::string strip;
    };
    /// By source: its sound, and the strip it is on.
    std::map<std::string, Placed, std::less<>> placed_;
    QElapsedTimer clock_;
    /// Fires when the next of ``going_until_`` is due.
    QTimer* timer_ = nullptr;
};

} // namespace seventhslide::app
