#pragma once

// QAbstractListModel projections of a Freeform editor draft.

#include <seventhslide/freeform/compile.hpp>
#include <seventhslide/geom/canvas.hpp>

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <string>
#include <vector>

namespace seventhslide::freeform {
struct Document;
class Editor;
} // namespace seventhslide::freeform

namespace seventhslide::app {

class FreeformSlideListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Roles {
        IndexRole = Qt::UserRole + 1,
        SlideIdRole,
        LabelRole,
        PreviewRole,
        SelectedRole,
        KindRole,
        CueIndexRole,
        CueTextRole,
        CueSelectedRole,
        CueIndicatorRole,
        /// The cue's chords over its row's text: ``[{at, label}]``, ``at`` a
        /// QML string index into the text without its tags, ``label`` the
        /// chord with sharps and flats. Empty when it has none.
        CueChordsRole,
        PreviewKeyRole,
        ThumbUrlRole,
    };

    explicit FreeformSlideListModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    /// Rebuild every row from ``editor``. True when a slide's preview key
    /// changed (a thumbnail now shows something else).
    bool reload(const freeform::Editor& editor, int canvas_w = geom::k_reference_canvas.width, int canvas_h = geom::k_reference_canvas.height,
                int theme_rev = 0);
    /// The slides (``first``..``last``) and cue picked moved, and nothing
    /// else: the rows' selected flags follow.
    void set_current(int first, int last, int cue_index);
    /// Recompute one slide's preview key and thumb URL, after an edit that
    /// changed nothing but how that slide looks (moving objects on it, say).
    /// Every other row, and the row order, is left as it is. True when the
    /// key changed.
    bool refresh_slide(const freeform::Editor& editor, int slide_index, int canvas_w,
                       int canvas_h, int theme_rev);
    Q_INVOKABLE [[nodiscard]] QVariantMap rowAt(int row) const;

private:
    struct Row {
        int index = 0;
        QString slide_id;
        QString label;
        QString preview;
        bool selected = false;
        QString kind = QStringLiteral("slide");
        int cue_index = -1;
        QString cue_id;
        QString cue_text;
        QVariantList cue_chords;
        QString cue_indicator;
        bool cue_selected = false;
        QString preview_key;
        QString thumb_url;

        [[nodiscard]] bool operator==(const Row&) const noexcept = default;
    };
    [[nodiscard]] static QString row_key(const Row& row);
    /// The preview keys of the slide rows, in order.
    [[nodiscard]] static std::vector<QString> slide_preview_keys(const std::vector<Row>& rows);
    void apply_rows(std::vector<Row> next);
    std::vector<Row> rows_;
    freeform::SlidePreviewKeys preview_keys_;
};

class FreeformFieldListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Roles { NameRole = Qt::UserRole + 1, ValueRole, UnusedRole, TokenRole, HideRole };
    explicit FreeformFieldListModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    void reload(const freeform::Editor& editor);

private:
    struct Row {
        QString name;
        QString value;
        bool unused = false;
        bool hide = false;
    };
    std::vector<Row> rows_;
};

/// The objects on the stage, back to front, one row each (``obj``: the
/// object's values, with the ``id`` the rows are kept by). An edit changes
/// only the rows it touched, so the stage keeps the items of every other
/// object; a row that comes, goes or moves is inserted, removed or moved.
class CanvasObjectModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Roles { ObjectRole = Qt::UserRole + 1 };
    explicit CanvasObjectModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    /// The rows become ``objects`` by the fewest changes.
    void apply(const QVariantList& objects);

private:
    QVariantList rows_;
};

} // namespace seventhslide::app
