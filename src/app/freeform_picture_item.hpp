#pragma once

// QML: FreeformPictureItem - the stage's picture, painted as the output
// paints a picture object (render/picture_painter): its crop, fit, recolour
// and transparency, with its effects round it. The picture is the library's
// file, decoded through the session's image cache.

#include "session_bound_item.hpp"

#include <QtQml/qqmlregistration.h>
#include <QString>
#include <QVariantMap>

namespace seventhslide::app {

class FreeformPictureItem : public SessionBoundPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    /// The library image file (``filename`` of a picture object).
    Q_PROPERTY(QString filename READ filename WRITE setFilename NOTIFY pictureChanged)
    /// ``contain``, ``cover`` or ``fill``.
    Q_PROPERTY(QString fit READ fit WRITE setFit NOTIFY pictureChanged)
    /// canvas::Picture's stored form (crop, recolor, tint, transparency).
    Q_PROPERTY(QVariantMap picture READ picture WRITE setPicture NOTIFY pictureChanged)
    /// canvas::Effects' stored form; sizes in design px, scaled by ``scaleFactor``.
    Q_PROPERTY(QVariantMap effects READ effects WRITE setEffects NOTIFY pictureChanged)
    Q_PROPERTY(qreal scaleFactor READ scaleFactor WRITE setScaleFactor NOTIFY pictureChanged)
    /// Item px the box is inset inside this item: room for the effects.
    Q_PROPERTY(qreal padding READ padding WRITE setPadding NOTIFY pictureChanged)

public:
    explicit FreeformPictureItem(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

    [[nodiscard]] QString filename() const { return filename_; }
    void setFilename(const QString& filename);
    [[nodiscard]] QString fit() const { return fit_; }
    void setFit(const QString& fit);
    [[nodiscard]] QVariantMap picture() const { return picture_; }
    void setPicture(const QVariantMap& picture);
    [[nodiscard]] QVariantMap effects() const { return effects_; }
    void setEffects(const QVariantMap& effects);
    [[nodiscard]] qreal scaleFactor() const { return scale_factor_; }
    void setScaleFactor(qreal factor);
    [[nodiscard]] qreal padding() const { return padding_; }
    void setPadding(qreal padding);

signals:
    void pictureChanged();

protected:
    /// The session's image cache decodes the file; it is painted again when
    /// a library file arrives, as its picture may be it.
    void connect_session(SessionFacade& session) override;

private:
    template <typename T>
    void assign(T& slot, const T& value);

    QString filename_;
    QString fit_ = QStringLiteral("contain");
    QVariantMap picture_;
    QVariantMap effects_;
    qreal scale_factor_ = 1.0;
    qreal padding_ = 0.0;
};

}  // namespace seventhslide::app
