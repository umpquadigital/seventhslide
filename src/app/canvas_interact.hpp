#pragma once

// QML facade over canvas::interact. One instance per stage; QML is chrome.

#include <seventhslide/geom/canvas.hpp>

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace seventhslide::app {

class CanvasInteract : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int canvasWidth READ canvasWidth WRITE setCanvasWidth NOTIFY canvasChanged)
    Q_PROPERTY(int canvasHeight READ canvasHeight WRITE setCanvasHeight NOTIFY canvasChanged)
    Q_PROPERTY(qreal snapThreshold READ snapThreshold WRITE setSnapThreshold NOTIFY canvasChanged)
    Q_PROPERTY(qreal handleHit READ handleHit WRITE setHandleHit NOTIFY canvasChanged)
    /// Back-to-front ``{id,x,y,w,h,rotation}``.
    Q_PROPERTY(QVariantList objects READ objects WRITE setObjects NOTIFY objectsChanged)
public:
    explicit CanvasInteract(QObject* parent = nullptr);

    [[nodiscard]] int canvasWidth() const noexcept { return canvas_w_; }
    void setCanvasWidth(int w);
    [[nodiscard]] int canvasHeight() const noexcept { return canvas_h_; }
    void setCanvasHeight(int h);
    [[nodiscard]] qreal snapThreshold() const noexcept { return snap_threshold_; }
    void setSnapThreshold(qreal v);
    [[nodiscard]] qreal handleHit() const noexcept { return handle_hit_; }
    void setHandleHit(qreal v);
    [[nodiscard]] QVariantList objects() const { return objects_; }
    void setObjects(const QVariantList& objects);

    Q_INVOKABLE [[nodiscard]] QString hitHandle(qreal stageX, qreal stageY, const QVariantMap& rect,
                                                qreal scale) const;
    Q_INVOKABLE [[nodiscard]] QString hitObject(qreal canvasX, qreal canvasY) const;
    Q_INVOKABLE [[nodiscard]] bool pointInRect(qreal canvasX, qreal canvasY,
                                               const QVariantMap& rect) const;
    Q_INVOKABLE [[nodiscard]] QStringList hitMarquee(qreal x, qreal y, qreal w, qreal h) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap unionBounds(const QVariantList& rects) const;
    Q_INVOKABLE [[nodiscard]] QVariantList geometryFromDragMany(const QString& mode,
                                                                const QVariantList& start, qreal dx,
                                                                qreal dy) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap snapGeometryMany(const QVariantList& rects,
                                                           const QString& mode,
                                                           const QStringList& excludeIds,
                                                           bool enabled) const;
    Q_INVOKABLE [[nodiscard]] int cursorFor(const QString& handle) const;

signals:
    void canvasChanged();
    void objectsChanged();

private:
    int canvas_w_ = geom::k_reference_canvas.width;
    int canvas_h_ = geom::k_reference_canvas.height;
    qreal snap_threshold_ = 10;
    qreal handle_hit_ = 10;
    QVariantList objects_;
};

} // namespace seventhslide::app
