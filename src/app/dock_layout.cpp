#include "dock_layout.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <unordered_set>
#include <utility>

namespace seventhslide::app {

namespace {

using Node = DockState::Node;
using Axis = DockState::Axis;
using Edge = DockState::Edge;

/// Every panel, in the order defaults() places them.
constexpr std::string_view k_panels[] = {"service", "library", "outputs", "mixer"};
constexpr int k_default_left_width = 360;
constexpr int k_default_right_width = 290;

QString settings_key()
{
    return QString::fromUtf8(DockLayout::k_settings_key);
}

QStringList to_qt(const std::vector<std::string>& ids)
{
    QStringList out;
    for (const auto& id : ids) {
        out.push_back(QString::fromStdString(id));
    }
    return out;
}

Node leaf(std::string_view id, double weight = 1.0)
{
    Node n;
    n.id = std::string(id);
    n.weight = weight;
    return n;
}

Node split(Axis axis, std::vector<Node> children, int extent = 0)
{
    Node n;
    n.axis = axis;
    n.children = std::move(children);
    n.extent = extent;
    return n;
}

bool contains(const Node& n, std::string_view id)
{
    if (n.is_leaf()) {
        return n.id == id;
    }
    return std::ranges::any_of(n.children, [id](const Node& c) { return contains(c, id); });
}

/// It takes room: a leaf shown, or a split with one.
bool visible(const Node& n)
{
    if (n.is_leaf()) {
        return n.shown;
    }
    return std::ranges::any_of(n.children, visible);
}

bool folded(const Node& n)
{
    return n.is_leaf() && n.collapsed;
}

/// ``N`` is Node or const Node.
template <class N>
N* find_leaf(N& n, std::string_view id)
{
    if (n.is_leaf()) {
        return n.id == id ? &n : nullptr;
    }
    for (auto& c : n.children) {
        if (N* found = find_leaf(c, id)) {
            return found;
        }
    }
    return nullptr;
}

/// The split holding leaf ``id`` and where in it, or null where ``id`` is the
/// root or nowhere.
std::pair<Node*, std::size_t> find_parent(Node& n, std::string_view id)
{
    if (n.is_leaf()) {
        return {nullptr, 0};
    }
    for (std::size_t i = 0; i < n.children.size(); ++i) {
        if (n.children[i].is_leaf() && n.children[i].id == id) {
            return {&n, i};
        }
        if (auto found = find_parent(n.children[i], id); found.first) {
            return found;
        }
    }
    return {nullptr, 0};
}

void remove_leaf(Node& n, std::string_view id)
{
    if (n.is_leaf()) {
        return;
    }
    std::erase_if(n.children, [id](const Node& c) { return c.is_leaf() && c.id == id; });
    for (auto& c : n.children) {
        remove_leaf(c, id);
    }
}

/// Splits put tidy: an empty one goes, one with a single child is that
/// child, and one going the same way as its parent gives its children to it.
void normalize(Node& n)
{
    if (n.is_leaf()) {
        return;
    }
    std::vector<Node> kids;
    for (auto& c : n.children) {
        normalize(c);
        if (!c.is_leaf() && c.children.empty()) {
            continue;
        }
        if (!c.is_leaf() && c.children.size() == 1) {
            Node only = std::move(c.children.front());
            only.weight = c.weight;
            only.extent = c.extent;
            c = std::move(only);
        }
        if (!c.is_leaf() && c.axis == n.axis) {
            double sum = 0;
            for (const auto& g : c.children) {
                sum += g.weight;
            }
            for (auto& g : c.children) {
                if (sum > 0) {
                    // Between them they keep the length the split had.
                    if (c.extent > 0) {
                        g.extent = std::max(1, static_cast<int>(std::lround(c.extent * g.weight / sum)));
                    }
                    g.weight = g.weight * c.weight / sum;
                }
                kids.push_back(std::move(g));
            }
            continue;
        }
        kids.push_back(std::move(c));
    }
    n.children = std::move(kids);
}

void normalize_root(Node& root)
{
    normalize(root);
    if (!root.is_leaf() && root.children.size() == 1) {
        Node only = std::move(root.children.front());
        only.weight = 1.0;
        only.extent = 0;
        root = std::move(only);
    }
}

/// Put ``node`` at ``edge`` of leaf ``target``, or of the whole tree.
void insert(Node& root, Node node, std::string_view target, Edge edge)
{
    const Axis axis = edge == Edge::Left || edge == Edge::Right ? Axis::Row : Axis::Column;
    const bool after = edge == Edge::Right || edge == Edge::Bottom;
    // ``at`` and ``node`` become a split of their own, in ``at``'s place.
    const auto wrap = [&](Node& at) {
        Node old = std::move(at);
        Node s = split(axis, {}, old.extent);
        s.weight = old.weight;
        old.weight = 1.0;
        old.extent = 0;
        if (after) {
            s.children.push_back(std::move(old));
            s.children.push_back(std::move(node));
        } else {
            s.children.push_back(std::move(node));
            s.children.push_back(std::move(old));
        }
        at = std::move(s);
    };
    if (target.empty()) {
        if (!root.is_leaf() && root.axis == axis) {
            root.children.insert(after ? root.children.end() : root.children.begin(),
                                 std::move(node));
        } else {
            wrap(root);
        }
        return;
    }
    const auto [parent, index] = find_parent(root, target);
    if (parent == nullptr) {
        wrap(root);
    } else if (parent->axis == axis) {
        // Beside its new neighbour, sharing as it does.
        node.weight = parent->children[index].weight;
        const auto at = parent->children.begin() + static_cast<std::ptrdiff_t>(index + (after ? 1 : 0));
        parent->children.insert(at, std::move(node));
    } else {
        wrap(parent->children[index]);
    }
}

bool same_places(const Node& a, const Node& b)
{
    if (a.id != b.id || a.shown != b.shown || a.collapsed != b.collapsed
        || a.children.size() != b.children.size()) {
        return false;
    }
    if (!a.is_leaf() && a.axis != b.axis) {
        return false;
    }
    for (std::size_t i = 0; i < a.children.size(); ++i) {
        if (!same_places(a.children[i], b.children[i])) {
            return false;
        }
    }
    return true;
}

void describe(const Node& n, std::string& out)
{
    if (n.is_leaf()) {
        out += n.id;
        if (!n.shown) {
            out += ":hidden";
        } else if (n.collapsed) {
            out += ":collapsed";
        }
        return;
    }
    out += n.axis == Axis::Row ? "row(" : "column(";
    for (std::size_t i = 0; i < n.children.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        describe(n.children[i], out);
    }
    out += ')';
}

template <class N>
N* node_at(N& root, std::string_view path)
{
    N* n = &root;
    while (!path.empty()) {
        const auto dot = path.find('.');
        const auto part = path.substr(0, dot);
        std::size_t index = 0;
        const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), index);
        if (ec != std::errc{} || end != part.data() + part.size() || n->is_leaf()
            || index >= n->children.size()) {
            return nullptr;
        }
        n = &n->children[index];
        path = dot == std::string_view::npos ? std::string_view{} : path.substr(dot + 1);
    }
    return n;
}

std::string child_path(const std::string& path, std::size_t index)
{
    return path.empty() ? std::to_string(index) : path + '.' + std::to_string(index);
}

double read_weight(const nlohmann::json& entry, double fallback)
{
    if (const auto w = entry.find("weight"); w != entry.end() && w->is_number()) {
        const double weight = w->get<double>();
        if (std::isfinite(weight) && weight > 0) {
            return weight;
        }
    }
    return fallback;
}

bool read_bool(const nlohmann::json& entry, const char* key, bool fallback)
{
    const auto it = entry.find(key);
    return it != entry.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

bool known_leaf(std::string_view id)
{
    return id == DockState::k_controller || std::ranges::find(k_panels, id) != std::end(k_panels);
}

/// The node ``j`` says, its unknown and repeated leaves dropped; nothing
/// where nothing is left.
std::optional<Node> read_node(const nlohmann::json& j, std::unordered_set<std::string>& seen)
{
    if (!j.is_object()) {
        return std::nullopt;
    }
    Node n;
    n.weight = read_weight(j, 1.0);
    if (const auto e = j.find("extent"); e != j.end() && e->is_number_integer()) {
        n.extent = std::clamp(e->get<int>(), 0, DockState::k_max_extent);
    }
    if (const auto id = j.find("id"); id != j.end()) {
        if (!id->is_string()) {
            return std::nullopt;
        }
        n.id = id->get<std::string>();
        if (n.id.empty() || !known_leaf(n.id) || !seen.insert(n.id).second) {
            return std::nullopt;
        }
        if (n.id != DockState::k_controller) {
            n.shown = read_bool(j, "shown", true);
            n.collapsed = read_bool(j, "collapsed", false);
        }
        return n;
    }
    const auto children = j.find("children");
    if (children == j.end() || !children->is_array()) {
        return std::nullopt;
    }
    if (const auto axis = j.find("axis"); axis != j.end() && axis->is_string()) {
        n.axis = axis->get<std::string>() == "column" ? Axis::Column : Axis::Row;
    }
    for (const auto& c : *children) {
        if (auto child = read_node(c, seen)) {
            n.children.push_back(std::move(*child));
        }
    }
    if (n.children.empty()) {
        return std::nullopt;
    }
    return n;
}

nlohmann::json node_json(const Node& n)
{
    nlohmann::json j = {{"weight", n.weight}, {"extent", n.extent}};
    if (n.is_leaf()) {
        j["id"] = n.id;
        j["shown"] = n.shown;
        j["collapsed"] = n.collapsed;
        return j;
    }
    j["axis"] = n.axis == Axis::Row ? "row" : "column";
    auto children = nlohmann::json::array();
    for (const auto& c : n.children) {
        children.push_back(node_json(c));
    }
    j["children"] = std::move(children);
    return j;
}

/// Lays ``n`` out in ``r``, and what is inside it.
void lay(const Node& n, DockState::Rect r, bool in_row, const std::string& path,
         const DockState::Metrics& m, DockState::Geometry& out)
{
    if (n.is_leaf()) {
        out.places.push_back({n.id, r, n.collapsed && in_row});
        return;
    }
    std::vector<std::size_t> vis;
    for (std::size_t i = 0; i < n.children.size(); ++i) {
        if (visible(n.children[i])) {
            vis.push_back(i);
        }
    }
    if (vis.empty()) {
        return;
    }
    const bool row = n.axis == Axis::Row;
    const int count = static_cast<int>(vis.size());
    const int len = std::max(0, (row ? r.width : r.height) - (count - 1) * m.line);
    const int controller_min = row ? m.controller_min_width : m.controller_min_height;
    const int panel_min = row ? m.panel_min_width : m.panel_min_height;

    std::vector<int> size(vis.size(), 0);
    int flex = -1;
    int folded_total = 0;
    for (int k = 0; k < count; ++k) {
        const Node& c = n.children[vis[k]];
        if (contains(c, DockState::k_controller)) {
            flex = k;
        }
        if (folded(c)) {
            size[k] = m.header;
            folded_total += m.header;
        }
    }
    if (flex >= 0) {
        // Beside the Controller each keeps its own length, and the
        // Controller takes the rest; short of room, they give way alike.
        int fixed = 0;
        for (int k = 0; k < count; ++k) {
            const Node& c = n.children[vis[k]];
            if (k == flex || folded(c)) {
                continue;
            }
            size[k] = c.extent > 0 ? c.extent : (row ? m.default_width : m.default_height);
            fixed += size[k];
        }
        const int room = std::max(0, len - folded_total - controller_min);
        if (fixed > room && fixed > 0) {
            const double scale = static_cast<double>(room) / fixed;
            for (int k = 0; k < count; ++k) {
                if (k != flex && !folded(n.children[vis[k]])) {
                    size[k] = static_cast<int>(std::floor(size[k] * scale));
                }
            }
        }
        int used = 0;
        for (int k = 0; k < count; ++k) {
            if (k != flex) {
                used += size[k];
            }
        }
        size[flex] = std::max(0, len - used);
    } else {
        // Shared by weight, the last open one taking what rounding leaves.
        // With none open, the bars sit at the start.
        double total = 0;
        int last_open = -1;
        for (int k = 0; k < count; ++k) {
            const Node& c = n.children[vis[k]];
            if (!folded(c)) {
                total += c.weight;
                last_open = k;
            }
        }
        const int free = std::max(0, len - folded_total);
        int given = 0;
        for (int k = 0; k < count; ++k) {
            const Node& c = n.children[vis[k]];
            if (folded(c)) {
                continue;
            }
            if (k == last_open) {
                size[k] = free - given;
            } else {
                size[k] = static_cast<int>(std::lround(free * c.weight / total));
                given += size[k];
            }
        }
    }

    int pos = row ? r.x : r.y;
    for (int k = 0; k < count; ++k) {
        const Node& c = n.children[vis[k]];
        const DockState::Rect cr = row ? DockState::Rect{pos, r.y, size[k], r.height}
                                       : DockState::Rect{r.x, pos, r.width, size[k]};
        lay(c, cr, row, child_path(path, vis[k]), m, out);
        const int start = pos;
        pos += size[k];
        if (k + 1 >= count) {
            break;
        }
        const Node& next = n.children[vis[k + 1]];
        DockState::Geometry::Line line;
        line.path = path;
        line.before = static_cast<int>(vis[k]);
        line.after = static_cast<int>(vis[k + 1]);
        line.vertical = row;
        line.rect = row ? DockState::Rect{pos, r.y, m.line, r.height}
                        : DockState::Rect{r.x, pos, r.width, m.line};
        line.start = start;
        line.total = size[k] + size[k + 1];
        line.min = k == flex ? controller_min : panel_min;
        line.max = line.total - (k + 1 == flex ? controller_min : panel_min);
        line.resizable = !folded(c) && !folded(next) && line.max >= line.min;
        out.lines.push_back(std::move(line));
        pos += m.line;
    }
}

Edge edge_from(const QString& name)
{
    if (name == QStringLiteral("right")) {
        return Edge::Right;
    }
    if (name == QStringLiteral("top")) {
        return Edge::Top;
    }
    if (name == QStringLiteral("bottom")) {
        return Edge::Bottom;
    }
    return Edge::Left;
}

QString line_key(const DockState::Geometry::Line& line)
{
    return QStringLiteral("%1:%2:%3")
        .arg(QString::fromStdString(line.path))
        .arg(line.before)
        .arg(line.after);
}

QVariantMap rect_map(const DockState::Rect& r)
{
    return {{QStringLiteral("x"), r.x},
            {QStringLiteral("y"), r.y},
            {QStringLiteral("width"), r.width},
            {QStringLiteral("height"), r.height}};
}

}  // namespace

const DockState::Geometry::Place* DockState::Geometry::find(std::string_view id) const
{
    const auto it = std::ranges::find(places, id, &Place::id);
    return it == places.end() ? nullptr : &*it;
}

DockState DockState::defaults()
{
    // The Library is a little taller than the Service above it: it holds
    // more; the Outputs' previews keep more than the Audio Mixer.
    DockState s;
    s.root = split(Axis::Row,
                   {split(Axis::Column, {leaf("service", 1.0), leaf("library", 1.25)},
                          k_default_left_width),
                    leaf(k_controller),
                    split(Axis::Column, {leaf("outputs", 1.0), leaf("mixer", 0.8)},
                          k_default_right_width)});
    return s;
}

const std::vector<std::string>& DockState::panel_ids()
{
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> out;
        for (const auto id : k_panels) {
            out.emplace_back(id);
        }
        return out;
    }();
    return ids;
}

DockState DockState::from_json(const nlohmann::json& j)
{
    if (!j.is_object()) {
        return defaults();
    }
    const auto saved = j.find("root");
    if (saved == j.end()) {
        return defaults();
    }
    std::unordered_set<std::string> seen;
    auto root = read_node(*saved, seen);
    if (!root || !contains(*root, k_controller)) {
        return defaults();
    }
    DockState s;
    s.root = std::move(*root);
    // A panel the saved layout does not know (one added since) goes along
    // the right edge.
    for (const auto& id : panel_ids()) {
        if (!seen.contains(id)) {
            insert(s.root, leaf(id), "", Edge::Right);
        }
    }
    normalize_root(s.root);
    return s;
}

nlohmann::json DockState::to_json() const
{
    return {{"root", node_json(root)}};
}

const DockState::Node* DockState::find(std::string_view id) const
{
    return find_leaf(root, id);
}

std::string DockState::describe() const
{
    std::string out;
    app::describe(root, out);
    return out;
}

bool DockState::same_places(const DockState& other) const
{
    return app::same_places(root, other.root);
}

bool DockState::move(std::string_view id, std::string_view target, Edge edge)
{
    if (id == k_controller || id == target) {
        return false;
    }
    const Node* moving_now = find(id);
    if (moving_now == nullptr) {
        return false;
    }
    if (!target.empty()) {
        const Node* t = find(target);
        if (t == nullptr || !t->shown) {
            return false;
        }
    }
    const DockState was = *this;
    Node moving = *moving_now;
    remove_leaf(root, id);
    normalize_root(root);
    moving.shown = true;
    moving.weight = 1.0;
    moving.extent = 0;
    insert(root, std::move(moving), target, edge);
    normalize_root(root);
    if (same_places(was)) {
        *this = was;
        return false;
    }
    return true;
}

bool DockState::set_shown(std::string_view id, bool shown)
{
    Node* n = find_leaf(root, id);
    if (n == nullptr || id == k_controller || n->shown == shown) {
        return false;
    }
    n->shown = shown;
    return true;
}

bool DockState::set_collapsed(std::string_view id, bool collapsed)
{
    Node* n = find_leaf(root, id);
    if (n == nullptr || id == k_controller || n->collapsed == collapsed) {
        return false;
    }
    n->collapsed = collapsed;
    return true;
}

bool DockState::resize_line(std::string_view path, int before, int after, int before_px,
                            int total_px)
{
    Node* n = node_at(root, path);
    if (n == nullptr || n->is_leaf() || before < 0 || after <= before
        || after >= static_cast<int>(n->children.size()) || total_px <= 0) {
        return false;
    }
    Node& a = n->children[static_cast<std::size_t>(before)];
    Node& b = n->children[static_cast<std::size_t>(after)];
    if (folded(a) || folded(b)) {
        return false;
    }
    const DockState was = *this;
    const int px = std::clamp(before_px, 0, total_px);
    const auto extent = [](int v) { return std::clamp(v, 1, k_max_extent); };
    const bool a_flex = contains(a, k_controller);
    const bool b_flex = contains(b, k_controller);
    const bool beside_controller = contains(*n, k_controller);
    if (beside_controller) {
        if (!a_flex) {
            a.extent = extent(px);
        }
        if (!b_flex) {
            b.extent = extent(total_px - px);
        }
    } else {
        const double total = a.weight + b.weight;
        const double f = std::clamp(static_cast<double>(px) / total_px, 0.05, 0.95);
        a.weight = total * f;
        b.weight = total - a.weight;
    }
    return *this != was;
}

DockState::Geometry DockState::layout(int width, int height, const Metrics& metrics) const
{
    Geometry out;
    lay(root, {0, 0, std::max(0, width), std::max(0, height)}, false, "", metrics, out);
    return out;
}

// ---------------------------------------------------------------- DockLayout

DockLayout::DockLayout(QObject* parent)
    : QObject(parent)
    , state_(DockState::defaults())
{
    const QByteArray saved = settings_.value(settings_key()).toByteArray();
    if (!saved.isEmpty()) {
        const auto j = nlohmann::json::parse(saved.toStdString(), nullptr, false);
        state_ = DockState::from_json(j);
    }
}

QStringList DockLayout::hidden() const
{
    std::vector<std::string> out;
    for (const auto& id : DockState::panel_ids()) {
        if (const auto* n = state_.find(id); n && !n->shown) {
            out.push_back(id);
        }
    }
    return to_qt(out);
}

QStringList DockLayout::collapsed() const
{
    std::vector<std::string> out;
    for (const auto& id : DockState::panel_ids()) {
        if (const auto* n = state_.find(id); n && n->collapsed) {
            out.push_back(id);
        }
    }
    return to_qt(out);
}

QVariantList DockLayout::lines() const
{
    QVariantList out;
    for (const auto& line : state_.layout(4000, 4000, {}).lines) {
        out.push_back(QVariantMap{{QStringLiteral("key"), line_key(line)},
                                  {QStringLiteral("path"), QString::fromStdString(line.path)},
                                  {QStringLiteral("before"), line.before},
                                  {QStringLiteral("after"), line.after},
                                  {QStringLiteral("vertical"), line.vertical}});
    }
    return out;
}

QVariantMap DockLayout::geometry(int width, int height, int header, int controllerWidth) const
{
    DockState::Metrics metrics;
    metrics.header = header;
    metrics.controller_min_width = std::max(metrics.controller_min_width, controllerWidth);
    const auto g = state_.layout(width, height, metrics);
    QVariantMap places;
    for (const auto& p : g.places) {
        QVariantMap place = rect_map(p.rect);
        place.insert(QStringLiteral("strip"), p.strip);
        places.insert(QString::fromStdString(p.id), place);
    }
    QVariantMap lines;
    for (const auto& line : g.lines) {
        QVariantMap l = rect_map(line.rect);
        l.insert(QStringLiteral("resizable"), line.resizable);
        l.insert(QStringLiteral("start"), line.start);
        l.insert(QStringLiteral("total"), line.total);
        l.insert(QStringLiteral("min"), line.min);
        l.insert(QStringLiteral("max"), line.max);
        lines.insert(line_key(line), l);
    }
    return {{QStringLiteral("places"), places}, {QStringLiteral("lines"), lines}};
}

void DockLayout::setEditMode(bool on)
{
    if (edit_mode_ == on) {
        return;
    }
    edit_mode_ = on;
    emit editModeChanged();
}

void DockLayout::movePanel(const QString& id, const QString& target, const QString& edge)
{
    DockState next = state_;
    if (next.move(id.toStdString(), target.toStdString(), edge_from(edge))) {
        apply(next);
    }
}

void DockLayout::setShown(const QString& id, bool shown)
{
    DockState next = state_;
    if (next.set_shown(id.toStdString(), shown)) {
        apply(next);
    }
}

void DockLayout::setCollapsed(const QString& id, bool collapsed)
{
    DockState next = state_;
    if (next.set_collapsed(id.toStdString(), collapsed)) {
        apply(next);
    }
}

void DockLayout::resizeLine(const QString& path, int before, int after, int beforePx,
                            int totalPx)
{
    DockState next = state_;
    if (next.resize_line(path.toStdString(), before, after, beforePx, totalPx)) {
        apply(next);
    }
}

void DockLayout::reset()
{
    apply(DockState::defaults());
}

void DockLayout::apply(const DockState& next)
{
    if (next == state_) {
        return;
    }
    const bool places = !state_.same_places(next);
    state_ = next;
    ++revision_;
    settings_.setValue(settings_key(), QByteArray::fromStdString(state_.to_json().dump()));
    if (places) {
        emit placesChanged();
    } else {
        emit sizesChanged();
    }
    emit changed();
}

} // namespace seventhslide::app
