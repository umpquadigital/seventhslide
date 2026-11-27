#include "line_map_model.hpp"

#include <algorithm>

#include <QRegularExpression>

namespace seventhslide::app {
namespace {

/// ``spans`` as StyledText: text HTML-escaped, bold / italic / underline kept.
[[nodiscard]] QString styled_text(const model::RichLine& spans)
{
    QString out;
    for (const auto& span : spans) {
        QString piece = QString::fromStdString(span.text).toHtmlEscaped();
        if (span.style.underline) {
            piece = QStringLiteral("<u>") + piece + QStringLiteral("</u>");
        }
        if (span.style.italic) {
            piece = QStringLiteral("<i>") + piece + QStringLiteral("</i>");
        }
        if (span.style.bold) {
            piece = QStringLiteral("<b>") + piece + QStringLiteral("</b>");
        }
        out += piece;
    }
    return out;
}

/// Drop the first ``bytes`` bytes of text from ``spans``.
void drop_leading_bytes(model::RichLine& spans, std::size_t bytes)
{
    while (bytes > 0 && !spans.empty()) {
        auto& front = spans.front().text;
        if (front.size() > bytes) {
            front.erase(0, bytes);
            return;
        }
        bytes -= front.size();
        spans.erase(spans.begin());
    }
}

}  // namespace

LineMapModel::LineMapModel(session::Session& session, session::StackSide side, QObject* parent)
    : QAbstractListModel(parent)
    , session_(session)
    , side_(side)
{
}

const session::StackRuntime& LineMapModel::shown() const
{
    // A side the stack does not have lists nothing, as an empty one does.
    static const session::StackRuntime k_nothing;
    const auto* st = session_.runtime({session_.focused_stack_id(), side_});
    return st != nullptr ? *st : k_nothing;
}

int LineMapModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    if (shown().image_mode_active()) {
        return shown().image_playlist().count();
    }
    return shown().player().total_lines();
}

bool LineMapModel::line_is_visible(int line) const
{
    const auto& player = shown().player();
    for (const auto& oc : player.outputs()) {
        if (oc.line_to_slide.empty()) {
            continue;
        }
        if (line < 0 || static_cast<std::size_t>(line) >= oc.line_to_slide.size()) {
            continue;
        }
        if (oc.line_to_slide[static_cast<std::size_t>(line)] == oc.index) {
            return true;
        }
    }
    return false;
}

int LineMapModel::next_change_line() const
{
    const auto next = shown().player().find_next_change_line(
        shown().player().line_cursor(), +1);
    return next.value_or(-1);
}

QVariant LineMapModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const int row = index.row();

    if (shown().image_mode_active()) {
        const auto& pl = shown().image_playlist();
        if (row < 0 || row >= pl.count()) {
            return {};
        }
        switch (role) {
        case LineIndexRole:
            return row;
        case TextRole:
            return QString::fromStdString(pl.images[static_cast<std::size_t>(row)]);
        case RichTextRole:
            return QString::fromStdString(pl.images[static_cast<std::size_t>(row)])
                .toHtmlEscaped();
        case LabelRole:
            return QString::number(row + 1);
        case IsCurrentRole:
            return row == pl.index;
        case IsVisibleRole:
            return false;
        case IsNextRole:
            return row == pl.index + 1;
        default:
            return {};
        }
    }

    const auto& player = shown().player();
    if (row < 0 || row >= player.total_lines()) {
        return {};
    }
    const auto urow = static_cast<std::size_t>(row);

    switch (role) {
    case LineIndexRole:
        return row;
    case TextRole:
        return QString::fromStdString(model::plain_text(line_spans(urow)));
    case RichTextRole:
        return styled_text(line_spans(urow));
    case LabelRole: {
        if (urow >= player.all_line_labels().size()) {
            return QString{};
        }
        QString label = QString::fromStdString(player.all_line_labels()[urow]);
        if (label.size() > 5) {
            label = label.left(3) + QStringLiteral("..");
        }
        return label;
    }
    case IsCurrentRole:
        return row == player.line_cursor();
    case IsVisibleRole:
        return line_is_visible(row) && row != player.line_cursor();
    case IsNextRole:
        return next_line_ >= 0 && row == next_line_ && row != player.line_cursor();
    case StepsRole:
        return player.steps_at(row);
    case CurrentStepRole:
        return row == player.line_cursor() ? player.step() : -1;
    default:
        return {};
    }
}

QHash<int, QByteArray> LineMapModel::roleNames() const
{
    return {
        {LineIndexRole, "lineIndex"},
        {TextRole, "text"},
        {LabelRole, "label"},
        {IsCurrentRole, "isCurrent"},
        {IsVisibleRole, "isVisible"},
        {IsNextRole, "isNext"},
        {RichTextRole, "richText"},
        {StepsRole, "steps"},
        {CurrentStepRole, "currentStep"},
    };
}

model::RichLine LineMapModel::line_spans(std::size_t row) const
{
    const auto& player = shown().player();
    if (row >= player.all_lines().size()) {
        return {};
    }
    auto spans = model::parse_inline_markup(player.all_lines()[row]);
    // A Bible chunk keeps its leading "ch:v " for layout measuring, but the
    // label already shows that reference.
    if (row < player.all_line_labels().size()) {
        static const QRegularExpression k_bible_ref_prefix(QStringLiteral(R"(^(\d+:\d+)\s+)"));
        const QString plain = QString::fromStdString(model::plain_text(spans));
        const auto m = k_bible_ref_prefix.match(plain);
        const QString label = QString::fromStdString(player.all_line_labels()[row]);
        if (m.hasMatch() && label.startsWith(m.captured(1))) {
            drop_leading_bytes(spans, static_cast<std::size_t>(m.captured(0).toUtf8().size()));
        }
    }
    return spans;
}

void LineMapModel::FlagSpan::add(int row)
{
    if (row < 0) {
        return;
    }
    first = first < 0 ? row : std::min(first, row);
    last = last < 0 ? row : std::max(last, row);
}

LineMapModel::FlagSpan LineMapModel::flagged_rows() const
{
    FlagSpan span;
    const int rows = rowCount();
    if (rows <= 0) {
        return span;
    }
    if (shown().image_mode_active()) {
        const auto& pl = shown().image_playlist();
        span.add(pl.index);
        span.add(pl.index + 1 < rows ? pl.index + 1 : -1);
        return span;
    }
    span.add(shown().player().line_cursor());
    span.add(next_line_);
    for (int row = 0; row < rows; ++row) {
        if (line_is_visible(row)) {
            span.add(row);
        }
    }
    return span;
}

void LineMapModel::reload()
{
    next_line_ = shown().image_mode_active() ? -1 : next_change_line();
    flagged_ = flagged_rows();
    // The same lines (an edit elsewhere in the library, another computer's
    // sync): read again where they are, and the list stays where it is.
    const auto& stack = shown();
    auto rows = stack.image_mode_active() ? stack.image_playlist().images : stack.player().all_lines();
    if (!rows.empty() && rows == shown_) {
        emit dataChanged(index(0), index(rowCount() - 1));
        return;
    }
    beginResetModel();
    shown_ = std::move(rows);
    endResetModel();
}

void LineMapModel::notify_rows_changed()
{
    const int rows = rowCount();
    if (rows <= 0) {
        flagged_ = {};
        return;
    }
    // The flags moved from the rows they were on to the rows they are on now;
    // everything outside that span still reads the same.
    const auto previous = flagged_;
    next_line_ = shown().image_mode_active() ? -1 : next_change_line();
    flagged_ = flagged_rows();
    FlagSpan touched = previous;
    touched.add(flagged_.first);
    touched.add(flagged_.last);
    if (touched.first < 0) {
        return;
    }
    const int first = std::clamp(touched.first, 0, rows - 1);
    const int last = std::clamp(touched.last, first, rows - 1);
    emit dataChanged(index(first), index(last),
                     {IsCurrentRole, IsVisibleRole, IsNextRole, CurrentStepRole});
}

} // namespace seventhslide::app
