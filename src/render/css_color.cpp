#include "seventhslide/render/css_color.hpp"

#include <seventhslide/util/strings.hpp>

#include <QString>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace seventhslide::render {
namespace {

/// A CSS number, or a percentage of ``full``.
[[nodiscard]] std::optional<double> parse_number(std::string_view token, double full)
{
    token = util::trim_view(token);
    if (token.empty()) {
        return std::nullopt;
    }
    const bool percent = token.back() == '%';
    if (percent) {
        token.remove_suffix(1);
        token = util::trim_view(token);
    }
    bool ok = false;
    const double v = QString::fromUtf8(token).toDouble(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return percent ? v * full / 100.0 : v;
}

/// One ``rgb()`` component: ``0-255``, or ``0-100%`` scaled onto 0-255.
[[nodiscard]] bool parse_channel(std::string_view token, int& out)
{
    const auto v = parse_number(token, 255.0);
    if (!v) {
        return false;
    }
    out = std::clamp(static_cast<int>(std::lround(*v)), 0, 255);
    return true;
}

/// Alpha: ``0-1`` (CSS) or ``0-100%``.
[[nodiscard]] bool parse_alpha(std::string_view token, int& out)
{
    const auto v = parse_number(token, 1.0);
    if (!v) {
        return false;
    }
    out = std::clamp(static_cast<int>(std::lround(*v * 255.0)), 0, 255);
    return true;
}

/// ``rgb(...)`` / ``rgba(...)``. Accepts comma or whitespace separators and the
/// modern ``r g b / a`` slash form.
[[nodiscard]] QColor parse_functional(std::string_view s)
{
    const bool rgba = util::starts_with_ci(s, "rgba(");
    const bool rgb = !rgba && util::starts_with_ci(s, "rgb(");
    if (!rgba && !rgb) {
        return {};
    }
    const auto open = s.find('(');
    if (s.back() != ')') {
        return {};
    }
    std::string_view body = s.substr(open + 1, s.size() - open - 2);

    std::string_view alpha_token;
    if (const auto slash = body.find('/'); slash != std::string_view::npos) {
        alpha_token = body.substr(slash + 1);
        body = body.substr(0, slash);
    }

    // Split on commas, falling back to whitespace for the modern syntax.
    std::vector<std::string_view> parts;
    const bool comma = body.find(',') != std::string_view::npos;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= body.size(); ++i) {
        const bool sep = i == body.size()
            || (comma ? body[i] == ',' : (body[i] == ' ' || body[i] == '\t'));
        if (!sep) {
            continue;
        }
        const auto token = util::trim_view(body.substr(start, i - start));
        if (!token.empty()) {
            parts.push_back(token);
        }
        start = i + 1;
    }
    if (parts.size() == 4 && alpha_token.empty()) {
        alpha_token = parts[3];
    }
    if (parts.size() < 3) {
        return {};
    }

    int r = 0;
    int g = 0;
    int b = 0;
    if (!parse_channel(parts[0], r) || !parse_channel(parts[1], g)
        || !parse_channel(parts[2], b)) {
        return {};
    }
    int a = 255;
    if (!alpha_token.empty() && !parse_alpha(alpha_token, a)) {
        return {};
    }
    return QColor(r, g, b, a);
}

} // namespace

QColor css_color(std::string_view css)
{
    const std::string_view s = util::trim_view(css);
    if (s.empty() || s == "none") {
        return {};
    }
    if (const QColor fn = parse_functional(s); fn.isValid()) {
        return fn;
    }
    const QColor c(QString::fromUtf8(s));
    return c.isValid() ? c : QColor();
}

QColor css_color_or(std::string_view css, const QColor& fallback)
{
    const QColor c = css_color(css);
    return c.isValid() ? c : fallback;
}

std::string css_color_hex(std::string_view css)
{
    const QColor c = css_color(css);
    if (!c.isValid()) {
        return {};
    }
    const QString hex = c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
    return hex.toStdString();
}

} // namespace seventhslide::render
