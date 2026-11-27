#pragma once

// QAbstractListModel over one side's line map (or an image folder's
// pictures): the focused stack's Program, or its Preview.

#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>

#include <string>
#include <vector>

namespace seventhslide::app {

class LineMapModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Roles {
        LineIndexRole = Qt::UserRole + 1,
        /// Plain text of the line: tags removed, entities decoded.
        TextRole,
        LabelRole,
        IsCurrentRole,
        IsVisibleRole,
        IsNextRole,
        /// The line for a ``Text.StyledText`` item: HTML-escaped text with
        /// only its bold / italic / underline kept.
        RichTextRole,
        /// How many build steps the line's slide has: 1 for none.
        StepsRole,
        /// The step on the current line; -1 on any other.
        CurrentStepRole,
    };

    LineMapModel(session::Session& session, session::StackSide side, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void reload();
    /// Refresh current / visible / next flags without resetting the model.
    /// Only the rows those flags moved across are notified.
    void notify_rows_changed();

private:
    /// Rows the current / visible / next flags apply to.
    struct FlagSpan {
        int first = -1;
        int last = -1;

        void add(int row);
    };

    /// Row ``row``'s spans, without the ``ch:v `` prefix a Bible chunk keeps
    /// for layout (the label already shows it).
    [[nodiscard]] model::RichLine line_spans(std::size_t row) const;
    [[nodiscard]] bool line_is_visible(int line) const;
    [[nodiscard]] int next_change_line() const;
    [[nodiscard]] FlagSpan flagged_rows() const;

    /// The runtime listed: ``side_`` of the focused stack, or an empty one.
    [[nodiscard]] const session::StackRuntime& shown() const;

    session::Session& session_;
    session::StackSide side_;
    FlagSpan flagged_;
    /// The lines (or image files) the rows were last reset to.
    std::vector<std::string> shown_;
    /// ``next_change_line()`` as of the last reload / notify; ``IsNextRole`` is
    /// read for every row, and the player scan behind it is the same for all.
    int next_line_ = -1;
};

} // namespace seventhslide::app
