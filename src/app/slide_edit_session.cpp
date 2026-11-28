// SlideEditSession: the slide editor's one session, split into what the
// window reads. The commands are the two core editors'; the values go out by
// the property tables' names, as they paint.

#include "slide_edit_session.hpp"

#include "background_theme_copier.hpp"
#include "canvas_rich_edit.hpp"
#include "entry_error.hpp"
#include "refusals.hpp"
#include "freeform_slide_render.hpp"
#include "qvariant_json.hpp"
#include "requested_size.hpp"
#include "rich_line_variant.hpp"
#include "text_offsets.hpp"
#include "theme_controller.hpp"

#include <seventhslide/canvas/layout.hpp>
#include <seventhslide/freeform/compile.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/object_properties.hpp>
#include <seventhslide/freeform/paint.hpp>
#include <seventhslide/freeform/tokens.hpp>
#include <seventhslide/geom/shape_path.hpp>
#include <seventhslide/layout/background_fill.hpp>
#include <seventhslide/layout/chords.hpp>
#include <seventhslide/layout/overlays.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/model/transition.hpp>
#include <seventhslide/parse/lyrics_blocks.hpp>
#include <seventhslide/scene/chrome_text.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/theme/element_properties.hpp>
#include <seventhslide/theme/resolved_theme.hpp>
#include <seventhslide/theme/theme_elements.hpp>

#include <QTime>
#include <QTimer>

#include <tuple>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace seventhslide::app {
namespace {

constexpr std::string_view k_elements = theme::k_elements_key;

/// The type's name; a transition of no length is a cut.
[[nodiscard]] QString transition_type_label(const model::Transition& spec)
{
    return spec.plays() ? QString::fromUtf8(model::to_string(spec.type)) : QStringLiteral("cut");
}

/// ``type`` and ``duration_ms`` from QML as a transition; nullopt for
/// ``inherit`` (a slide background following the theme).
[[nodiscard]] std::optional<model::Transition> spec_from_qml(const QString& type,
                                                                  int duration_ms,
                                                                  const QString& direction = {})
{
    if (type == QLatin1String("inherit")) {
        return std::nullopt;
    }
    model::Transition spec;
    spec.type = model::transition_type_from_string(type.toStdString());
    spec.duration_ms = std::max(0, duration_ms);
    spec.direction = model::transition_direction_from_string(direction.toStdString());
    return spec;
}

[[nodiscard]] QString plain_of(const canvas::Object& o)
{
    const auto lines = model::parse_editor_lines(o.text, o.bold, o.italic, o.underline);
    return QString::fromStdString(model::plain_text_lines(lines));
}

/// What every object tells the window beside its properties.
void add_identity(QVariantMap& m, const canvas::Object& o)
{
    m.insert(QStringLiteral("id"), QString::fromStdString(o.id));
    m.insert(QStringLiteral("type"), QString::fromUtf8(canvas::to_string(o.type)));
    m.insert(QStringLiteral("text"), QString::fromStdString(o.text));
    m.insert(QStringLiteral("plain"), plain_of(o));
    m.insert(QStringLiteral("z"), o.z);
    m.insert(QStringLiteral("locked"), o.locked);
    m.insert(QStringLiteral("flip_h"), o.flip_h);
    m.insert(QStringLiteral("flip_v"), o.flip_v);
    m.insert(QStringLiteral("group_id"), QString::fromStdString(o.group_id));
    m.insert(QStringLiteral("transition_type"), transition_type_label(o.transition));
    m.insert(QStringLiteral("transition_ms"), o.transition.duration_ms);
    // The effects whole, for the stage's items, which paint them as the
    // output does; and how far they reach past the box, for their room.
    m.insert(QStringLiteral("effects"), to_qvariant(nlohmann::json(o.effects)));
    m.insert(QStringLiteral("effects_reach"), canvas::effects_reach(o.effects));
    // The paragraphs the same way, and whether they are anything but the
    // default (the stage then draws the words as the output does).
    m.insert(QStringLiteral("paragraph"), to_qvariant(nlohmann::json(o.paragraph)));
    m.insert(QStringLiteral("paragraph_set"),
             o.paragraph != canvas::Paragraph{} || o.text.find("<p=") != std::string::npos);
    // A picture's adjustments whole, for the stage's picture item.
    m.insert(QStringLiteral("picture"), to_qvariant(nlohmann::json(o.picture)));
}

/// A Freeform object by the names the property table takes, as it paints.
[[nodiscard]] QVariantMap object_map(const freeform::Object& obj)
{
    QVariantMap m = to_qvariant(freeform::object_property_values(obj)).toMap();
    add_identity(m, obj);
    m.insert(QStringLiteral("binding"), QStringLiteral("static"));
    m.insert(QStringLiteral("cue_linked"), obj.cue_linked);
    m.insert(QStringLiteral("show_upcoming"), obj.show_upcoming);
    return m;
}

/// A theme element the same way, with the element's own values beside.
[[nodiscard]] QVariantMap element_map(const theme::Element& el)
{
    QVariantMap m = to_qvariant(theme::element_property_values(el)).toMap();
    add_identity(m, el.object);
    m.insert(QStringLiteral("binding"), QString::fromUtf8(theme::to_string(el.binding)));
    m.insert(QStringLiteral("cue_linked"), false);
    m.insert(QStringLiteral("show_upcoming"), false);
    return m;
}

[[nodiscard]] const freeform::SlideBackground* current_slide_background(
    const freeform::Editor& editor)
{
    const auto* slide = editor.current_slide();
    return slide ? &slide->background : nullptr;
}

[[nodiscard]] const freeform::Cue* current_cue(const freeform::Editor& editor)
{
    const auto& doc = editor.document();
    const int i = editor.cue_index();
    if (!doc.line_control || i < 0 || i >= static_cast<int>(doc.cues.size())) {
        return nullptr;
    }
    return &doc.cues[static_cast<std::size_t>(i)];
}

/// ``cue``'s text as a linked box shows it, plain, line breaks left out: what
/// chord offsets count in.
[[nodiscard]] QString cue_display_plain(const freeform::Deck& deck, const freeform::Cue& cue)
{
    QString plain = QString::fromStdString(model::plain_text_lines(
        model::parse_markup_lines(freeform::cue_display_markup(deck, cue))));
    plain.remove(QLatin1Char('\n'));
    return plain;
}

[[nodiscard]] QVariantMap insert_kind(const char* id, const QString& label, const char* icon,
                                      const char* type, const char* binding,
                                      const char* menu = nullptr)
{
    QVariantMap m{
        {QStringLiteral("id"), QString::fromUtf8(id)},
        {QStringLiteral("label"), label},
        {QStringLiteral("icon"), QString::fromUtf8(icon)},
        {QStringLiteral("type"), QString::fromUtf8(type)},
        {QStringLiteral("binding"), QString::fromUtf8(binding)},
    };
    if (menu != nullptr) {
        m.insert(QStringLiteral("menu"), QString::fromUtf8(menu));
    }
    return m;
}

}  // namespace

// --- objects ------------------------------------------------------------

SlideObjectsSession::SlideObjectsSession(SlideEditSession& owner)
    : QObject(&owner)
    , owner_(owner)
    , canvas_(&owner.freeform_commands_)
{
    connect(this, &SlideObjectsSession::documentChanged, this, [this] {
        canvas_objects_ = build_canvas_objects();
        canvas_model_.apply(canvas_objects_);
        emit canvasObjectsChanged();
    });
}

QString SlideObjectsSession::draw(const QVariantList& points, bool smooth, bool closed)
{
    if (!owner_.freeform_mode()) {
        return refusal::nothing_open();
    }
    std::vector<geom::PathPoint> path;
    path.reserve(static_cast<std::size_t>(points.size()));
    for (const auto& entry : points) {
        const auto pt = entry.toList();
        if (pt.size() >= 2) {
            path.push_back({.x = pt[0].toDouble(), .y = pt[1].toDouble(), .smooth = smooth});
        }
    }
    return apply(owner_.freeform_.add_drawn(std::move(path), closed));
}

QString SlideObjectsSession::insert(const QString& type, const QVariantMap& options)
{
    const auto kind = freeform::object_type_from_string(type.toStdString());
    const auto shape = geom::shape_kind_from_string(
        options.value(QStringLiteral("shapeKind"), QStringLiteral("rectangle"))
            .toString().toStdString());
    if (owner_.text_theme_mode()) {
        const auto binding = theme::element_binding_from_string(
            options.value(QStringLiteral("binding"), QStringLiteral("static"))
                .toString().toStdString());
        return apply(owner_.theme_editor_.insert(
            kind, binding, std::string(geom::to_string(shape))));
    }
    if (!owner_.freeform_mode()) {
        return refusal::nothing_open();
    }
    auto& ed = owner_.freeform_;
    switch (kind) {
    case canvas::ObjectType::Text:
        return apply(ed.add_text());
    case canvas::ObjectType::Image:
        return apply(ed.add_image(options.value(QStringLiteral("filename")).toString().toStdString()));
    case canvas::ObjectType::Shape:
        return apply(ed.add_shape(shape));
    case canvas::ObjectType::Indicator:
        return apply(ed.add_indicator());
    case canvas::ObjectType::Clock:
        return apply(ed.add_clock());
    case canvas::ObjectType::Video:
        return apply(ed.add_video(options.value(QStringLiteral("filename")).toString().toStdString()));
    case canvas::ObjectType::Table:
        return apply(ed.add_table(options.value(QStringLiteral("rows"), 3).toInt(),
                                  options.value(QStringLiteral("columns"), 3).toInt()));
    }
    return tr("SeventhSlide cannot put a “%1” on a slide.").arg(type);
}

QString SlideObjectsSession::setText(const QString& object_id, const QString& text, bool bold,
                                     bool italic, bool underline)
{
    if (owner_.text_theme_mode()) {
        return apply(owner_.theme_editor_.set_text(object_id.toStdString(),
                                                   sanitize_editor_plain(text.toStdString()), bold,
                                                   italic, underline));
    }
    if (!owner_.freeform_mode()) {
        return refusal::nothing_open();
    }
    auto& ed = owner_.freeform_;
    const auto id = object_id.toStdString();
    if (ed.selected_id() != id) {
        ed.select(id, false);
    }
    const auto* obj = ed.selected_item();
    if (obj == nullptr) {
        return tr("That object is no longer on the slide.");
    }
    // The flags that changed, each its own step; the text last, so the box
    // reads as typed.
    for (const auto& [name, now, want] : {std::tuple{"bold", obj->bold, bold},
                                          std::tuple{"italic", obj->italic, italic},
                                          std::tuple{"underline", obj->underline, underline}}) {
        if (now != want) {
            if (const auto r = ed.set_property(name, want); !r) {
                return QString::fromStdString(r.error);
            }
        }
    }
    return apply(ed.set_text(text.toStdString()));
}

QString SlideObjectsSession::applyTextAttrs(const QVariantMap& attrs)
{
    if (owner_.text_theme_mode()) {
        return apply(owner_.theme_editor_.apply_text_attrs(span_attrs_from_qml(attrs)));
    }
    if (!owner_.freeform_mode()) {
        return refusal::nothing_open();
    }
    // A Freeform box's whole-box style is its properties.
    auto& ed = owner_.freeform_;
    const auto a = span_attrs_from_qml(attrs);
    const auto set = [&](const char* name, const nlohmann::json& v) {
        return ed.set_property(name, v);
    };
    freeform::EditResult r;
    if (a.bold && (r = set("bold", *a.bold), !r)) {
        return QString::fromStdString(r.error);
    }
    if (a.italic && (r = set("italic", *a.italic), !r)) {
        return QString::fromStdString(r.error);
    }
    if (a.underline && (r = set("underline", *a.underline), !r)) {
        return QString::fromStdString(r.error);
    }
    if (a.color && (r = set("font_color", *a.color), !r)) {
        return QString::fromStdString(r.error);
    }
    if (a.font_family && (r = set("font_family", *a.font_family), !r)) {
        return QString::fromStdString(r.error);
    }
    return apply(freeform::EditResult::success());
}

QString SlideObjectsSession::setTransition(const QString& type, int duration_ms)
{
    const auto spec = spec_from_qml(type, duration_ms).value_or(model::Transition{});
    if (owner_.text_theme_mode()) {
        return apply(owner_.theme_editor_.set_transition(spec));
    }
    if (!owner_.freeform_mode()) {
        return refusal::nothing_open();
    }
    return apply(owner_.freeform_.set_object_transition(spec));
}

QVariantList SlideObjectsSession::parseRichLines(const QString& text, bool bold, bool italic,
                                                 bool underline) const
{
    auto lines = model::parse_markup_lines(text.toStdString());
    const bool styled = rich_lines_styled(lines);
    QVariantList out;
    out.reserve(static_cast<int>(lines.size()));
    for (auto& line : lines) {
        if (line.empty()) {
            model::TextSpan empty;
            empty.style.bold = styled ? false : bold;
            empty.style.italic = styled ? false : italic;
            empty.style.underline = styled ? false : underline;
            line.push_back(std::move(empty));
        } else if (!styled) {
            for (auto& sp : line) {
                sp.style.bold = bold;
                sp.style.italic = italic;
                sp.style.underline = underline;
            }
        }
        out.push_back(rich_line_to_map(line));
    }
    return out;
}

QString SlideObjectsSession::clockText(bool clock_24h, bool show_seconds) const
{
    const QTime now = QTime::currentTime();
    return QString::fromStdString(scene::format_wall_clock_hms(
        now.hour(), now.minute(), now.second(), clock_24h, show_seconds));
}

QVariantMap SlideObjectsSession::selection() const
{
    if (owner_.text_theme_mode()) {
        if (const auto* el = owner_.theme_editor_.selected_item()) {
            return element_map(*el);
        }
        theme::Element fresh;
        fresh.object.fill.color = freeform::k_default_shape_fill;
        fresh.object.line.color = freeform::k_default_shape_stroke;
        return element_map(fresh);
    }
    if (const auto* obj = owner_.freeform_mode() ? owner_.freeform_.selected_item() : nullptr) {
        return object_map(*obj);
    }
    // With nothing selected, a new object's values: what the idle ribbon
    // shows. Its fill and outline are a new shape's, the only kind that has
    // them from the start.
    freeform::Object fresh;
    fresh.fill.color = freeform::k_default_shape_fill;
    fresh.line.color = freeform::k_default_shape_stroke;
    return object_map(fresh);
}

QVariantMap SlideObjectsSession::ranges() const
{
    if (owner_.text_theme_mode()) {
        const auto* el = owner_.theme_editor_.selected_item();
        return to_qvariant(theme::element_property_ranges(el ? *el : theme::Element{})).toMap();
    }
    const auto* obj = owner_.freeform_mode() ? owner_.freeform_.selected_item() : nullptr;
    return to_qvariant(freeform::object_property_ranges(obj ? *obj : freeform::Object{})).toMap();
}

bool SlideObjectsSession::hasSelection() const
{
    return !selectedIds().isEmpty();
}

QString SlideObjectsSession::selectedType() const
{
    if (owner_.text_theme_mode()) {
        const auto* el = owner_.theme_editor_.selected_item();
        return el ? QString::fromUtf8(canvas::to_string(el->object.type)) : QString();
    }
    const auto* obj = owner_.freeform_mode() ? owner_.freeform_.selected_item() : nullptr;
    return obj ? QString::fromUtf8(canvas::to_string(obj->type)) : QString();
}

QVariantMap SlideObjectsSession::supports() const
{
    theme::ObjectControls can;
    if (owner_.text_theme_mode()) {
        if (const auto* el = owner_.theme_editor_.selected_item()) {
            can = theme::object_controls(el->object.type, el->binding);
        }
    } else if (const auto* obj = owner_.freeform_mode() ? owner_.freeform_.selected_item() : nullptr) {
        can = theme::object_controls(obj->type, theme::ElementBinding::Static);
    }
    return {
        {QStringLiteral("runs"), can.runs},
        {QStringLiteral("weight"), can.weight},
        {QStringLiteral("font"), can.font},
        {QStringLiteral("color"), can.color},
        {QStringLiteral("align"), can.align},
        {QStringLiteral("fit"), can.fit},
        {QStringLiteral("shape"), can.shape},
        {QStringLiteral("clock"), can.clock},
        {QStringLiteral("indicator"), can.indicator},
        {QStringLiteral("effects"), can.effects},
        {QStringLiteral("fill"), can.fill},
    };
}

int SlideObjectsSession::chordRoom() const
{
    const auto* obj = owner_.freeform_mode() ? owner_.freeform_.selected_item() : nullptr;
    if (obj == nullptr || obj->type != canvas::ObjectType::Text) {
        return 0;
    }
    const auto type = freeform::painted_type(*obj);
    layout::FontStyle lyric;
    lyric.family = type.font_family;
    lyric.size_px = type.font_size;
    lyric.bold = type.bold;
    lyric.italic = type.italic;
    lyric.letter_spacing = type.letter_spacing;
    return layout::chord_row_px(owner_.previews_.measurer(), lyric, obj->chords);
}

QVariantList SlideObjectsSession::build_canvas_objects() const
{
    QVariantList out;
    if (owner_.text_theme_mode()) {
        std::vector<const theme::Element*> els;
        for (const auto& el : owner_.theme_editor_.current()) {
            els.push_back(&el);
        }
        std::stable_sort(els.begin(), els.end(), [](const theme::Element* a,
                                                    const theme::Element* b) {
            return a->object.z < b->object.z;
        });
        for (const auto* el : els) {
            out.push_back(element_map(*el));
        }
        return out;
    }
    if (!owner_.freeform_mode()) {
        return out;
    }
    const auto& editor = owner_.freeform_;
    const auto* slide = editor.current_slide();
    if (!slide) {
        return out;
    }
    std::vector<const freeform::Object*> objs;
    objs.reserve(slide->objects.size());
    for (const auto& obj : slide->objects) {
        objs.push_back(&obj);
    }
    std::stable_sort(objs.begin(), objs.end(),
                     [](const freeform::Object* a, const freeform::Object* b) { return a->z < b->z; });
    const auto& doc = editor.document();
    const auto sections = doc.line_control ? freeform::cue_sections(doc.cues)
                                           : freeform::CueSections{};
    const auto* deck = editor.current_deck();
    const int slide_index = editor.slide_index();
    const int slide_count = deck ? deck->slide_count() : 0;
    const std::vector<int> empty_map;
    const auto& line_to_slide = deck ? deck->line_to_slide : empty_map;
    const int cue_index = editor.cue_index();
    for (const auto* obj : objs) {
        QVariantMap m = object_map(*obj);
        // The canvas shows what the object will read on screen, so an
        // indicator, a clock and a cue-linked text box resolve here; the
        // sidebar edits the stored text instead.
        std::string text;
        if (obj->type == freeform::ObjectType::Indicator) {
            const auto items = scene::resolve_indicator_items(
                {.codes = sections.codes,
                 .line_section = sections.cue_section,
                 .line_to_slide = line_to_slide},
                cue_index, slide_index, slide_count,
                obj->indicator_fallback == freeform::IndicatorFallback::Blank);
            text = scene::format_indicator_markup(items, obj->indicator_gap);
        } else if (obj->type == freeform::ObjectType::Clock) {
            text = clockText(obj->clock_24h, obj->clock_seconds).toStdString();
        } else if (obj->type == freeform::ObjectType::Text && obj->cue_linked && doc.line_control
                   && deck) {
            auto resolved = freeform::resolved_object_text(doc, *deck, slide_index, *obj,
                                                           owner_.previews_.measurer());
            text = std::move(resolved.markup);
            if (std::ranges::any_of(resolved.gaps_above, [](int gap) { return gap > 0; })) {
                QVariantList gaps;
                for (const int gap : resolved.gaps_above) {
                    gaps.push_back(gap);
                }
                m.insert(QStringLiteral("gaps_above"), gaps);
            }
        } else {
            text = obj->text;
        }
        m.insert(QStringLiteral("text"), QString::fromStdString(text));
        out.push_back(m);
    }
    return out;
}

// --- slides -------------------------------------------------------------

SlideSlidesSession::SlideSlidesSession(SlideEditSession& owner)
    : QObject(&owner)
    , owner_(owner)
    , list_(this)
{
}

bool SlideSlidesSession::reload()
{
    const auto [width, height] = owner_.canvas();
    return list_.reload(owner_.freeform_, width, height, owner_.previews_.theme_revision());
}

bool SlideSlidesSession::refresh_current_slide()
{
    const auto [width, height] = owner_.canvas();
    return list_.refresh_slide(owner_.freeform_, owner_.freeform_.slide_index(), width, height,
                               owner_.previews_.theme_revision());
}

void SlideSlidesSession::invalidate_preview()
{
    ++preview_revision_;
    preview_build_dirty_ = true;
}

void SlideSlidesSession::select(int index)
{
    if (!owner_.freeform_mode()) {
        return;
    }
    owner_.freeform_.select_slide(index);
    owner_.view_moved();
}

void SlideSlidesSession::extendTo(int index)
{
    if (!owner_.freeform_mode()) {
        return;
    }
    owner_.freeform_.extend_slide_selection(index);
    owner_.view_moved();
}

void SlideSlidesSession::selectAll()
{
    if (!owner_.freeform_mode()) {
        return;
    }
    owner_.freeform_.select_all_slides();
    owner_.view_moved();
}

int SlideSlidesSession::selectedCount() const
{
    if (!owner_.freeform_mode() || owner_.freeform_.slide_count() <= 0) {
        return 0;
    }
    return owner_.freeform_.slide_range_last() - owner_.freeform_.slide_range_first() + 1;
}

QString SlideSlidesSession::insert()
{
    return owner_.apply(owner_.freeform_.insert_slide());
}

QString SlideSlidesSession::duplicate()
{
    return owner_.apply(owner_.freeform_.duplicate_slide());
}

QString SlideSlidesSession::remove()
{
    return owner_.apply(owner_.freeform_.delete_slide());
}

QString SlideSlidesSession::reorder(int from, int to)
{
    return owner_.apply(owner_.freeform_.reorder_slides(from, to));
}

QString SlideSlidesSession::copy()
{
    const auto r = owner_.freeform_.copy_slides();
    if (!r) {
        return QString::fromStdString(r.error);
    }
    emit owner_.clipboardChanged();
    emit owner_.objects_->clipboardChanged();
    return {};
}

QString SlideSlidesSession::placeThumb(int slide_index, int last_cue)
{
    return owner_.apply(owner_.freeform_.place_slide_thumb(slide_index, last_cue));
}

QString SlideSlidesSession::setBackground(const QString& kind, const QString& value)
{
    freeform::SlideBackground bg;
    bg.kind = freeform::slide_background_kind_from_string(kind.toStdString());
    const auto v = value.toStdString();
    switch (bg.kind) {
    case freeform::SlideBackgroundKind::Video:
        bg.video = v;
        break;
    case freeform::SlideBackgroundKind::Color:
        // No colour given: keep the slide's, or start on what an empty one paints.
        if (!v.empty()) {
            bg.color = v;
        } else if (const auto* now = current_slide_background(owner_.freeform_);
                   now != nullptr && now->kind == freeform::SlideBackgroundKind::Color
                   && !now->color.empty()) {
            bg.color = now->color;
        } else {
            bg.color = layout::k_default_fill_color;
        }
        break;
    case freeform::SlideBackgroundKind::Image:
        bg.image = v;
        break;
    case freeform::SlideBackgroundKind::None:
        break;
    }
    return owner_.apply(owner_.freeform_.set_slide_background(std::move(bg)));
}

QString SlideSlidesSession::setBackgroundTransition(const QString& type, int duration_ms)
{
    return owner_.apply(
        owner_.freeform_.set_slide_background_transition(spec_from_qml(type, duration_ms)));
}

QString SlideSlidesSession::copyBackgroundTheme(const QString& theme_id)
{
    const auto* slide = owner_.freeform_mode() ? owner_.freeform_.current_slide() : nullptr;
    if (slide == nullptr || theme_id.isEmpty()) {
        return tr("There is no slide to copy the background onto.");
    }
    return owner_.copy_background_into(owner_.freeform_.output_id(), theme_id.toStdString(),
                                       slide->id);
}

QString SlideSlidesSession::setSlideTransition(const QString& type, int duration_ms, bool all,
                                              const QString& direction)
{
    if (!owner_.freeform_mode()) {
        return refusal::nothing_open();
    }
    return owner_.apply(
        owner_.freeform_.set_slide_transition(spec_from_qml(type, duration_ms, direction), all));
}

QStringList SlideSlidesSession::deckOutputs() const
{
    QStringList out;
    if (!owner_.freeform_mode()) {
        return out;
    }
    const auto& current = owner_.freeform_.output_id();
    const auto& on = owner_.session_.outputs_on();
    for (const auto& oc : owner_.session_.outputs()) {
        const bool is_on = std::ranges::any_of(on, [&](const auto& o) { return o.id == oc.id; });
        if (oc.id == current
            || (is_on && freeform::find_deck(owner_.freeform_.document(), oc.id))) {
            out.push_back(QString::fromStdString(oc.name));
        }
    }
    return out;
}

QString SlideSlidesSession::currentId() const
{
    const auto* slide = owner_.freeform_mode() ? owner_.freeform_.current_slide() : nullptr;
    return slide ? QString::fromStdString(slide->id) : QString();
}

int SlideSlidesSession::index() const
{
    return owner_.freeform_mode() ? owner_.freeform_.slide_index() : 0;
}

int SlideSlidesSession::count() const
{
    return owner_.freeform_mode() ? owner_.freeform_.slide_count() : 0;
}

QString SlideSlidesSession::notes() const
{
    const auto* slide = owner_.freeform_mode() ? owner_.freeform_.current_slide() : nullptr;
    return slide ? QString::fromStdString(slide->notes) : QString();
}

QString SlideSlidesSession::setNotes(const QString& notes)
{
    return owner_.apply(owner_.freeform_.set_slide_notes(notes.toStdString()));
}

QVariantMap SlideSlidesSession::background() const
{
    QVariantMap m;
    const auto* bg = owner_.freeform_mode() ? current_slide_background(owner_.freeform_) : nullptr;
    m.insert(QStringLiteral("kind"),
             bg ? QString::fromUtf8(freeform::to_string(bg->kind)) : QStringLiteral("inherit"));
    m.insert(QStringLiteral("color"), bg ? QString::fromStdString(bg->color) : QString());
    m.insert(QStringLiteral("image"), bg ? QString::fromStdString(bg->image) : QString());
    m.insert(QStringLiteral("video"), bg ? QString::fromStdString(bg->video) : QString());
    const auto* slide = owner_.freeform_mode() ? owner_.freeform_.current_slide() : nullptr;
    // Unset is a cut: nothing of a theme's reaches a Freeform.
    const auto tr = slide && slide->background_transition ? *slide->background_transition
                                                          : model::Transition{};
    m.insert(QStringLiteral("transition_type"), transition_type_label(tr));
    m.insert(QStringLiteral("transition_ms"), tr.duration_ms);
    const auto own = slide && slide->transition ? *slide->transition : model::Transition{};
    m.insert(QStringLiteral("slide_transition_type"), transition_type_label(own));
    m.insert(QStringLiteral("slide_transition_ms"), own.duration_ms);
    m.insert(QStringLiteral("slide_transition_direction"),
             QString::fromUtf8(model::to_string(own.direction)));
    return m;
}

void SlideSlidesSession::ensure_preview_build() const
{
    if (!owner_.freeform_mode()) {
        preview_build_ = {};
        preview_build_dirty_ = true;
        return;
    }
    if (!preview_build_dirty_ && !preview_build_.outputs.empty()) {
        return;
    }
    preview_build_ = build_freeform_slides(owner_.freeform_.document(), owner_.session_.outputs(),
                                           owner_.session_.theme_priority(), owner_.previews_);
    preview_build_dirty_ = false;
}

QImage SlideSlidesSession::render_preview(const QString& output, int slide_index,
                                          const QSize& size, const QString& slide_id) const
{
    return finish_preview(prepare_preview(output, slide_index, size, slide_id));
}

PreviewJob SlideSlidesSession::prepare_preview(const QString& output, int slide_index,
                                               const QSize& size, const QString& slide_id) const
{
    if (!owner_.freeform_mode()) {
        return {};
    }
    const auto& editor = owner_.freeform_;
    const std::string output_id = output.isEmpty() ? editor.output_id() : output.toStdString();
    const auto* deck = freeform::find_deck(editor.document(), output_id);
    if (deck == nullptr) {
        deck = editor.current_deck();
    }
    int resolved = slide_index;
    if (!slide_id.isEmpty() && deck != nullptr) {
        const auto want = slide_id.toStdString();
        resolved = -1;
        for (int i = 0; i < deck->slide_count(); ++i) {
            if (deck->slides[static_cast<std::size_t>(i)].id == want) {
                resolved = i;
                break;
            }
        }
    }
    if (resolved < 0) {
        return {};
    }
    ensure_preview_build();
    const build::OutputRuntime* rt = nullptr;
    for (const auto& out : preview_build_.outputs) {
        if (out.output_id == output_id) {
            rt = &out;
            break;
        }
    }
    if (rt == nullptr) {
        return {};
    }
    const auto canvas = model::output_canvas(owner_.session_.outputs(), output_id);
    QSize want = size_or(
        size, QSize(320, std::max(1, 320 * canvas.height / std::max(1, canvas.width))));
    const int max_edge = std::max(want.width(), want.height());
    if (max_edge > 512) {
        want = want.scaled(512, 512, Qt::KeepAspectRatio);
    }
    return {.still = freeform_slide_still(*rt, canvas, resolved, want, owner_.previews_)};
}

// --- cues ---------------------------------------------------------------

SlideCuesSession::SlideCuesSession(SlideEditSession& owner)
    : QObject(&owner)
    , owner_(owner)
{
}

void SlideCuesSession::select(int index)
{
    if (!owner_.freeform_mode()) {
        return;
    }
    owner_.freeform_.select_cue(index);
    owner_.view_moved();
}

QString SlideCuesSession::setLineControl(bool on, bool flatten)
{
    return owner_.apply(owner_.freeform_.set_line_control(on, flatten));
}

QString SlideCuesSession::insert(const QString& text)
{
    return owner_.apply(owner_.freeform_.insert_cue(text.toStdString()));
}

QString SlideCuesSession::remove()
{
    return owner_.apply(owner_.freeform_.delete_cue());
}

QString SlideCuesSession::rename(const QString& text)
{
    return owner_.apply(owner_.freeform_.rename_cue(text.toStdString()));
}

QString SlideCuesSession::setVerse(const QString& type, int number)
{
    return owner_.apply(owner_.freeform_.set_cue_verse(type.toStdString(), number));
}

QString SlideCuesSession::setScripture(int chapter, int verse)
{
    return owner_.apply(owner_.freeform_.set_cue_scripture(chapter, verse));
}

QString SlideCuesSession::setRefShown(bool shown)
{
    return owner_.apply(owner_.freeform_.set_cue_ref_shown(shown));
}

QString SlideCuesSession::setSectionStart(bool on)
{
    return owner_.apply(owner_.freeform_.set_cue_section_start(on));
}

QString SlideCuesSession::setLinked(bool on)
{
    return owner_.apply(owner_.freeform_.set_cue_linked(on));
}

QString SlideCuesSession::setLinkedStyle(const QString& markup)
{
    return owner_.apply(owner_.freeform_.set_linked_style(markup.toStdString()));
}

QString SlideCuesSession::linkedEditMarkup() const
{
    if (!owner_.freeform_mode()) {
        return {};
    }
    const auto& editor = owner_.freeform_;
    const auto* obj = editor.selected_item();
    const auto* deck = editor.current_deck();
    if (!obj || !deck || obj->type != freeform::ObjectType::Text || !obj->cue_linked) {
        return {};
    }
    return QString::fromStdString(freeform::linked_snapshot_markup(
        editor.document(), *deck, editor.slide_index(), *obj));
}

QVariantMap SlideCuesSession::linkedEditInfo() const
{
    if (!owner_.freeform_mode()) {
        return {};
    }
    const auto& editor = owner_.freeform_;
    const auto* obj = editor.selected_item();
    const auto* deck = editor.current_deck();
    if (!obj || !deck) {
        return {};
    }
    const auto& doc = editor.document();
    const auto edit =
        freeform::linked_edit(doc, *deck, editor.slide_index(), *obj, owner_.previews_.measurer());
    if (edit.owned <= 0) {
        return {};
    }
    QVariantList cues;
    for (std::size_t i = 0; i < edit.chords.size(); ++i) {
        const auto& cue = doc.cues[static_cast<std::size_t>(edit.first_cue) + i];
        const QByteArray utf8 = cue_display_plain(*deck, cue).toUtf8();
        QVariantList chords;
        for (const auto& mark : edit.chords[i]) {
            chords.push_back(QVariantMap{
                {QStringLiteral("at"), utf16_offset(utf8, mark.at)},
                {QStringLiteral("chord"), QString::fromStdString(mark.chord)},
            });
        }
        cues.push_back(QVariantMap{
            {QStringLiteral("length"), QString::fromUtf8(utf8).size()},
            {QStringLiteral("before"), static_cast<qsizetype>(edit.spaces_before[i])},
            {QStringLiteral("chords"), chords},
        });
    }
    return {
        {QStringLiteral("markup"), QString::fromStdString(edit.markup)},
        {QStringLiteral("upcoming"), edit.upcoming},
        {QStringLiteral("cues"), cues},
    };
}

QString SlideCuesSession::setLinkedEdit(const QString& markup, int upcoming,
                                        const QVariantList& cues)
{
    if (!owner_.freeform_mode()) {
        return refusal::nothing_open();
    }
    const auto& editor = owner_.freeform_;
    const auto* deck = editor.current_deck();
    if (!deck) {
        return tr("This slide is in no deck.");
    }
    const auto& doc = editor.document();
    int first_cue = -1;
    for (const auto& span : freeform::deck_spans(doc, *deck)) {
        if (span.slide_index == editor.slide_index()) {
            first_cue = std::max(0, span.first_cue);
            break;
        }
    }
    std::vector<std::vector<layout::ChordMark>> chords;
    for (qsizetype i = 0; i < cues.size() && first_cue >= 0; ++i) {
        const auto c = static_cast<std::size_t>(first_cue) + static_cast<std::size_t>(i);
        if (c >= doc.cues.size()) {
            break;
        }
        const QString plain = cue_display_plain(*deck, doc.cues[c]);
        std::vector<layout::ChordMark> marks;
        for (const auto& v : cues[i].toMap().value(QStringLiteral("chords")).toList()) {
            const QVariantMap m = v.toMap();
            QString name = m.value(QStringLiteral("chord")).toString();
            name.remove(QLatin1Char('['));
            name.remove(QLatin1Char(']'));
            name = name.trimmed();
            if (name.isEmpty()) {
                continue;
            }
            marks.push_back({utf8_offset(plain, m.value(QStringLiteral("at")).toInt()),
                             name.toStdString()});
        }
        chords.push_back(std::move(marks));
    }
    return owner_.apply(
        owner_.freeform_.set_linked_edit(markup.toStdString(), upcoming, std::move(chords)));
}

bool SlideCuesSession::linkedEditAllowed(const QString& plain, int upcoming) const
{
    return owner_.freeform_mode()
        && owner_.freeform_.linked_edit_allowed(plain.toStdString(), upcoming);
}

QString SlideCuesSession::setShowUpcoming(bool on)
{
    return owner_.apply(owner_.freeform_.set_show_upcoming(on));
}

bool SlideCuesSession::lineControl() const
{
    return owner_.freeform_mode() && owner_.freeform_.line_control();
}

int SlideCuesSession::index() const
{
    return owner_.freeform_mode() ? owner_.freeform_.cue_index() : -1;
}

QVariantMap SlideCuesSession::current() const
{
    const auto* cue = owner_.freeform_mode() ? current_cue(owner_.freeform_) : nullptr;
    if (cue == nullptr) {
        // No cue: the fields read as a new cue would, so the ribbon shows
        // nothing of its own.
        const freeform::Cue none;
        return {
            {QStringLiteral("text"), QString()},
            {QStringLiteral("verse_type"), QString()},
            {QStringLiteral("verse_number"), none.verse_number},
            {QStringLiteral("chapter"), none.chapter},
            {QStringLiteral("verse"), none.verse},
            {QStringLiteral("show_ref"), none.show_ref},
            {QStringLiteral("section_start"), none.section_start},
            {QStringLiteral("can_start_section"), false},
        };
    }
    const auto& editor = owner_.freeform_;
    return {
        {QStringLiteral("text"), QString::fromStdString(cue->text)},
        {QStringLiteral("verse_type"), QString::fromStdString(cue->verse_type)},
        {QStringLiteral("verse_number"), cue->verse_number},
        {QStringLiteral("chapter"), cue->chapter},
        {QStringLiteral("verse"), cue->verse},
        {QStringLiteral("show_ref"), cue->show_ref},
        {QStringLiteral("section_start"), cue->section_start},
        // The cue before has its label: only a mark can part them.
        {QStringLiteral("can_start_section"),
         freeform::can_start_section(editor.document().cues, editor.cue_index())},
    };
}

QStringList SlideCuesSession::verseTypes() const
{
    QStringList out;
    for (const auto name : parse::verse_type_names()) {
        out.push_back(QString::fromUtf8(name));
    }
    return out;
}

bool SlideCuesSession::canDelete() const
{
    return owner_.freeform_mode() && owner_.freeform_.can_delete_cue();
}

bool SlideCuesSession::canDisableLineControl() const
{
    return owner_.freeform_mode() && owner_.freeform_.can_disable_line_control();
}

// --- animations ------------------------------------------------

namespace {

/// What the Build pane calls an effect.
[[nodiscard]] QString effect_label(canvas::AnimationEffect effect)
{
    using E = canvas::AnimationEffect;
    switch (effect) {
    case E::Appear: return SlideAnimationsSession::tr("Appear");
    case E::Fade: return SlideAnimationsSession::tr("Fade in");
    case E::FlyIn: return SlideAnimationsSession::tr("Fly in");
    case E::Wipe: return SlideAnimationsSession::tr("Wipe in");
    case E::Zoom: return SlideAnimationsSession::tr("Zoom in");
    case E::Float: return SlideAnimationsSession::tr("Float in");
    case E::Pulse: return SlideAnimationsSession::tr("Pulse");
    case E::Grow: return SlideAnimationsSession::tr("Grow");
    case E::Disappear: return SlideAnimationsSession::tr("Disappear");
    case E::FadeOut: return SlideAnimationsSession::tr("Fade out");
    case E::FlyOut: return SlideAnimationsSession::tr("Fly out");
    case E::WipeOut: return SlideAnimationsSession::tr("Wipe out");
    case E::ZoomOut: return SlideAnimationsSession::tr("Zoom out");
    case E::FloatOut: return SlideAnimationsSession::tr("Float out");
    }
    return {};
}

[[nodiscard]] QString trigger_label(canvas::AnimationTrigger trigger)
{
    switch (trigger) {
    case canvas::AnimationTrigger::WithPrevious:
        return SlideAnimationsSession::tr("with previous");
    case canvas::AnimationTrigger::AfterPrevious:
        return SlideAnimationsSession::tr("after previous");
    case canvas::AnimationTrigger::OnClick:
        break;
    }
    return SlideAnimationsSession::tr("on click");
}

/// What the Build pane calls ``obj``: its words, else its kind.
[[nodiscard]] QString object_label(const freeform::Object& obj)
{
    const auto words = plain_of(obj).simplified();
    if (!words.isEmpty()) {
        return words.length() > 28 ? words.left(27) + QChar(0x2026) : words;
    }
    return QString::fromUtf8(canvas::to_string(obj.type));
}

}  // namespace

SlideAnimationsSession::SlideAnimationsSession(SlideEditSession& owner)
    : QObject(&owner)
    , owner_(owner)
{
}

SlideAnimationsSession::~SlideAnimationsSession() = default;

QVariantList SlideAnimationsSession::list() const
{
    QVariantList out;
    if (!owner_.freeform_mode()) {
        return out;
    }
    const auto* slide = owner_.freeform_.current_slide();
    int step = 0;
    int index = 0;
    for (const auto& [object_id, a] : owner_.freeform_.slide_animations()) {
        QString name;
        if (slide != nullptr) {
            for (const auto& obj : slide->objects) {
                if (obj.id == object_id) {
                    name = object_label(obj);
                }
            }
        }
        const bool click = a.trigger == canvas::AnimationTrigger::OnClick;
        if (click) {
            ++step;
        }
        const auto kind = canvas::animation_kind(a.effect);
        out.push_back(QVariantMap{
            {QStringLiteral("objectId"), QString::fromStdString(object_id)},
            {QStringLiteral("order"), a.order},
            {QStringLiteral("index"), index++},
            {QStringLiteral("effect"), QString::fromUtf8(canvas::to_string(a.effect))},
            {QStringLiteral("kind"), kind == canvas::AnimationKind::Exit       ? QStringLiteral("exit")
                                     : kind == canvas::AnimationKind::Emphasis ? QStringLiteral("emphasis")
                                                                               : QStringLiteral("entrance")},
            {QStringLiteral("trigger"), QString::fromUtf8(canvas::to_string(a.trigger))},
            {QStringLiteral("direction"), QString::fromUtf8(model::to_string(a.direction))},
            {QStringLiteral("durationMs"), a.duration_ms},
            {QStringLiteral("delayMs"), a.delay_ms},
            {QStringLiteral("name"), name},
            {QStringLiteral("step"), click ? step : 0},
            {QStringLiteral("summary"),
             QStringLiteral("%1 \u00b7 %2 \u00b7 %3 s")
                 .arg(effect_label(a.effect), trigger_label(a.trigger),
                      QString::number(a.duration_ms / 1000.0, 'f', 1))},
        });
    }
    return out;
}

QString SlideAnimationsSession::add(const QString& effect)
{
    if (!owner_.freeform_mode()) {
        return refusal::nothing_open();
    }
    return owner_.apply(
        owner_.freeform_.add_animation(canvas::animation_effect_from_string(effect.toStdString())));
}

QString SlideAnimationsSession::set(const QString& object_id, int order, const QString& field,
                                    const QVariant& value)
{
    if (!owner_.freeform_mode()) {
        return refusal::nothing_open();
    }
    std::optional<canvas::Animation> now;
    for (const auto& [id, a] : owner_.freeform_.slide_animations()) {
        if (id == object_id.toStdString() && a.order == order) {
            now = a;
        }
    }
    if (!now) {
        return tr("That animation is no longer on the slide.");
    }
    const auto text = value.toString().toStdString();
    if (field == QLatin1String("effect")) {
        now->effect = canvas::animation_effect_from_string(text);
    } else if (field == QLatin1String("trigger")) {
        now->trigger = canvas::animation_trigger_from_string(text);
    } else if (field == QLatin1String("direction")) {
        now->direction = model::transition_direction_from_string(text);
    } else if (field == QLatin1String("duration_ms")) {
        now->duration_ms = value.toInt();
    } else if (field == QLatin1String("delay_ms")) {
        now->delay_ms = value.toInt();
    } else {
        return tr("That is not a setting an animation has.");
    }
    return owner_.apply(owner_.freeform_.set_animation(object_id.toStdString(), order, *now));
}

QString SlideAnimationsSession::remove(const QString& object_id, int order)
{
    if (!owner_.freeform_mode()) {
        return refusal::nothing_open();
    }
    return owner_.apply(owner_.freeform_.remove_animation(object_id.toStdString(), order));
}

QString SlideAnimationsSession::move(int from, int to)
{
    if (!owner_.freeform_mode()) {
        return refusal::nothing_open();
    }
    return owner_.apply(owner_.freeform_.move_animation(from, to));
}

render::ScenePresenter* SlideAnimationsSession::presenter()
{
    if (!presenter_) {
        presenter_ = std::make_unique<render::ScenePresenter>();
        presenter_->set_font_faces(&owner_.previews_.measurer().faces());
        // The previews' cache, which follows the library open now and the
        // pictures that arrive in it.
        presenter_->share_image_cache(owner_.previews_.shared_image_cache());
    }
    return presenter_.get();
}

void SlideAnimationsSession::play()
{
    if (!owner_.freeform_mode()) {
        return;
    }
    const auto run = ++run_;
    auto* p = presenter();
    // Nothing first, so the slide comes in by its transition.
    const auto [w, h] = owner_.canvas();
    scene::SceneFrame empty;
    empty.canvas_width = w;
    empty.canvas_height = h;
    p->present(scene::share_frame(empty));
    p->settle();
    if (!playing_) {
        playing_ = true;
        emit playingChanged();
    }
    play_step(0, run);
}

void SlideAnimationsSession::stop()
{
    ++run_;
    if (playing_) {
        playing_ = false;
        emit playingChanged();
    }
}

void SlideAnimationsSession::play_step(int step, std::uint64_t run)
{
    if (run != run_ || !owner_.freeform_mode()) {
        return;
    }
    owner_.slides_->ensure_preview_build();
    const auto& built = owner_.slides_->preview_build_;
    const auto& output = owner_.freeform_.output_id();
    const auto rt = std::find_if(built.outputs.begin(), built.outputs.end(),
                                 [&](const build::OutputRuntime& r) { return r.output_id == output; });
    const int slide = owner_.freeform_.slide_index();
    if (rt == built.outputs.end() || slide < 0
        || slide >= static_cast<int>(rt->compiled_slides.size())) {
        stop();
        return;
    }
    const int steps = static_cast<int>(rt->compiled_slides[static_cast<std::size_t>(slide)].steps.size());
    if (step >= steps) {
        QTimer::singleShot(900, this, [this, run] {
            if (run == run_) {
                stop();
            }
        });
        return;
    }
    const auto [w, h] = owner_.canvas();
    scene::SceneFrameRequest req{
        .runtime = *rt,
        .mode = scene::SceneMode::Freeform,
        .canvas_width = w,
        .canvas_height = h,
        .slide_index = slide,
        .extras = {},
        .measurer = &owner_.previews_.measurer(),
        .line_index = -1,
        .step = step,
        .animate_step = true,
        .within_slide = step > 0,
    };
    auto frame = scene::build_scene_frame(req);
    const int ms = std::max(frame.content_transition.plays() ? frame.content_transition.duration_ms : 0,
                            frame.builds_end_ms);
    presenter()->present(scene::share_frame(std::move(frame)));
    // The next step once this one has played, as a click would.
    QTimer::singleShot(ms + 700, this, [this, step, run] { play_step(step + 1, run); });
}

// --- fields -------------------------------------------------------------

SlideFieldsSession::SlideFieldsSession(SlideEditSession& owner)
    : QObject(&owner)
    , owner_(owner)
    , list_(this)
{
}

QString SlideFieldsSession::set(const QString& name, const QString& value)
{
    return owner_.apply(owner_.freeform_.set_field(name.toStdString(), value.toStdString()));
}

QString SlideFieldsSession::add(const QString& name)
{
    return owner_.apply(owner_.freeform_.add_field(name.toStdString()));
}

QString SlideFieldsSession::rename(const QString& from, const QString& to)
{
    return owner_.apply(owner_.freeform_.rename_field(from.toStdString(), to.toStdString()));
}

QString SlideFieldsSession::remove(const QString& name)
{
    return owner_.apply(owner_.freeform_.delete_field(name.toStdString()));
}

QString SlideFieldsSession::sanitize(const QString& raw) const
{
    return QString::fromStdString(freeform::sanitize_field_name(raw.toStdString()));
}

QStringList SlideFieldsSession::names() const
{
    QStringList out;
    if (!owner_.freeform_mode()) {
        return out;
    }
    for (const auto& row : owner_.freeform_.field_rows()) {
        out.push_back(QString::fromStdString(row.name));
    }
    return out;
}

int SlideFieldsSession::usage(const QString& name) const
{
    if (!owner_.freeform_mode()) {
        return 0;
    }
    return freeform::count_field_usage(owner_.freeform_.document(), name.toStdString());
}

QStringList SlideFieldsSession::tokenNames() const
{
    QStringList out;
    if (!owner_.text_theme_mode()) {
        return out;
    }
    for (const auto name : layout::k_template_variables) {
        out.push_back(QString::fromUtf8(name));
    }
    return out;
}

// --- theme --------------------------------------------------------------

SlideThemeSession::SlideThemeSession(SlideEditSession& owner)
    : QObject(&owner)
    , owner_(owner)
{
    connect(&owner_.themes_, &ThemeController::assetUploadFinished, this,
            &SlideThemeSession::on_asset_upload_finished);
    connect(&owner_.themes_, &ThemeController::assetDrafted, this,
            [this](qint64 request_id, const QString& url) {
                if (request_id == pending_upload_) {
                    pending_url_ = url;
                }
            });
}

void SlideThemeSession::setTags(const QStringList& tags)
{
    if (!owner_.theme_mode()) {
        return;
    }
    const QStringList normalized = owner_.themes_.normalizeTags(tags);
    if (normalized == owner_.theme_tags_) {
        return;
    }
    owner_.theme_tags_ = normalized;
    owner_.theme_dirty_ = true;
    owner_.document_changed();
}

void SlideThemeSession::setSetting(const QString& key, const QVariant& value)
{
    if (!owner_.theme_mode() || key.isEmpty()) {
        return;
    }
    owner_.theme_style_[key.toStdString()] = to_json(value);
    owner_.theme_dirty_ = true;
    owner_.document_changed();
    repaint_draft();
}

void SlideThemeSession::setContextTransition(const QString& type, int duration_ms,
                                             const QString& direction)
{
    if (!owner_.text_theme_mode()) {
        return;
    }
    model::Transition spec;
    spec.type = model::transition_type_from_string(type.toStdString());
    spec.duration_ms = std::max(0, duration_ms);
    spec.direction = model::transition_direction_from_string(direction.toStdString());
    theme::set_context_transition(owner_.theme_style_,
                                  theme::element_context_from_string(owner_.theme_context_), spec);
    owner_.theme_dirty_ = true;
    owner_.document_changed();
    repaint_draft();
}

void SlideThemeSession::refreshDraft(const QString& suppress_id)
{
    suppress_id_ = suppress_id;
    repaint_draft();
}

void SlideThemeSession::repaint_draft()
{
    if (!owner_.theme_mode()) {
        return;
    }
    owner_.themes_.update_draft_preview(owner_.theme_output_, owner_.theme_kind_,
                                        owner_.context(), owner_.theme_style_, suppress_id_);
}

void SlideThemeSession::uploadAsset(const QString& slot, const QString& local_path)
{
    if (!owner_.theme_mode()) {
        return;
    }
    if (pending_upload_ >= 0) {
        owner_.themes_.drop_draft_upload(pending_upload_);
    }
    pending_slot_ = slot;
    pending_url_.clear();
    // Into the draft only: the saved theme changes when it is saved, and a
    // copy not kept goes when the editor closes.
    pending_upload_ =
        owner_.themes_.upload_asset(owner_.theme_output_, owner_.theme_id_, slot, local_path, true);
}

QString SlideThemeSession::clearAsset(const QString& slot)
{
    if (!owner_.theme_mode()) {
        return refusal::nothing_open();
    }
    if (slot != QLatin1String("bg") && slot != QLatin1String("bg_video")) {
        return refusal::unknown_asset_slot();
    }
    put_asset_slot(slot, {});
    return {};
}

void SlideThemeSession::on_asset_upload_finished(qint64 request_id, const QString& error)
{
    if (request_id != pending_upload_) {
        return;
    }
    pending_upload_ = -1;
    const QString slot = std::exchange(pending_slot_, QString());
    const QString url = std::exchange(pending_url_, QString());
    if (!error.isEmpty()) {
        emit owner_.errorOccurred(error);
        return;
    }
    put_asset_slot(slot, url);
}

void SlideThemeSession::put_asset_slot(const QString& slot, const QString& url)
{
    if (slot != QLatin1String("bg") && slot != QLatin1String("bg_video")) {
        return;
    }
    const bool video = slot == QLatin1String("bg_video");
    owner_.theme_style_[video ? "background_video" : "background_image"] = url.toStdString();
    if (!url.isEmpty()) {
        owner_.theme_style_["background_type"] = video ? "video" : "image";
    }
    owner_.theme_dirty_ = true;
    owner_.document_changed();
    repaint_draft();
}

QString SlideThemeSession::kind() const
{
    return owner_.theme_mode() ? owner_.theme_kind_ : QString();
}

QString SlideThemeSession::themeId() const
{
    return owner_.theme_mode() ? owner_.theme_id_ : QString();
}

QStringList SlideThemeSession::tags() const
{
    return owner_.theme_mode() ? owner_.theme_tags_ : QStringList();
}

QVariantMap SlideThemeSession::settings() const
{
    if (!owner_.theme_mode()) {
        return {};
    }
    return owner_.themes_.style_settings(owner_.theme_style_);
}

QVariantMap SlideThemeSession::settingRanges() const
{
    return to_qvariant(theme::theme_behavior_ranges()).toMap();
}

QVariantList SlideThemeSession::insertKinds() const
{
    QVariantList out;
    if (!owner_.text_theme_mode()) {
        return out;
    }
    const auto ctx = theme::element_context_from_string(owner_.theme_context_);
    const auto has = [&](theme::ElementBinding binding) {
        return std::ranges::any_of(owner_.theme_editor_.current(), [&](const theme::Element& el) {
            return el.binding == binding;
        });
    };
    out.push_back(insert_kind("text", tr("Text Box"), "textBox", "text", "static"));
    if (ctx == theme::ElementContext::Song && !has(theme::ElementBinding::Lyrics)) {
        out.push_back(insert_kind("lyrics", tr("Lyrics Box"), "song", "text", "lyrics"));
    }
    if (ctx == theme::ElementContext::Bible && !has(theme::ElementBinding::BibleText)) {
        out.push_back(insert_kind("bible_text", tr("Bible Box"), "bible", "text", "bible_text"));
    }
    out.push_back(insert_kind("image", tr("Picture"), "image", "image", "static"));
    out.push_back(insert_kind("shape", tr("Shape"), "shape", "shape", "static", "shape"));
    if (ctx == theme::ElementContext::Song) {
        out.push_back(insert_kind("indicator", tr("Indicator"), "indicator", "indicator", "indicator"));
    }
    out.push_back(insert_kind("clock", tr("Clock"), "clock", "clock", "static"));
    if (ctx == theme::ElementContext::Video) {
        out.push_back(insert_kind("countdown", tr("Countdown"), "clock", "clock", "countdown"));
    }
    if ((ctx == theme::ElementContext::Video || ctx == theme::ElementContext::Live)
        && !has(theme::ElementBinding::VideoArea)) {
        out.push_back(insert_kind("video_area", tr("Video Area"), "video", "image", "video_area"));
    }
    if (ctx == theme::ElementContext::Image && !has(theme::ElementBinding::ImageArea)) {
        out.push_back(insert_kind("image_area", tr("Image Area"), "image", "image", "image_area"));
    }
    if (ctx == theme::ElementContext::Web && !has(theme::ElementBinding::WebArea)) {
        out.push_back(insert_kind("web_area", tr("Web Area"), "globe", "image", "web_area"));
    }
    return out;
}

// --- the session --------------------------------------------------------

SlideEditSession::SlideEditSession(const session::Session& session,
                                   PreviewResources& previews,
                                   IFreeformDocumentStore& documents,
                                   ThemeController& themes,
                                   QObject* parent)
    : QObject(parent)
    , session_(session)
    , previews_(previews)
    , documents_(documents)
    , themes_(themes)
    , objects_(std::make_unique<SlideObjectsSession>(*this))
    , slides_(std::make_unique<SlideSlidesSession>(*this))
    , cues_(std::make_unique<SlideCuesSession>(*this))
    , fields_(std::make_unique<SlideFieldsSession>(*this))
    , animations_(std::make_unique<SlideAnimationsSession>(*this))
    , theme_(std::make_unique<SlideThemeSession>(*this))
{
    connect(objects_.get(), &SlideObjectsSession::documentChanged, this,
            &SlideEditSession::emit_document_changed);
    connect(objects_.get(), &SlideObjectsSession::selectionChanged, this,
            &SlideEditSession::selectionChanged);
    connect(objects_.get(), &SlideObjectsSession::clipboardChanged, this,
            &SlideEditSession::clipboardChanged);
    connect(objects_.get(), &SlideObjectsSession::errorOccurred, this,
            &SlideEditSession::errorOccurred);
    // The children's values follow the document.
    connect(this, &SlideEditSession::documentChanged, slides_.get(), &SlideSlidesSession::changed);
    connect(this, &SlideEditSession::documentChanged, cues_.get(), &SlideCuesSession::changed);
    connect(this, &SlideEditSession::documentChanged, fields_.get(), &SlideFieldsSession::changed);
    connect(this, &SlideEditSession::documentChanged, animations_.get(),
            &SlideAnimationsSession::changed);
    // An edit, or another slide picked, stops a preview.
    connect(this, &SlideEditSession::documentChanged, animations_.get(),
            &SlideAnimationsSession::stop);
    connect(this, &SlideEditSession::documentChanged, theme_.get(), &SlideThemeSession::changed);
}

SlideEditSession::~SlideEditSession() = default;

QString SlideEditSession::apply(const freeform::EditResult& result)
{
    return objects_->apply(result);
}

void SlideEditSession::after_edit(bool geometry_only)
{
    if (freeform_mode()) {
        // Before the rows: a thumbnail whose URL changes can paint as they do.
        slides_->preview_build_dirty_ = true;
        bool thumbs_changed = false;
        if (geometry_only) {
            thumbs_changed = slides_->refresh_current_slide();
        } else {
            thumbs_changed = slides_->reload();
            fields_->list_.reload(freeform_);
        }
        if (thumbs_changed) {
            ++slides_->preview_revision_;
        }
    } else if (text_theme_mode()) {
        theme_->repaint_draft();
    }
}

void SlideEditSession::document_changed()
{
    if (freeform_mode()) {
        slides_->reload();
        fields_->list_.reload(freeform_);
    }
    // Through the objects: the stage reads them, and these reach this session.
    objects_->emit_all();
}

void SlideEditSession::emit_document_changed()
{
    ++revision_;
    emit documentChanged();
}

void SlideEditSession::view_moved()
{
    slides_->list_.set_current(freeform_.slide_range_first(), freeform_.slide_range_last(),
                               freeform_.cue_index());
    objects_->emit_all();
}

QString SlideEditSession::copy_background_into(std::string output_id,
                                               std::string theme_id,
                                               std::string slide_id)
{
    if (bg_copier_ == nullptr) {
        return tr("Background themes cannot be copied here.");
    }
    const auto opened = opened_;
    // Set when the copy fails at once (a theme that is gone), for the caller;
    // a copy that lands later reports through errorOccurred alone.
    auto failed = std::make_shared<QString>();
    bg_copier_->copy(output_id, theme_id,
                     [this, opened, slide = std::move(slide_id),
                      failed](std::optional<freeform::BackgroundCopy> copied, QString error) {
                         if (opened != opened_ || !freeform_mode()) {
                             return;  // the editor has moved on
                         }
                         if (copied) {
                             auto result = freeform_.set_background_of(slide, copied->background,
                                                                       copied->transition);
                             if (result) {
                                 (void)apply(result);
                             }
                         }
                         if (!error.isEmpty()) {
                             *failed = error;
                             emit errorOccurred(error);
                         }
                     });
    return *failed;
}

void SlideEditSession::close_document()
{
    ++opened_;
    // Saved or not, the background files the theme does not name go: a
    // picture picked and not saved, or the one a saved pick replaced.
    if (mode_ == SlideEditMode::BackgroundTheme) {
        themes_.prune_assets(theme_output_, theme_id_);
    }
    if (theme_ && theme_->pending_upload_ >= 0) {
        themes_.drop_draft_upload(std::exchange(theme_->pending_upload_, -1));
    }
    if (animations_) {
        animations_->stop();
    }
    freeform_.clear();
    theme_editor_.clear();
    mode_ = SlideEditMode::None;
    item_id_ = -1;
    theme_output_.clear();
    theme_kind_.clear();
    theme_id_.clear();
    theme_name_.clear();
    theme_tags_.clear();
    theme_style_ = nlohmann::json::object();
    theme_dirty_ = false;
    theme_context_ = "song";
    slides_->preview_build_ = {};
    slides_->preview_build_dirty_ = true;
    slides_->list_.reload(freeform_);
    fields_->list_.reload(freeform_);
}

bool SlideEditSession::openFreeform(qint64 freeform_id)
try {
    if (freeform_id < 0) {
        return false;
    }
    auto doc = documents_.load_freeform_document(freeform_id);
    if (!doc) {
        return false;
    }
    close_document();
    const auto* primary = session_.primary_output();
    std::string output = primary != nullptr ? primary->id : std::string{};
    if (doc->decks.empty() && !output.empty()) {
        // A new item with no document yet: give it a deck to edit.
        *doc = freeform::make_document(doc->name, {output});
    } else if (!freeform::find_deck(*doc, output) && !doc->decks.empty()) {
        // The first output that is on and has a deck; the picker lists no other.
        output = doc->decks.front().output_id;
        for (const auto& oc : session_.outputs_on()) {
            if (freeform::find_deck(*doc, oc.id)) {
                output = oc.id;
                break;
            }
        }
    }
    // The editor works in each output's own pixels: lay every deck out on the
    // canvas its output paints before the operator sees it.
    freeform::fit_decks_to_canvases(*doc, build::output_deck_canvases(session_.outputs()));
    freeform_.load(std::move(*doc), output);
    mode_ = SlideEditMode::Freeform;
    item_id_ = freeform_id;
    ++opened_;
    objects_->rebind(freeform_commands_);
    slides_->reload();
    fields_->list_.reload(freeform_);
    slides_->invalidate_preview();
    objects_->emit_all();
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return false;
}

bool SlideEditSession::openTheme(const QString& output_name, const QString& kind,
                                 const QString& theme_id)
try {
    const QVariantMap t = themes_.theme_map(output_name, kind, theme_id);
    if (t.value(QStringLiteral("id")).toString().isEmpty()) {
        if (const QString error = t.value(QStringLiteral("error")).toString(); !error.isEmpty()) {
            emit errorOccurred(error);
        }
        return false;
    }
    close_document();
    mode_ = kind == QLatin1String("bg") ? SlideEditMode::BackgroundTheme
                                        : SlideEditMode::TextTheme;
    theme_output_ = output_name;
    theme_kind_ = kind;
    theme_id_ = theme_id;
    theme_name_ = t.value(QStringLiteral("name")).toString();
    theme_tags_ = themes_.normalizeTags(t.value(QStringLiteral("tags")));
    nlohmann::json style = to_json(t.value(QStringLiteral("style")));
    if (!style.is_object()) {
        style = nlohmann::json::object();
    }
    theme_context_ = "bg";
    if (text_theme_mode()) {
        // A theme opens on its kind's first context (Song, Bible, Picture).
        const auto first = theme::primary_context(text_theme_kind());
        theme_context_ = std::string(theme::to_string(first));
        theme_editor_.load_from_style(style, canvas());
        theme_editor_.set_context(first);
        style.erase(std::string(k_elements));
    }
    theme_style_ = std::move(style);
    objects_->rebind(theme_commands_);
    objects_->emit_all();
    // A theme just opened has no box being typed into.
    theme_->refreshDraft({});
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return false;
}

QString SlideEditSession::setName(const QString& name)
{
    if (freeform_mode()) {
        return apply(freeform_.set_name(name.trimmed().toStdString()));
    }
    if (!theme_mode()) {
        return refusal::nothing_open();
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return refusal::name_required();
    }
    if (trimmed != theme_name_) {
        theme_name_ = trimmed;
        theme_dirty_ = true;
        document_changed();
    }
    return {};
}

void SlideEditSession::setOutput(const QString& name)
{
    if (!freeform_mode()) {
        return;
    }
    freeform_.set_output(output_id_for(name));
    document_changed();
}

void SlideEditSession::setContext(const QString& context)
{
    if (!text_theme_mode()) {
        return;
    }
    // By the context's own name; one this theme's kind does not hold is not
    // there to switch to.
    const auto ctx = theme::element_context_from_string(context.toStdString());
    if (theme::kind_of(ctx) != text_theme_kind()) {
        return;
    }
    theme_context_ = std::string(theme::to_string(ctx));
    theme_editor_.set_context(ctx);
    objects_->emit_all();
    theme_->repaint_draft();
}

QString SlideEditSession::save()
try {
    if (freeform_mode()) {
        if (item_id_ < 0) {
            return tr("There is nothing to save: this is not a Freeform item of the library.");
        }
        freeform_.prune_on_save();
        // The document store owns what saving entails beyond the document
        // itself: the library row and its thumbnails, the listing, and
        // re-sending the item to the outputs when it is the one live.
        const QString error = documents_.commit_freeform_document(item_id_, freeform_.document());
        if (!error.isEmpty()) {
            return error;
        }
        cancel();
        return {};
    }
    if (theme_mode()) {
        // The controller folds the elements in (apply_elements) before it
        // saves, as it does for the draft.
        const QString error =
            themes_.update_theme(theme_output_, theme_kind_, theme_id_, theme_name_, theme_style_,
                                 theme_tags_.join(QStringLiteral(", ")), true);
        if (!error.isEmpty()) {
            return error;
        }
        cancel();
        return {};
    }
    return refusal::nothing_open();
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

void SlideEditSession::cancel()
{
    close_document();
    objects_->emit_all();
}

QString SlideEditSession::undo()
{
    return objects_->undo();
}

QString SlideEditSession::redo()
{
    return objects_->redo();
}

QString SlideEditSession::mode() const
{
    switch (mode_) {
    case SlideEditMode::Freeform:
        return QStringLiteral("freeform");
    case SlideEditMode::TextTheme:
        return QStringLiteral("text");
    case SlideEditMode::BackgroundTheme:
        return QStringLiteral("bg");
    case SlideEditMode::None:
        break;
    }
    return {};
}

bool SlideEditSession::active() const
{
    return mode_ != SlideEditMode::None;
}

bool SlideEditSession::dirty() const
{
    if (freeform_mode()) {
        return freeform_.dirty();
    }
    if (text_theme_mode()) {
        return theme_dirty_ || theme_editor_.dirty();
    }
    return theme_mode() && theme_dirty_;
}

bool SlideEditSession::canUndo() const
{
    return freeform_mode() ? freeform_.can_undo() : text_theme_mode() && theme_editor_.can_undo();
}

bool SlideEditSession::canRedo() const
{
    return freeform_mode() ? freeform_.can_redo() : text_theme_mode() && theme_editor_.can_redo();
}

QString SlideEditSession::name() const
{
    if (freeform_mode()) {
        return QString::fromStdString(freeform_.document().name);
    }
    return theme_mode() ? theme_name_ : QString();
}

geom::CanvasSize SlideEditSession::canvas() const
{
    if (freeform_mode()) {
        return model::output_canvas(session_.outputs(), freeform_.output_id());
    }
    const auto* oc =
        theme_mode() ? model::find_output_by_name(session_.outputs(), theme_output_.toStdString())
                     : nullptr;
    return oc != nullptr ? model::output_canvas(*oc) : geom::k_reference_canvas;
}

int SlideEditSession::canvasWidth() const
{
    return canvas().width;
}

int SlideEditSession::canvasHeight() const
{
    return canvas().height;
}

QString SlideEditSession::output() const
{
    if (freeform_mode()) {
        return output_name_for(freeform_.output_id());
    }
    return theme_mode() ? theme_output_ : QString();
}

QString SlideEditSession::context() const
{
    return theme_mode() ? QString::fromStdString(theme_context_) : QString();
}

theme::ThemeKind SlideEditSession::text_theme_kind() const noexcept
{
    // openTheme took the kind through ThemeController, which knows only real
    // ones; a text theme's is one of the three text kinds.
    return theme::parse_theme_kind(theme_kind_.toStdString()).value_or(theme::ThemeKind::Song);
}

QStringList SlideEditSession::contexts() const
{
    if (text_theme_mode()) {
        QStringList out;
        for (const auto ctx : theme::contexts_of(text_theme_kind())) {
            out.push_back(QString::fromUtf8(theme::to_string(ctx)));
        }
        return out;
    }
    if (theme_mode()) {
        return {QStringLiteral("bg")};
    }
    return {};
}

void SlideEditSession::apply_elements(nlohmann::json& style) const
{
    if (!text_theme_mode() || !theme_editor_.active()) {
        return;
    }
    if (!style.is_object()) {
        style = nlohmann::json::object();
    }
    style[std::string(k_elements)] = theme_editor_.set().to_json();
}

void SlideEditSession::on_theme_library_changed()
{
    if (!freeform_mode()) {
        return;
    }
    slides_->reload();
    slides_->preview_build_dirty_ = true;
}

std::string SlideEditSession::output_id_for(const QString& name) const
{
    const auto* oc = model::find_output_by_name(session_.outputs(), name.toStdString());
    return oc != nullptr ? oc->id : std::string{};
}

QString SlideEditSession::output_name_for(std::string_view output_id) const
{
    const auto* oc = model::find_output_by_id(session_.outputs(), output_id);
    return oc != nullptr ? QString::fromStdString(oc->name) : QString();
}

}  // namespace seventhslide::app
