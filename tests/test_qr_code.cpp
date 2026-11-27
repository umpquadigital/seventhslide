// The web remote's QR code: the SVG draws exactly the encoder's modules, in
// the quiet zone, dark on white.

#include "qr_code.hpp"

#include <catch2/catch_test_macros.hpp>

#include <qrcodegen.hpp>

#include <QRegularExpression>
#include <QString>

#include <set>
#include <utility>

using seventhslide::app::QrCode;
namespace qr_code = seventhslide::app::qr_code;

namespace {

/// The dark cells an SVG from qr_code::svg paints, read back from its path.
std::set<std::pair<int, int>> painted_cells(const QString& svg)
{
    std::set<std::pair<int, int>> cells;
    static const QRegularExpression run(QStringLiteral(R"(M(\d+) (\d+)h(\d+)v1h-\d+z)"));
    for (auto it = run.globalMatch(svg); it.hasNext();) {
        const auto m = it.next();
        const int x = m.captured(1).toInt();
        const int y = m.captured(2).toInt();
        for (int i = 0; i < m.captured(3).toInt(); ++i) {
            cells.emplace(x + i, y);
        }
    }
    return cells;
}

}  // namespace

TEST_CASE("A QR code's SVG paints exactly the encoder's modules", "[app][qr]")
{
    const QString url = QStringLiteral("http://10.0.5.105:8765/");
    const QString svg = qr_code::svg(url);
    REQUIRE_FALSE(svg.isEmpty());

    const auto code = qrcodegen::QrCode::encodeText(url.toUtf8().constData(),
                                                    qrcodegen::QrCode::Ecc::MEDIUM);
    const int size = code.getSize();
    const int q = qr_code::k_quiet_zone;
    std::set<std::pair<int, int>> expected;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (code.getModule(x, y)) {
                expected.emplace(x + q, y + q);
            }
        }
    }
    CHECK(painted_cells(svg) == expected);

    // The symbol sits in a white square with the quiet zone all round.
    const int extent = size + 2 * q;
    CHECK(qr_code::extent(url) == extent);
    CHECK(svg.contains(QStringLiteral("viewBox=\"0 0 %1 %1\"").arg(extent)));
    CHECK(svg.contains(QStringLiteral("<rect width=\"%1\" height=\"%1\" fill=\"#ffffff\"/>")
                           .arg(extent)));
}

TEST_CASE("A QR code is an icon-provider URL, or nothing for nothing", "[app][qr]")
{
    QrCode qr;
    CHECK(qr.url(QStringLiteral("http://192.168.1.20:8765/")).startsWith(QStringLiteral("image://ssicon/")));
    CHECK(qr.url(QString()).isEmpty());
    CHECK(qr.extent(QString()) == 0);
    // More than any QR version holds.
    CHECK(qr_code::svg(QString(8000, QLatin1Char('x'))).isEmpty());
}
