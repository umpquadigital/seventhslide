#pragma once

// QML: FreeformShapeItem - the editor canvas' shape renderer.
//
// Paints through the same core geometry (geom/shape_path) and the same Qt painter
// (render/shape_painter) as audience output and thumbnails, so what the operator
// drags is what the congregation sees. A text box's frame (its fill and line)
// is the same item with the rectangle kind.

#include "session_bound_item.hpp"

#include <QtQml/qqmlregistration.h>
#include <QColor>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace seventhslide::app {

class FreeformShapeItem : public SessionBoundPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY shapeChanged)
    Q_PROPERTY(QString fill READ fill WRITE setFill NOTIFY shapeChanged)
    /// The fill's kind by its stored name (``solid``, ``none``, ...): none
    /// paints no fill whatever the colour.
    Q_PROPERTY(QString fillKind READ fillKind WRITE setFillKind NOTIFY shapeChanged)
    /// A picture fill's file (``fill_picture_file``) and fit.
    Q_PROPERTY(QString fillPictureFile READ fillPictureFile WRITE setFillPictureFile NOTIFY shapeChanged)
    Q_PROPERTY(QString fillPictureFit READ fillPictureFit WRITE setFillPictureFit NOTIFY shapeChanged)
    Q_PROPERTY(QString stroke READ stroke WRITE setStroke NOTIFY shapeChanged)
    /// The line's dash by its stored name (``solid``, ``dash``, ``dot``,
    /// ``dash_dot``).
    Q_PROPERTY(QString dash READ dash WRITE setDash NOTIFY shapeChanged)
    /// The effects round the shape, as the selection map carries them
    /// (``effects``: canvas::Effects' stored form). Their sizes are design
    /// px, scaled with the border.
    Q_PROPERTY(QVariantMap effects READ effects WRITE setEffects NOTIFY shapeChanged)
    /// Border width in *design* px; multiplied by ``scaleFactor`` when painting.
    Q_PROPERTY(qreal strokeWidth READ strokeWidth WRITE setStrokeWidth NOTIFY shapeChanged)
    /// Stage zoom (canvas px -> item px). Only the border needs it; the outline
    /// itself is built from normalized adjustments and is scale-free.
    Q_PROPERTY(qreal scaleFactor READ scaleFactor WRITE setScaleFactor NOTIFY shapeChanged)
    Q_PROPERTY(qreal corner READ corner WRITE setCorner NOTIFY shapeChanged)
    Q_PROPERTY(int points READ points WRITE setPoints NOTIFY shapeChanged)
    Q_PROPERTY(qreal inner READ inner WRITE setInner NOTIFY shapeChanged)
    Q_PROPERTY(qreal head READ head WRITE setHead NOTIFY shapeChanged)
    Q_PROPERTY(qreal thickness READ thickness WRITE setThickness NOTIFY shapeChanged)
    /// A gradient fill: linear | radial, its angle, and its end colours.
    Q_PROPERTY(QString gradientKind READ gradientKind WRITE setGradientKind NOTIFY shapeChanged)
    Q_PROPERTY(qreal gradientAngle READ gradientAngle WRITE setGradientAngle NOTIFY shapeChanged)
    Q_PROPERTY(qreal gradientCenterX READ gradientCenterX WRITE setGradientCenterX NOTIFY shapeChanged)
    Q_PROPERTY(qreal gradientCenterY READ gradientCenterY WRITE setGradientCenterY NOTIFY shapeChanged)
    Q_PROPERTY(QString gradientFrom READ gradientFrom WRITE setGradientFrom NOTIFY shapeChanged)
    Q_PROPERTY(QString gradientTo READ gradientTo WRITE setGradientTo NOTIFY shapeChanged)
    /// A drawn shape's points, each [x, y, smooth] as shares of the box.
    Q_PROPERTY(QVariantList path READ path WRITE setPath NOTIFY shapeChanged)
    Q_PROPERTY(bool closed READ closed WRITE setClosed NOTIFY shapeChanged)
    /// An open outline's ends: none | arrow | dot.
    Q_PROPERTY(QString lineStart READ lineStart WRITE setLineStart NOTIFY shapeChanged)
    Q_PROPERTY(QString lineEnd READ lineEnd WRITE setLineEnd NOTIFY shapeChanged)
    /// Item-px inset of the shape box inside this item. QQuickPaintedItem clips to
    /// its own bounds, and a centred border spills half its width past the box -
    /// lay the item out with negative margins of ``padding`` to keep that visible.
    Q_PROPERTY(qreal padding READ padding WRITE setPadding NOTIFY shapeChanged)

public:
    explicit FreeformShapeItem(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

    [[nodiscard]] QString kind() const { return kind_; }
    void setKind(const QString& kind);
    [[nodiscard]] QString fillPictureFile() const { return fill_picture_file_; }
    void setFillPictureFile(const QString& file);
    [[nodiscard]] QString fillPictureFit() const { return fill_picture_fit_; }
    void setFillPictureFit(const QString& fit);
    [[nodiscard]] QString fill() const { return fill_; }
    void setFill(const QString& fill);
    [[nodiscard]] QString fillKind() const { return fill_kind_; }
    void setFillKind(const QString& kind);
    [[nodiscard]] QString stroke() const { return stroke_; }
    void setStroke(const QString& stroke);
    [[nodiscard]] QString dash() const { return dash_; }
    void setDash(const QString& dash);
    [[nodiscard]] QVariantMap effects() const { return effects_; }
    void setEffects(const QVariantMap& effects);
    [[nodiscard]] qreal strokeWidth() const { return stroke_width_; }
    void setStrokeWidth(qreal width);
    [[nodiscard]] qreal scaleFactor() const { return scale_factor_; }
    void setScaleFactor(qreal factor);
    [[nodiscard]] qreal corner() const { return corner_; }
    void setCorner(qreal value);
    [[nodiscard]] int points() const { return points_; }
    void setPoints(int value);
    [[nodiscard]] qreal inner() const { return inner_; }
    void setInner(qreal value);
    [[nodiscard]] qreal head() const { return head_; }
    void setHead(qreal value);
    [[nodiscard]] qreal thickness() const { return thickness_; }
    void setThickness(qreal value);
    [[nodiscard]] QString gradientKind() const { return gradient_kind_; }
    void setGradientKind(const QString& value);
    [[nodiscard]] qreal gradientAngle() const { return gradient_angle_; }
    void setGradientAngle(qreal value);
    [[nodiscard]] qreal gradientCenterX() const { return gradient_center_x_; }
    void setGradientCenterX(qreal value);
    [[nodiscard]] qreal gradientCenterY() const { return gradient_center_y_; }
    void setGradientCenterY(qreal value);
    [[nodiscard]] QString gradientFrom() const { return gradient_from_; }
    void setGradientFrom(const QString& value);
    [[nodiscard]] QString gradientTo() const { return gradient_to_; }
    void setGradientTo(const QString& value);
    [[nodiscard]] QVariantList path() const { return path_; }
    void setPath(const QVariantList& value);
    [[nodiscard]] bool closed() const { return closed_; }
    void setClosed(bool value);
    [[nodiscard]] QString lineStart() const { return line_start_; }
    void setLineStart(const QString& value);
    [[nodiscard]] QString lineEnd() const { return line_end_; }
    void setLineEnd(const QString& value);
    [[nodiscard]] qreal padding() const { return padding_; }
    void setPadding(qreal value);

signals:
    void shapeChanged();

protected:
    /// The session's image cache decodes a picture fill's file; it is painted
    /// again when a library file arrives, as its picture may be it.
    void connect_session(SessionFacade& session) override;

private:
    /// Assign + repaint only when the value actually moved.
    template <typename T>
    void assign(T& slot, const T& value);

    QString kind_ = QStringLiteral("rectangle");
    QString fill_picture_file_;
    QString fill_picture_fit_ = QStringLiteral("cover");
    QString fill_;
    QString fill_kind_ = QStringLiteral("solid");
    QString stroke_;
    QString dash_ = QStringLiteral("solid");
    QVariantMap effects_;
    qreal stroke_width_ = 2.0;
    qreal scale_factor_ = 1.0;
    qreal corner_ = 0.18;
    int points_ = 5;
    qreal inner_ = 0.38;
    qreal head_ = 0.35;
    qreal thickness_ = 0.45;
    QString gradient_kind_;
    qreal gradient_angle_ = 90;
    qreal gradient_center_x_ = 0.5;
    qreal gradient_center_y_ = 0.5;
    QString gradient_from_;
    QString gradient_to_;
    QVariantList path_;
    bool closed_ = false;
    QString line_start_;
    QString line_end_;
    qreal padding_ = 0.0;
};

} // namespace seventhslide::app
