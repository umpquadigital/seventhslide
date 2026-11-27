#include "webengine_renderer.hpp"

#include "web_page_sound.hpp"

#include <seventhslide/audio/stream_source.hpp>

#include <QCoreApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QFocusEvent>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QTimer>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <rhi/qrhi.h>

#include <QVariant>

#include <algorithm>
#include <memory>
#include <vector>

namespace seventhslide::app {
namespace {

/// At most this often a page is drawn and read back (~30 fps), however often
/// it asks.
constexpr int k_frame_ms = 33;

/// The page's view. Muted: its sound reaches the mixer through
/// web_page_sound.hpp's script instead, so it is heard only through its
/// stack's fader. No scroll bars on an output; media starts without a click,
/// as a countdown page expects.
constexpr char k_view_qml[] = R"(
import QtQuick
import QtWebChannel
import QtWebEngine

WebEngineView {
    id: view
    property real pageZoom: 1.0
    signal problem(string text)
    signal loaded()
    signal navigated()

    onUrlChanged: view.navigated()
    onCanGoBackChanged: view.navigated()
    // A link to a new window (target=_blank, window.open) opens here: an
    // off-screen page has no other window to open it in. Loaded as a link
    // is, so Previous comes back from it.
    onNewWindowRequested: function (request) { view.url = request.requestedUrl }

    webChannel: WebChannel { id: channel }
    /// Hear the page through ``listener``: ``code`` runs in every frame of
    /// it, before the page's own scripts.
    function listen(code, listener) {
        channel.registerObjects({ seventhslideSound: listener })
        const script = WebEngine.script()
        script.name = "seventhslide-sound"
        script.sourceCode = code
        script.injectionPoint = WebEngineScript.DocumentCreation
        script.worldId = WebEngineScript.MainWorld
        script.runsOnSubFrames = true
        userScripts.insert(script)
    }

    audioMuted: true
    backgroundColor: "black"
    settings.showScrollBars: false
    settings.playbackRequiresUserGesture: false
    onPageZoomChanged: view.zoomFactor = view.pageZoom

    // Chromium's own pop-ups (its menu, alert and confirm boxes, file and
    // colour pickers, sign-in boxes, tooltips) would be drawn into the page,
    // and so onto the outputs: each is turned down. Leaving the page is let
    // through, or a page that asks first could not be reloaded.
    onContextMenuRequested: function (request) { request.accepted = true }
    onJavaScriptDialogRequested: function (request) {
        request.accepted = true
        if (request.type === JavaScriptDialogRequest.DialogTypeBeforeUnload)
            request.dialogAccept()
        else
            request.dialogReject()
    }
    onFileDialogRequested: function (request) {
        request.accepted = true
        request.dialogReject()
    }
    onColorDialogRequested: function (request) {
        request.accepted = true
        request.dialogReject()
    }
    onAuthenticationDialogRequested: function (request) {
        request.accepted = true
        request.dialogReject()
    }
    onTooltipRequested: function (request) { request.accepted = true }
    onLoadingChanged: function (info) {
        if (info.status === WebEngineView.LoadSucceededStatus) {
            // Chromium keeps a zoom per site; this page's is its own.
            view.zoomFactor = view.pageZoom
            view.problem("")
            view.loaded()
        } else if (info.status === WebEngineView.LoadFailedStatus) {
            view.problem(info.errorString)
        }
    }
}
)";

/// Qt WebChannel's client and the sound script, in one function so neither
/// leaves names in the page; empty when the client is missing.
QString page_sound_script()
{
    static const QString script = [] {
        QFile client(QStringLiteral(":/qtwebchannel/qwebchannel.js"));
        if (!client.open(QIODevice::ReadOnly)) {
            qWarning("SeventhSlide: Qt WebChannel's qwebchannel.js is missing: pages are silent");
            return QString();
        }
        return QStringLiteral("(function () {\n") + QString::fromUtf8(client.readAll())
               + QString::fromUtf8(k_web_page_sound_script) + QStringLiteral("\n})();\n");
    }();
    return script;
}

/// What a page's script hands its sound to, over Qt WebChannel.
class PageSound : public QObject {
    Q_OBJECT

public:
    explicit PageSound(audio::StreamMix& sink)
        : sink_(sink)
    {
    }

public slots:
    /// ``data``: base64 of 16-bit stereo frames at ``rate``, of ``stream``.
    void push(int stream, int rate, const QString& data)
    {
        const QByteArray bytes = QByteArray::fromBase64(data.toLatin1());
        const auto* pcm = reinterpret_cast<const qint16*>(bytes.constData());
        const auto samples = static_cast<std::size_t>(bytes.size()) / sizeof(qint16);
        const std::size_t frames = samples / audio::k_channels;
        frames_.resize(frames * audio::k_channels);
        for (std::size_t i = 0; i < frames_.size(); ++i) {
            frames_[i] = static_cast<float>(pcm[i]) / 32768.0f;
        }
        sink_.push(stream, frames_.data(), frames, rate);
    }

private:
    audio::StreamMix& sink_;
    std::vector<float> frames_;
};

/// Where pages keep what they store (sign-ins, cookies, local storage) and
/// their cache, under the ``persistentStoragePath`` the factory gives.
constexpr char k_profile_qml[] = R"(
import QtWebEngine

WebEngineProfile {
    storageName: "pages"
    offTheRecord: false
}
)";

/// One engine for every page's view.
QQmlEngine& view_engine()
{
    static QPointer<QQmlEngine> engine;
    if (!engine) {
        engine = new QQmlEngine(QCoreApplication::instance());
    }
    return *engine;
}

class WebEngineRenderer final : public WebRenderer {
    Q_OBJECT

public:
    /// ``profile``: the WebEngineProfile pages keep what they store in;
    /// null for one that keeps nothing.
    explicit WebEngineRenderer(QObject* profile)
        : profile_(profile)
        , listener_(sound_)
    {
        timer_.setSingleShot(true);
        connect(&timer_, &QTimer::timeout, this, &WebEngineRenderer::draw);
        connect(&control_, &QQuickRenderControl::renderRequested, this,
                &WebEngineRenderer::schedule);
        connect(&control_, &QQuickRenderControl::sceneChanged, this,
                &WebEngineRenderer::schedule);
    }

    ~WebEngineRenderer() override
    {
        delete view_;
        if (window_) {
            window_->setRenderTarget(QQuickRenderTarget());
        }
        target_.reset();
        pass_.reset();
        depth_.reset();
        texture_.reset();
        window_.reset();
    }

    void load(const QUrl& url, QSize size, double zoom) override
    {
        if (!start(size) || view_ == nullptr) {
            return;
        }
        view_->setProperty("pageZoom", zoom);
        view_->setProperty("url", url);
    }

    void reload() override
    {
        if (view_ != nullptr) {
            QMetaObject::invokeMethod(view_, "reload");
        }
    }

    [[nodiscard]] QImage frame() const override { return frame_; }
    [[nodiscard]] QString problem() const override { return problem_; }

    [[nodiscard]] QUrl url() const override
    {
        return view_ != nullptr ? view_->property("url").toUrl() : QUrl();
    }
    [[nodiscard]] bool can_go_back() const override
    {
        return view_ != nullptr && view_->property("canGoBack").toBool();
    }
    void go_back() override
    {
        if (view_ != nullptr) {
            QMetaObject::invokeMethod(view_, "goBack");
        }
    }
    bool navigate(const QUrl& url) override
    {
        if (view_ == nullptr) {
            return false;
        }
        // The last page's problem is not this one's: none while it loads.
        set_problem(QString());
        view_->setProperty("url", url);
        return true;
    }
    [[nodiscard]] audio::AudioSource* sound() override { return &sound_; }

    bool send_input(QEvent* event) override
    {
        if (!window_ || view_ == nullptr || event == nullptr) {
            return false;
        }
        if (event->type() == QEvent::FocusOut) {
            // Given back: the page has no keys (and no caret) until worked again.
            if (focused_) {
                QCoreApplication::sendEvent(window_.get(), event);
                focused_ = false;
            }
            return true;
        }
        if (!focused_) {
            // An off-screen window is never activated, so the page would never
            // see a key: tell the window it has focus, and give it to the view.
            QFocusEvent in(QEvent::FocusIn, Qt::OtherFocusReason);
            QCoreApplication::sendEvent(window_.get(), &in);
            view_->forceActiveFocus(Qt::OtherFocusReason);
            focused_ = true;
        }
        QCoreApplication::sendEvent(window_.get(), event);
        return true;
    }

private slots:
    void on_problem(const QString& text)
    {
        const QString next = text.isEmpty()
            ? QString()
            : QCoreApplication::translate("WebEngineRenderer",
                                          "The page could not be loaded: %1")
                  .arg(text);
        set_problem(next);
    }

private:
    void set_problem(const QString& text)
    {
        if (text == problem_) {
            return;
        }
        problem_ = text;
        emit problemChanged();
    }

    /// Say why the page cannot be drawn, and let go of what was made towards
    /// drawing it: the renderer stays as it was before ``start``.
    void fail(const QString& why)
    {
        set_problem(why);
        if (window_) {
            window_->setRenderTarget(QQuickRenderTarget());
        }
        target_.reset();
        pass_.reset();
        depth_.reset();
        texture_.reset();
        window_.reset();
        failed_ = true;
    }

    /// The off-screen window, its render target and the view, at ``size``.
    bool start(QSize size)
    {
        if (window_ || failed_) {
            return !failed_;
        }
        window_ = std::make_unique<QQuickWindow>(&control_);
        if (!control_.initialize()) {
            fail(QCoreApplication::translate(
                "WebEngineRenderer", "Web pages cannot be drawn on this computer's graphics."));
            return false;
        }
        QRhi* rhi = control_.rhi();
        texture_.reset(rhi->newTexture(QRhiTexture::RGBA8, size, 1,
                                       QRhiTexture::RenderTarget
                                           | QRhiTexture::UsedAsTransferSource));
        depth_.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
        if (!texture_->create() || !depth_->create()) {
            fail(QCoreApplication::translate(
                "WebEngineRenderer", "The page is too large to draw on this computer."));
            return false;
        }
        QRhiTextureRenderTargetDescription desc{QRhiColorAttachment(texture_.get())};
        desc.setDepthStencilBuffer(depth_.get());
        target_.reset(rhi->newTextureRenderTarget(desc));
        pass_.reset(target_->newCompatibleRenderPassDescriptor());
        target_->setRenderPassDescriptor(pass_.get());
        if (!target_->create()) {
            fail(QCoreApplication::translate(
                "WebEngineRenderer", "The page is too large to draw on this computer."));
            return false;
        }
        window_->setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(target_.get()));
        window_->setGeometry(0, 0, size.width(), size.height());

        QQmlComponent component(&view_engine());
        component.setData(k_view_qml, QUrl());
        view_ = qobject_cast<QQuickItem*>(component.create());
        if (view_ == nullptr) {
            qWarning("SeventhSlide: web page view: %s", qPrintable(component.errorString()));
            fail(QCoreApplication::translate("WebEngineRenderer",
                                             "Web pages cannot be shown in this build."));
            return false;
        }
        if (profile_ != nullptr) {
            // Before the page loads: a view's profile is fixed once it has.
            view_->setProperty("profile", QVariant::fromValue(profile_.data()));
        }
        if (const QString script = page_sound_script(); !script.isEmpty()) {
            QMetaObject::invokeMethod(view_, "listen", Q_ARG(QVariant, script),
                                      Q_ARG(QVariant, QVariant::fromValue<QObject*>(&listener_)));
        }
        view_->setParentItem(window_->contentItem());
        view_->setSize(QSizeF(size));
        connect(view_, SIGNAL(problem(QString)), this, SLOT(on_problem(QString)));
        connect(view_, SIGNAL(loaded()), this, SIGNAL(loaded()));
        connect(view_, SIGNAL(navigated()), this, SIGNAL(navigated()));
        schedule();
        return true;
    }

    void schedule()
    {
        if (timer_.isActive() || !target_) {
            return;
        }
        const qint64 since = since_.isValid() ? since_.elapsed() : k_frame_ms;
        timer_.start(static_cast<int>(std::max<qint64>(0, k_frame_ms - since)));
    }

    void draw()
    {
        if (!target_) {
            return;
        }
        since_.start();
        control_.polishItems();
        control_.beginFrame();
        control_.sync();
        control_.render();
        QRhi* rhi = control_.rhi();
        QRhiReadbackResult result;
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        batch->readBackTexture(texture_.get(), &result);
        control_.commandBuffer()->resourceUpdate(batch);
        control_.endFrame();
        if (result.data.isEmpty()) {
            return;
        }
        const QImage wrapped(reinterpret_cast<const uchar*>(result.data.constData()),
                             result.pixelSize.width(), result.pixelSize.height(),
                             QImage::Format_RGBA8888_Premultiplied);
        // The format the compositor draws without converting, once here
        // rather than on every output.
        QImage next = wrapped.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (rhi->isYUpInFramebuffer()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            next = std::move(next).flipped(Qt::Vertical);
#else
            next = std::move(next).mirrored();
#endif
        }
        frame_ = std::move(next);
        emit frameChanged();
    }


    QQuickRenderControl control_;
    std::unique_ptr<QQuickWindow> window_;
    std::unique_ptr<QRhiTexture> texture_;
    std::unique_ptr<QRhiRenderBuffer> depth_;
    std::unique_ptr<QRhiTextureRenderTarget> target_;
    std::unique_ptr<QRhiRenderPassDescriptor> pass_;
    QQuickItem* view_ = nullptr;
    QTimer timer_;
    QElapsedTimer since_;
    QImage frame_;
    QString problem_;
    QPointer<QObject> profile_;
    audio::StreamMix sound_;
    PageSound listener_;
    bool focused_ = false;
    /// ``start`` failed; ``problem_`` says why.
    bool failed_ = false;
};

} // namespace

void initialize_web_engine()
{
    QtWebEngineQuick::initialize();
}

WebRendererFactory webengine_renderer_factory(const QString& storage_dir)
{
    // One profile for every page, made with the first: pages share sign-ins,
    // as tabs of one browser do.
    auto profile = std::make_shared<QPointer<QObject>>();
    return [storage_dir, profile]() -> std::unique_ptr<WebRenderer> {
        if (!storage_dir.isEmpty() && !*profile) {
            QQmlComponent component(&view_engine());
            component.setData(k_profile_qml, QUrl());
            QObject* made = component.createWithInitialProperties(
                {{QStringLiteral("persistentStoragePath"), storage_dir},
                 {QStringLiteral("cachePath"), storage_dir + QStringLiteral("/cache")}});
            if (made == nullptr) {
                qWarning("SeventhSlide: web page profile: %s",
                         qPrintable(component.errorString()));
            } else {
                made->setParent(QCoreApplication::instance());
                *profile = made;
            }
        }
        return std::make_unique<WebEngineRenderer>(profile->data());
    };
}

} // namespace seventhslide::app

#include "webengine_renderer.moc"
