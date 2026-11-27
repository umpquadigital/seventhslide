#include "freeform_editor_models.hpp"

#include "text_offsets.hpp"

#include <seventhslide/freeform/compile.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/editor.hpp>
#include <seventhslide/layout/chords.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/parse/lyrics_blocks.hpp>

#include <QSet>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace seventhslide::app {
namespace {

QString first_preview_line(const freeform::Slide& slide)
{
    for (const auto& obj : slide.objects) {
        if (obj.type != freeform::ObjectType::Text || obj.text.empty()) {
            continue;
        }
        const auto nl = obj.text.find('\n');
        const auto line = nl == std::string::npos ? obj.text : obj.text.substr(0, nl);
        return QString::fromStdString(model::strip_markup_tags(line));
    }
    return {};
}

QString make_thumb_url(const QString& output_id, const QString& slide_id,
                       const QString& preview_key, int canvas_w, int canvas_h, int theme_rev)
{
    if (slide_id.isEmpty() || preview_key.isEmpty()) {
        return {};
    }
    const QString name = QString::fromUtf8(QUrl::toPercentEncoding(output_id));
    const QString id = QString::fromUtf8(QUrl::toPercentEncoding(slide_id));
    return QStringLiteral("image://freeformpreview/%1?id=%2&v=%3&t=%4&cw=%5&ch=%6")
        .arg(name, id, preview_key)
        .arg(theme_rev)
        .arg(canvas_w)
        .arg(canvas_h);
}

} // namespace

std::vector<QString> FreeformSlideListModel::slide_preview_keys(const std::vector<Row>& rows)
{
    std::vector<QString> keys;
    keys.reserve(rows.size());
    for (const auto& row : rows) {
        if (row.kind == QStringLiteral("slide")) {
            keys.push_back(row.preview_key);
        }
    }
    return keys;
}

QString FreeformSlideListModel::row_key(const Row& row)
{
    if (row.kind == QStringLiteral("cue")) {
        if (!row.cue_id.isEmpty()) {
            return QStringLiteral("c:") + row.cue_id;
        }
        return QStringLiteral("c#") + QString::number(row.cue_index);
    }
    if (!row.slide_id.isEmpty()) {
        return QStringLiteral("s:") + row.slide_id;
    }
    return QStringLiteral("s#") + QString::number(row.index);
}

FreeformSlideListModel::FreeformSlideListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int FreeformSlideListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(rows_.size());
}

QVariant FreeformSlideListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(rows_.size())) {
        return {};
    }
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case IndexRole:
        return row.index;
    case SlideIdRole:
        return row.slide_id;
    case LabelRole:
        return row.label;
    case PreviewRole:
        return row.preview;
    case SelectedRole:
        return row.selected;
    case KindRole:
        return row.kind;
    case CueIndexRole:
        return row.cue_index;
    case CueTextRole:
        return row.cue_text;
    case CueSelectedRole:
        return row.cue_selected;
    case CueIndicatorRole:
        return row.cue_indicator;
    case CueChordsRole:
        return row.cue_chords;
    case PreviewKeyRole:
        return row.preview_key;
    case ThumbUrlRole:
        return row.thumb_url;
    default:
        return {};
    }
}

QHash<int, QByteArray> FreeformSlideListModel::roleNames() const
{
    return {
        {IndexRole, "slideIndex"},
        {SlideIdRole, "slideId"},
        {LabelRole, "label"},
        {PreviewRole, "preview"},
        {SelectedRole, "selected"},
        {KindRole, "kind"},
        {CueIndexRole, "cueIndex"},
        {CueTextRole, "cueText"},
        {CueSelectedRole, "cueSelected"},
        {CueIndicatorRole, "cueIndicator"},
        {CueChordsRole, "cueChords"},
        {PreviewKeyRole, "previewKey"},
        {ThumbUrlRole, "thumbUrl"},
    };
}

bool FreeformSlideListModel::reload(const freeform::Editor& editor, int canvas_w, int canvas_h,
                                    int theme_rev)
{
    std::vector<Row> next;
    const auto* deck = editor.current_deck();
    if (deck) {
        next.reserve(deck->slides.size() + editor.document().cues.size());
        const auto list = freeform::deck_list_rows(editor.document(), *deck);
        const auto preview_keys = preview_keys_.keys(editor.document(), *deck);
        const QString output = QString::fromStdString(editor.output_id());
        const int cw = std::max(1, canvas_w);
        const int ch = std::max(1, canvas_h);
        for (const auto& item : list) {
            Row row;
            row.index = item.slide_index;
            if (item.slide_index >= 0 && item.slide_index < deck->slide_count()) {
                const auto& slide = deck->slides[static_cast<std::size_t>(item.slide_index)];
                row.slide_id = QString::fromStdString(slide.id);
                row.preview = first_preview_line(slide);
                row.preview_key = QString::fromStdString(
                    preview_keys[static_cast<std::size_t>(item.slide_index)]);
            }
            row.label = QString::number(item.slide_index + 1);
            row.selected = item.slide_index >= editor.slide_range_first()
                        && item.slide_index <= editor.slide_range_last();
            if (item.kind == freeform::ListRowKind::Cue) {
                row.kind = QStringLiteral("cue");
                row.cue_index = item.cue_index;
                if (item.cue_index >= 0
                    && item.cue_index < static_cast<int>(editor.document().cues.size())) {
                    const auto& cue = editor.document().cues[static_cast<std::size_t>(item.cue_index)];
                    row.cue_id = QString::fromStdString(cue.id);
                    // The row reads the cue's words; its chords are drawn over them.
                    const auto taken = layout::take_chord_marks(cue.text);
                    row.cue_text = QString::fromStdString(taken.markup);
                    const QByteArray plain = QByteArray::fromStdString(model::plain_text_lines(
                        model::parse_markup_lines(taken.markup)));
                    for (const auto& mark : taken.marks) {
                        row.cue_chords.push_back(QVariantMap{
                            {QStringLiteral("at"), utf16_offset(plain, mark.at)},
                            {QStringLiteral("label"), QString::fromStdString(
                                                          layout::format_chord_accidentals(mark.chord))},
                        });
                    }
                    row.cue_indicator = QString::fromStdString(
                        parse::verse_indicator_text(cue.verse_type, cue.verse_number));
                }
                row.cue_selected = item.cue_index == editor.cue_index();
            } else {
                row.kind = QStringLiteral("slide");
                row.thumb_url = make_thumb_url(output, row.slide_id, row.preview_key, cw, ch,
                                               theme_rev);
            }
            next.push_back(std::move(row));
        }
    }
    const bool keys_changed = slide_preview_keys(rows_) != slide_preview_keys(next);
    apply_rows(std::move(next));
    return keys_changed;
}

bool FreeformSlideListModel::refresh_slide(const freeform::Editor& editor, int slide_index,
                                           int canvas_w, int canvas_h, int theme_rev)
{
    const auto* deck = editor.current_deck();
    if (deck == nullptr || slide_index < 0 || slide_index >= deck->slide_count()) {
        return false;
    }
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto& row = rows_[i];
        if (row.kind != QStringLiteral("slide") || row.index != slide_index) {
            continue;
        }
        const auto key = QString::fromStdString(
            preview_keys_.key(editor.document(), *deck, slide_index));
        if (key == row.preview_key) {
            return false;
        }
        row.preview_key = key;
        row.thumb_url = make_thumb_url(QString::fromStdString(editor.output_id()), row.slide_id,
                                       key, std::max(1, canvas_w), std::max(1, canvas_h),
                                       theme_rev);
        const auto idx = index(static_cast<int>(i));
        emit dataChanged(idx, idx, {PreviewKeyRole, ThumbUrlRole});
        return true;
    }
    return false;
}

void FreeformSlideListModel::set_current(int first, int last, int cue_index)
{
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto& row = rows_[i];
        const bool selected = row.index >= first && row.index <= last;
        const bool cue_selected = row.kind == QStringLiteral("cue") && row.cue_index == cue_index;
        if (row.selected == selected && row.cue_selected == cue_selected) {
            continue;
        }
        row.selected = selected;
        row.cue_selected = cue_selected;
        const auto idx = index(static_cast<int>(i));
        emit dataChanged(idx, idx, {SelectedRole, CueSelectedRole});
    }
}

void FreeformSlideListModel::apply_rows(std::vector<Row> next)
{
    // QML ListView zeros contentY on modelReset. Identify rows by stable
    // slide/cue ids so insert/delete/reorder emit granular signals instead.
    const auto keys_unique = [](const std::vector<Row>& rows) {
        QSet<QString> seen;
        seen.reserve(static_cast<int>(rows.size()));
        for (const auto& row : rows) {
            const auto key = row_key(row);
            if (seen.contains(key)) {
                return false;
            }
            seen.insert(key);
        }
        return true;
    };
    const auto reset = [&] {
        beginResetModel();
        rows_ = std::move(next);
        endResetModel();
    };
    if (!keys_unique(rows_) || !keys_unique(next)) {
        reset();
        return;
    }

    QSet<QString> next_keys;
    next_keys.reserve(static_cast<int>(next.size()));
    for (const auto& row : next) {
        next_keys.insert(row_key(row));
    }
    for (int i = static_cast<int>(rows_.size()) - 1; i >= 0; --i) {
        if (next_keys.contains(row_key(rows_[static_cast<std::size_t>(i)]))) {
            continue;
        }
        beginRemoveRows({}, i, i);
        rows_.erase(rows_.begin() + i);
        endRemoveRows();
    }

    for (int i = 0; i < static_cast<int>(next.size()); ++i) {
        const auto key = row_key(next[static_cast<std::size_t>(i)]);
        if (i < static_cast<int>(rows_.size())
            && row_key(rows_[static_cast<std::size_t>(i)]) == key) {
            continue;
        }
        int from = -1;
        for (int j = i + 1; j < static_cast<int>(rows_.size()); ++j) {
            if (row_key(rows_[static_cast<std::size_t>(j)]) == key) {
                from = j;
                break;
            }
        }
        if (from < 0) {
            beginInsertRows({}, i, i);
            rows_.insert(rows_.begin() + i, next[static_cast<std::size_t>(i)]);
            endInsertRows();
            continue;
        }
        const int dest_child = from < i ? i + 1 : i;
        if (!beginMoveRows({}, from, from, {}, dest_child)) {
            reset();
            return;
        }
        Row moved = std::move(rows_[static_cast<std::size_t>(from)]);
        rows_.erase(rows_.begin() + from);
        rows_.insert(rows_.begin() + i, std::move(moved));
        endMoveRows();
    }

    if (rows_.size() != next.size()) {
        reset();
        return;
    }
    for (std::size_t i = 0; i < next.size(); ++i) {
        if (rows_[i] == next[i]) {
            continue;
        }
        rows_[i] = std::move(next[i]);
        const auto idx = index(static_cast<int>(i));
        emit dataChanged(idx, idx);
    }
}

QVariantMap FreeformSlideListModel::rowAt(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= static_cast<int>(rows_.size())) {
        return m;
    }
    const auto& r = rows_[static_cast<std::size_t>(row)];
    m.insert(QStringLiteral("kind"), r.kind);
    m.insert(QStringLiteral("slideIndex"), r.index);
    m.insert(QStringLiteral("cueIndex"), r.cue_index);
    m.insert(QStringLiteral("cueText"), r.cue_text);
    m.insert(QStringLiteral("cueIndicator"), r.cue_indicator);
    m.insert(QStringLiteral("hasChords"), !r.cue_chords.isEmpty());
    m.insert(QStringLiteral("selected"), r.selected);
    return m;
}

FreeformFieldListModel::FreeformFieldListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int FreeformFieldListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(rows_.size());
}

QVariant FreeformFieldListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(rows_.size())) {
        return {};
    }
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case NameRole:
        return row.name;
    case ValueRole:
        return row.value;
    case UnusedRole:
        return row.unused;
    case HideRole:
        return row.hide;
    case TokenRole:
        return row.hide ? QStringLiteral("{-%1}").arg(row.name)
                        : QStringLiteral("{%1}").arg(row.name);
    default:
        return {};
    }
}

QHash<int, QByteArray> FreeformFieldListModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {ValueRole, "value"},
        {UnusedRole, "unused"},
        {TokenRole, "token"},
        {HideRole, "hide"},
    };
}

void FreeformFieldListModel::reload(const freeform::Editor& editor)
{
    std::vector<Row> next;
    for (const auto& field : editor.field_rows()) {
        Row row;
        row.name = QString::fromStdString(field.name);
        row.value = QString::fromStdString(field.value);
        row.unused = field.unused;
        row.hide = field.hide;
        next.push_back(std::move(row));
    }

    // Typing a value must not tear down the delegate holding the caret, so a
    // value-only change is patched in place; only a change of shape resets.
    bool same_shape = next.size() == rows_.size();
    for (std::size_t i = 0; same_shape && i < next.size(); ++i) {
        same_shape = next[i].name == rows_[i].name && next[i].unused == rows_[i].unused
            && next[i].hide == rows_[i].hide;
    }
    if (same_shape) {
        for (std::size_t i = 0; i < next.size(); ++i) {
            if (next[i].value == rows_[i].value) {
                continue;
            }
            rows_[i].value = next[i].value;
            const auto idx = index(static_cast<int>(i));
            emit dataChanged(idx, idx, {ValueRole});
        }
        return;
    }

    beginResetModel();
    rows_ = std::move(next);
    endResetModel();
}

CanvasObjectModel::CanvasObjectModel(QObject* parent) : QAbstractListModel(parent) {}

int CanvasObjectModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant CanvasObjectModel::data(const QModelIndex& index, int role) const
{
    if (role != ObjectRole || !index.isValid() || index.row() >= rows_.size()) {
        return {};
    }
    return rows_.at(index.row());
}

QHash<int, QByteArray> CanvasObjectModel::roleNames() const
{
    return {{ObjectRole, QByteArrayLiteral("obj")}};
}

void CanvasObjectModel::apply(const QVariantList& objects)
{
    const auto id_of = [](const QVariant& v) {
        return v.toMap().value(QStringLiteral("id")).toString();
    };
    QStringList next_ids;
    next_ids.reserve(objects.size());
    for (const auto& v : objects) {
        next_ids.push_back(id_of(v));
    }
    QSet<QString> next_set(next_ids.begin(), next_ids.end());
    if (next_set.size() != next_ids.size()) {
        // Ids that repeat cannot be followed.
        beginResetModel();
        rows_ = objects;
        endResetModel();
        return;
    }
    for (qsizetype i = rows_.size() - 1; i >= 0; --i) {
        if (!next_set.contains(id_of(rows_.at(i)))) {
            beginRemoveRows({}, static_cast<int>(i), static_cast<int>(i));
            rows_.removeAt(i);
            endRemoveRows();
        }
    }
    for (qsizetype i = 0; i < objects.size(); ++i) {
        const auto& id = next_ids.at(i);
        if (i < rows_.size() && id_of(rows_.at(i)) == id) {
            if (rows_.at(i) != objects.at(i)) {
                rows_[i] = objects.at(i);
                const auto idx = index(static_cast<int>(i));
                emit dataChanged(idx, idx, {ObjectRole});
            }
            continue;
        }
        qsizetype from = -1;
        for (qsizetype j = i + 1; j < rows_.size(); ++j) {
            if (id_of(rows_.at(j)) == id) {
                from = j;
                break;
            }
        }
        if (from < 0) {
            beginInsertRows({}, static_cast<int>(i), static_cast<int>(i));
            rows_.insert(i, objects.at(i));
            endInsertRows();
            continue;
        }
        // Later in the rows: up to here, then as it is now.
        beginMoveRows({}, static_cast<int>(from), static_cast<int>(from), {}, static_cast<int>(i));
        rows_.move(from, i);
        endMoveRows();
        if (rows_.at(i) != objects.at(i)) {
            rows_[i] = objects.at(i);
            const auto idx = index(static_cast<int>(i));
            emit dataChanged(idx, idx, {ObjectRole});
        }
    }
}

} // namespace seventhslide::app
