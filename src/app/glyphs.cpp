#include "glyphs.hpp"

#include "icon_image_provider.hpp"

#include <QColor>
#include <QUrl>

#include <algorithm>
#include <iterator>

namespace seventhslide::app {
namespace glyphs {
namespace {

using enum Style;

// The glyphs, by name. Bodies are SVG on a 24x24 viewBox unless a glyph gives
// its own; a raw glyph's {ink} is the colour it is drawn in.
constexpr Glyph k_glyphs[] = {
    // --- the slide editor's ribbon ---
    {"undo", Stroke, "", R"svg(<path d="M9 14 4 9l5-5"/><path d="M4 9h11a5 5 0 0 1 0 10h-3"/>)svg"},
    {"redo", Stroke, "", R"svg(<path d="m15 14 5-5-5-5"/><path d="M20 9H9a5 5 0 0 0 0 10h3"/>)svg"},
    {"braces", Stroke, "",
     R"svg(<path d="M8 4c-2 0-3 1-3 3v3c0 1-1 2-2 2 1 0 2 1 2 2v3c0 2 1 3 3 3M16 4c2 0 3 1 3 3v3c0 1 1 2 2 2-1 0-2 1-2 2v3c0 2-1 3-3 3"/>)svg"},
    {"sidebar", Stroke, "", R"svg(<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M15 4v16"/>)svg"},
    {"newSlide", Stroke, "", R"svg(<rect x="3" y="5" width="18" height="12" rx="2"/><path d="M12 8v6M9 11h6"/>)svg"},
    {"fit", Stroke, "",
     R"svg(<path d="M4 9V5a1 1 0 0 1 1-1h4M15 4h4a1 1 0 0 1 1 1v4M20 15v4a1 1 0 0 1-1 1h-4M9 20H5a1 1 0 0 1-1-1v-4"/>)svg"},
    {"bullets", Stroke, "",
     R"svg(<circle cx="5" cy="7" r="1"/><circle cx="5" cy="12" r="1"/><circle cx="5" cy="17" r="1"/><path d="M10 7h10M10 12h10M10 17h10"/>)svg"},
    {"lineSpacing", Stroke, "", R"svg(<path d="M11 6h10M11 12h10M11 18h10M5 4v16M3 7l2-3 2 3M3 17l2 3 2-3"/>)svg"},
    {"columns", Stroke, "", R"svg(<path d="M4 6h6M4 10h6M4 14h6M4 18h6M14 6h6M14 10h6M14 14h6M14 18h6"/>)svg"},
    {"textLeft", Stroke, "", R"svg(<path d="M4 6h16M4 10h10M4 14h16M4 18h10"/>)svg"},
    {"textCenter", Stroke, "", R"svg(<path d="M4 6h16M7 10h10M4 14h16M7 18h10"/>)svg"},
    {"textRight", Stroke, "", R"svg(<path d="M4 6h16M10 10h10M4 14h16M10 18h10"/>)svg"},
    {"textTop", Stroke, "", R"svg(<path d="M4 4h16M8 9h8M8 13h8"/>)svg"},
    {"textMiddle", Stroke, "", R"svg(<path d="M4 12h16M8 7h8M8 17h8"/>)svg"},
    {"textBottom", Stroke, "", R"svg(<path d="M4 20h16M8 11h8M8 15h8"/>)svg"},
    {"fillBucket", Stroke, "", R"svg(<path d="M5 12 12 5l7 7-7 7z"/><path d="M19 16c1 2 2 3 2 4a2 2 0 0 1-4 0c0-1 1-2 2-4z"/>)svg"},
    {"lineColor", Stroke, "", R"svg(<path d="M4 20 20 4"/>)svg"},
    {"cue", Stroke, "", R"svg(<path d="M4 6h16M4 12h10M4 18h16"/><circle cx="18" cy="12" r="2"/>)svg"},
    {"note", Stroke, "", R"svg(<path d="M9 18V6l10-2v12"/><circle cx="6.5" cy="18" r="2.5"/><circle cx="16.5" cy="16" r="2.5"/>)svg"},
    {"shapeRect", Stroke, "", R"svg(<rect x="4" y="6" width="16" height="12" rx="1"/>)svg"},
    {"shapeRound", Stroke, "", R"svg(<rect x="4" y="6" width="16" height="12" rx="4"/>)svg"},
    {"shapeEllipse", Stroke, "", R"svg(<ellipse cx="12" cy="12" rx="9" ry="7"/>)svg"},
    {"shapeTriangle", Stroke, "", R"svg(<path d="M12 5 21 19H3z"/>)svg"},
    {"shapeRightTriangle", Stroke, "", R"svg(<path d="M4 5v14h16z"/>)svg"},
    {"shapeLine", Stroke, "", R"svg(<path d="M4 19 20 5"/>)svg"},
    {"shapeArrow", Stroke, "", R"svg(<path d="M3 9h10V5l8 7-8 7v-4H3z"/>)svg"},
    {"shapeStar", Stroke, "", R"svg(<path d="m12 3 2.7 5.8 6.3.8-4.6 4.3 1.2 6.3-5.6-3.1-5.6 3.1 1.2-6.3L3 9.6l6.3-.8z"/>)svg"},
    {"shapeRhombus", Stroke, "", R"svg(<path d="m12 3 9 9-9 9-9-9z"/>)svg"},
    {"shapePolygon", Stroke, "", R"svg(<path d="m12 3 8.5 6.2-3.2 10H6.7l-3.2-10z"/>)svg"},
    {"shapeDoubleArrow", Stroke, "", R"svg(<path d="M2 12l5-5v3h10V7l5 5-5 5v-3H7v3z"/>)svg"},
    {"shapeCallout", Stroke, "", R"svg(<path d="M4 5h16v11H10l-4 4v-4H4z"/>)svg"},
    {"shapeCurve", Stroke, "", R"svg(<path d="M4 19C8 5 14 19 20 5"/>)svg"},
    {"alignBottom", Stroke, "",
     R"svg(<path d="M4 20h16"/><rect x="7" y="4" width="4" height="13" rx="1"/><rect x="13" y="8" width="4" height="9" rx="1"/>)svg"},
    {"alignCenter", Stroke, "",
     R"svg(<path d="M12 4v16"/><rect x="6" y="7" width="12" height="4" rx="1"/><rect x="8" y="13" width="8" height="4" rx="1"/>)svg"},
    {"alignLeft", Stroke, "",
     R"svg(<path d="M4 4v16"/><rect x="7" y="7" width="13" height="4" rx="1"/><rect x="7" y="13" width="9" height="4" rx="1"/>)svg"},
    {"alignMiddle", Stroke, "",
     R"svg(<path d="M4 12h16"/><rect x="7" y="6" width="4" height="12" rx="1"/><rect x="13" y="8" width="4" height="8" rx="1"/>)svg"},
    {"alignRight", Stroke, "",
     R"svg(<path d="M20 4v16"/><rect x="4" y="7" width="13" height="4" rx="1"/><rect x="8" y="13" width="9" height="4" rx="1"/>)svg"},
    {"alignTop", Stroke, "",
     R"svg(<path d="M4 4h16"/><rect x="7" y="7" width="4" height="13" rx="1"/><rect x="13" y="7" width="4" height="9" rx="1"/>)svg"},
    {"captions", Stroke, "",
     R"svg(<rect x="3" y="5" width="18" height="14" rx="2" ry="2"/><path d="M7 15h4M15 15h2M7 11h2M13 11h4"/>)svg"},
    {"cast", Stroke, "",
     R"svg(<path d="M2 8V6a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-6"/><path d="M2 12a9 9 0 0 1 8 8"/><path d="M2 16a5 5 0 0 1 4 4"/><line x1="2" y1="20" x2="2.01" y2="20"/>)svg"},
    {"church", Stroke, "",
     R"svg(<path d="M10 9h4"/><path d="M12 7v5"/><path d="M14 22v-4a2 2 0 0 0-4 0v4"/><path d="M18 22V5.618a1 1 0 0 0-.553-.894l-4.553-2.277a2 2 0 0 0-1.788 0L6.553 4.724A1 1 0 0 0 6 5.618V22"/><path d="m18 7 3.447 1.724a1 1 0 0 1 .553.894V20a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2v-9.382a1 1 0 0 1 .553-.894L6 7"/>)svg"},
    {"globe", Stroke, "",
     R"svg(<circle cx="12" cy="12" r="10"/><path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"/><path d="M2 12h20"/>)svg"},
    {"languages", Stroke, "",
     R"svg(<path d="m5 8 6 6"/><path d="m4 14 6-6 2-3"/><path d="M2 5h12"/><path d="M7 2h1"/><path d="m22 22-5-10-5 10"/><path d="M14 18h6"/>)svg"},
    {"laptop", Stroke, "",
     R"svg(<path d="M20 16V7a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v9m16 0H4m16 0 1.28 2.55a1 1 0 0 1-.9 1.45H3.62a1 1 0 0 1-.9-1.45L4 16"/>)svg"},
    {"mic", Stroke, "",
     R"svg(<path d="M12 2a3 3 0 0 0-3 3v7a3 3 0 0 0 6 0V5a3 3 0 0 0-3-3Z"/><path d="M19 10v2a7 7 0 0 1-14 0v-2"/><line x1="12" y1="19" x2="12" y2="22"/>)svg"},
    {"projector", Stroke, "",
     R"svg(<path d="M5 7 3 5"/><path d="M9 6V3"/><path d="m13 7 2-2"/><circle cx="9" cy="13" r="3"/><path d="M11.83 12H20a2 2 0 0 1 2 2v4a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2v-4a2 2 0 0 1 2-2h2.17"/><path d="M16 16h2"/>)svg"},
    {"pulpit", Stroke, "",
     R"svg(<path d="M3 9 20 5l1 4z"/><path d="M5 9l2 12h10l2-12"/><path d="M12 12v6M10 14h4"/>)svg"},
    {"screen", Stroke, "",
     R"svg(<path d="M2 3h20"/><path d="M21 3v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V3"/><path d="m7 21 5-5 5 5"/>)svg"},
    {"tablet", Stroke, "",
     R"svg(<rect x="4" y="2" width="16" height="20" rx="2" ry="2"/><line x1="12" y1="18" x2="12.01" y2="18"/>)svg"},
    {"titles", Stroke, "",
     R"svg(<path d="M22 17H2a3 3 0 0 0 3-3V9a5 5 0 0 1 10 0v5"/><path d="M6 17v2a3 3 0 0 0 6 0v-2"/>)svg"},
    {"bible", Stroke, "",
     R"svg(<path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/>)svg"},
    {"bmd", Stroke, "",
     R"svg(<rect x="2" y="7" width="20" height="10" rx="2"/><path d="M6 7V5M10 7V4M14 7V5M18 7V6"/>)svg"},
    {"break", Stroke, "",
     R"svg(<rect x="4" y="3" width="16" height="7" rx="1.5"/><rect x="4" y="14" width="16" height="7" rx="1.5"/>)svg"},
    {"chain", Stroke, "",
     R"svg(<path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"/><path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"/>)svg"},
    {"check", Stroke, "",
     R"svg(<polyline points="20 6 9 17 4 12"/>)svg"},
    {"chevronDown", Stroke, "",
     R"svg(<polyline points="6 9 12 15 18 9"/>)svg"},
    {"chevronLeft", Stroke, "",
     R"svg(<polyline points="15 18 9 12 15 6"/>)svg"},
    {"chevronRight", Stroke, "",
     R"svg(<polyline points="9 18 15 12 9 6"/>)svg"},
    {"clock", Stroke, "",
     R"svg(<circle cx="12" cy="12" r="9"/><polyline points="12 6.5 12 12 15.5 14"/>)svg"},
    {"close", Stroke, "",
     R"svg(<path d="M18 6 6 18M6 6l12 12"/>)svg"},
    {"copy", Stroke, "",
     R"svg(<rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>)svg"},
    {"database", Stroke, "",
     R"svg(<ellipse cx="12" cy="5" rx="9" ry="3"/><path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"/><path d="M3 12c0 1.66 4 3 9 3s9-1.34 9-3"/>)svg"},
    {"display", Stroke, "",
     R"svg(<rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/>)svg"},
    {"window", Stroke, "",
     R"svg(<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 9h18"/><path d="M6 6.5h.01M8.5 6.5h.01"/>)svg"},
    {"distributeH", Stroke, "",
     R"svg(<path d="M4 4v16M20 4v16"/><rect x="8" y="8" width="3" height="8" rx="1"/><rect x="13" y="8" width="3" height="8" rx="1"/>)svg"},
    {"distributeV", Stroke, "",
     R"svg(<path d="M4 4h16M4 20h16"/><rect x="8" y="8" width="8" height="3" rx="1"/><rect x="8" y="13" width="8" height="3" rx="1"/>)svg"},
    {"divider", Stroke, "",
     R"svg(<line x1="3" y1="12" x2="21" y2="12"/>)svg"},
    {"down", Stroke, "",
     R"svg(<path d="M12 5v14"/><polyline points="19 12 12 19 5 12"/>)svg"},
    {"edit", Stroke, "",
     R"svg(<path d="M17 3a2.828 2.828 0 1 1 4 4L7.5 20.5 2 22l1.5-5.5L17 3z"/>)svg"},
    {"export", Stroke, "",
     R"svg(<path d="M14 3h7v7"/><line x1="21" y1="3" x2="11" y2="13"/><path d="M19 14v5a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7a2 2 0 0 1 2-2h5"/>)svg"},
    {"feather", Stroke, "",
     R"svg(<path d="M20.24 12.24a6 6 0 0 0-8.49-8.49L5 10.5V19h8.5z"/><line x1="16" y1="8" x2="2" y2="22"/><line x1="17.5" y1="15" x2="9" y2="15"/>)svg"},
    {"flipH", Stroke, "",
     R"svg(<path d="M12 3v18"/><path d="M7 8l-4 4 4 4"/><path d="M17 8l4 4-4 4"/>)svg"},
    {"flipV", Stroke, "",
     R"svg(<path d="M3 12h18"/><path d="M8 7l4-4 4 4"/><path d="M8 17l4 4 4-4"/>)svg"},
    {"folder", Stroke, "",
     R"svg(<path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/>)svg"},
    {"folderPlus", Stroke, "",
     R"svg(<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7z"/><path d="M12 11v6M9 14h6"/>)svg"},
    {"freeform", Stroke, "",
     R"svg(<path d="M3 11l18-5v12L3 14z"/><path d="M11.6 16.8a3 3 0 1 1-5.8-1.6"/>)svg"},
    {"freeze", Stroke, "",
     R"svg(<line x1="12" y1="2" x2="12" y2="22"/><line x1="3.34" y1="7" x2="20.66" y2="17"/><line x1="20.66" y1="7" x2="3.34" y2="17"/>)svg"},
    {"grid", Stroke, "",
     R"svg(<rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/>)svg"},
    {"group", Stroke, "",
     R"svg(<rect x="3" y="3" width="10" height="10" rx="1"/><rect x="11" y="11" width="10" height="10" rx="1"/>)svg"},
    {"hammer", Stroke, "",
     R"svg(<path d="m15 12-8.373 8.373a1 1 0 1 1-3-3L12 9"/><path d="m18 15 4-4"/><path d="m21.5 11.5-1.914-1.914A2 2 0 0 1 19 8.172V7l-2.26-2.26a6 6 0 0 0-4.202-1.756L9 2.96l.92.82A6.18 6.18 0 0 1 12 8.4V10l2 2h1.172a2 2 0 0 1 1.414.586L18.5 14.5"/>)svg"},
    {"image", Stroke, "",
     R"svg(<rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/>)svg"},
    {"import", Stroke, "",
     R"svg(<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/>)svg"},
    {"indicator", Stroke, "",
     R"svg(<path d="M20.6 13.4l-7.2 7.2a2 2 0 0 1-2.8 0L2 12V2h10l8.6 8.6a2 2 0 0 1 0 2.8z"/><line x1="7" y1="7" x2="7.01" y2="7"/>)svg"},
    {"layers", Stroke, "",
     R"svg(<path d="M12 2 2 7l10 5 10-5-10-5z"/><path d="m2 17 10 5 10-5"/><path d="m2 12 10 5 10-5"/>)svg"},
    {"layout", Stroke, "",
     R"svg(<rect x="3" y="3" width="18" height="18" rx="2"/><line x1="9" y1="3" x2="9" y2="21"/><line x1="15" y1="3" x2="15" y2="21"/><line x1="3" y1="11" x2="9" y2="11"/>)svg"},
    {"list", Stroke, "",
     R"svg(<line x1="8" y1="6" x2="21" y2="6"/><line x1="8" y1="12" x2="21" y2="12"/><line x1="8" y1="18" x2="21" y2="18"/><line x1="3" y1="6" x2="3.01" y2="6"/><line x1="3" y1="12" x2="3.01" y2="12"/><line x1="3" y1="18" x2="3.01" y2="18"/>)svg"},
    {"listPlus", Stroke, "",
     R"svg(<path d="M11 12H3"/><path d="M16 6H3"/><path d="M16 18H3"/><path d="M18 9v6"/><path d="M21 12h-6"/>)svg"},
    {"lock", Stroke, "",
     R"svg(<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>)svg"},
    {"ndi", Stroke, "",
     R"svg(<circle cx="12" cy="12" r="2"/><path d="M16.24 7.76a6 6 0 0 1 0 8.49M7.76 16.24a6 6 0 0 1 0-8.49"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14M4.93 19.07a10 10 0 0 1 0-14.14"/>)svg"},
    {"parent", Stroke, "",
     R"svg(<line x1="12" y1="19" x2="12" y2="5"/><polyline points="5 12 12 5 19 12"/>)svg"},
    {"pause", Stroke, "",
     R"svg(<rect x="6" y="4.5" width="4" height="15" rx="1"/><rect x="14" y="4.5" width="4" height="15" rx="1"/>)svg"},
    {"pen", Stroke, "",
     R"svg(<path d="M12 20h9"/><path d="M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4Z"/>)svg"},
    {"person", Stroke, "",
     R"svg(<path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/>)svg"},
    {"phone", Stroke, "",
     R"svg(<rect x="5" y="2" width="14" height="20" rx="2" ry="2"/><line x1="12" y1="18" x2="12.01" y2="18"/>)svg"},
    {"play", Stroke, "",
     R"svg(<path d="M6 4.5v15a1 1 0 0 0 1.5.86l12-7.5a1 1 0 0 0 0-1.72l-12-7.5A1 1 0 0 0 6 4.5z"/>)svg"},
    {"plus", Stroke, "",
     R"svg(<path d="M12 5v14"/><path d="M5 12h14"/>)svg"},
    {"rename", Stroke, "",
     R"svg(<path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4L16.5 3.5z"/>)svg"},
    {"reset", Stroke, "",
     R"svg(<polyline points="1 4 1 10 7 10"/><path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10"/>)svg"},
    {"reload", Stroke, "",
     R"svg(<polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>)svg"},
    {"server", Stroke, "",
     R"svg(<rect x="2" y="2" width="20" height="8" rx="2" ry="2"/><rect x="2" y="14" width="20" height="8" rx="2" ry="2"/><line x1="6" y1="6" x2="6.01" y2="6"/><line x1="6" y1="18" x2="6.01" y2="18"/>)svg"},
    {"sync", Stroke, "",
     R"svg(<path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"/><path d="M21 3v5h-5"/><path d="M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16"/><path d="M8 16H3v5"/>)svg"},
    {"search", Stroke, "",
     R"svg(<circle cx="11" cy="11" r="7"/><path d="M20 20l-3.5-3.5"/>)svg"},
    {"settings", Stroke, "",
     R"svg(<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/>)svg"},
    {"shape", Stroke, "",
     R"svg(<rect x="2.5" y="2.5" width="10.5" height="10.5" rx="1.5"/><circle cx="16.5" cy="16.5" r="5"/>)svg"},
    {"sliders", Stroke, "",
     R"svg(<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>)svg"},
    {"song", Stroke, "",
     R"svg(<path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/>)svg"},
    {"stop", Stroke, "",
     R"svg(<rect x="5.5" y="5.5" width="13" height="13" rx="1.5"/>)svg"},
    {"swap", Stroke, "",
     R"svg(<path d="M4 8h16"/><path d="M16 4l4 4-4 4"/><path d="M20 16H4"/><path d="M8 12l-4 4 4 4"/>)svg"},
    {"table", Stroke, "",
     R"svg(<rect x="3" y="4" width="18" height="16" rx="2"/><line x1="3" y1="10" x2="21" y2="10"/><line x1="3" y1="15" x2="21" y2="15"/><line x1="9" y1="10" x2="9" y2="20"/><line x1="15" y1="10" x2="15" y2="20"/>)svg"},
    {"tag", Stroke, "",
     R"svg(<path d="M12.59 2.59A2 2 0 0 0 11.17 2H4a2 2 0 0 0-2 2v7.17a2 2 0 0 0 .59 1.42l8.7 8.7a2.43 2.43 0 0 0 3.42 0l6.58-6.58a2.43 2.43 0 0 0 0-3.42z"/><circle cx="7.5" cy="7.5" r="1.2"/>)svg"},
    {"target", Stroke, "",
     R"svg(<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4"/><line x1="12" y1="1" x2="12" y2="4"/><line x1="12" y1="20" x2="12" y2="23"/><line x1="1" y1="12" x2="4" y2="12"/><line x1="20" y1="12" x2="23" y2="12"/>)svg"},
    {"textBox", Stroke, "",
     R"svg(<polyline points="4 7 4 4 20 4 20 7"/><line x1="12" y1="4" x2="12" y2="20"/><line x1="9" y1="20" x2="15" y2="20"/>)svg"},
    {"themes", Stroke, "",
     R"svg(<circle cx="7.5" cy="7.5" r="3.5"/><circle cx="16.5" cy="7.5" r="3.5"/><circle cx="7.5" cy="16.5" r="3.5"/><circle cx="16.5" cy="16.5" r="3.5"/>)svg"},
    {"toBack", Stroke, "",
     R"svg(<line x1="4.5" y1="21.5" x2="19.5" y2="21.5"/><polyline points="7.5 14 12 18.5 16.5 14"/><line x1="12" y1="18.5" x2="12" y2="5"/>)svg"},
    {"toFront", Stroke, "",
     R"svg(<line x1="4.5" y1="2.5" x2="19.5" y2="2.5"/><polyline points="7.5 10 12 5.5 16.5 10"/><line x1="12" y1="5.5" x2="12" y2="19"/>)svg"},
    {"trash", Stroke, "",
     R"svg(<polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>)svg"},
    {"tv", Stroke, "",
     R"svg(<rect x="2" y="7" width="20" height="15" rx="2"/><polyline points="17 2 12 7 7 2"/>)svg"},
    {"ungroup", Stroke, "",
     R"svg(<rect x="3" y="4" width="8" height="8" rx="1"/><rect x="13" y="12" width="8" height="8" rx="1"/>)svg"},
    {"unlock", Stroke, "",
     R"svg(<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0"/>)svg"},
    {"up", Stroke, "",
     R"svg(<path d="M12 19V5"/><polyline points="5 12 12 5 19 12"/>)svg"},
    {"audio", Stroke, "",
     R"svg(<path d="M11 5L6 9H2v6h4l5 4V5z"/><path d="M15.5 8.5a5 5 0 0 1 0 7"/><path d="M19 5a10 10 0 0 1 0 14"/>)svg"},
    // The Audio Mixer's Mute (a speaker struck through; "audio" is the
    // speaker) and Listen.
    {"speakerMuted", Stroke, "",
     R"svg(<path d="M11 5L6 9H2v6h4l5 4V5z"/><line x1="22" y1="9" x2="16" y2="15"/><line x1="16" y1="9" x2="22" y2="15"/>)svg"},
    {"headphones", Stroke, "",
     R"svg(<path d="M3 14h3a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-7a9 9 0 0 1 18 0v7a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3"/>)svg"},
    {"users", Stroke, "",
     R"svg(<path d="M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M22 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/>)svg"},
    {"video", Stroke, "",
     R"svg(<polygon points="23 7 16 12 23 17 23 7"/><rect x="1" y="5" width="15" height="14" rx="2" ry="2"/>)svg"},
    // Stack glyphs only the slide editor uses.
    {"heart", Stroke, "",
     R"svg(<path d="M19 14c1.49-1.46 3-3.21 3-5.5A5.5 5.5 0 0 0 16.5 3c-1.76 0-3 .5-4.5 2-1.5-1.5-2.74-2-4.5-2A5.5 5.5 0 0 0 2 8.5c0 2.3 1.5 4.05 3 5.5l7 7Z"/>)svg"},
    {"info", Stroke, "",
     R"svg(<circle cx="12" cy="12" r="9"/><line x1="12" y1="11" x2="12" y2="16"/><line x1="12" y1="7.5" x2="12.01" y2="7.5"/>)svg"},
    {"quote", Stroke, "",
     R"svg(<path d="M3 21c3 0 7-1 7-8V5c0-1.25-.756-2.017-2-2H4c-1.25 0-2 .75-2 1.972V11c0 1.25.75 2 2 2 1 0 1 0 1 1v1c0 1-1 2-2 2s-1 .008-1 1.031V20c0 1 0 1 1 1z"/><path d="M15 21c3 0 7-1 7-8V5c0-1.25-.757-2.017-2-2h-4c-1.25 0-2 .75-2 1.972V11c0 1.25.75 2 2 2h.75c0 2.25.25 4-2.75 4v3c0 1 0 1 1 1z"/>)svg"},
    {"star", Stroke, "",
     R"svg(<polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2"/>)svg"},
    {"timer", Stroke, "",
     R"svg(<line x1="10" y1="2" x2="14" y2="2"/><line x1="12" y1="14" x2="15" y2="11"/><circle cx="12" cy="14" r="8"/>)svg"},
    // Send to: the stack an item will be sent to (the crosshair, its centre
    // filled).
    {"sendTo", Stroke, "",
     R"svg(<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="5"/><circle cx="12" cy="12" r="1.6" fill="{ink}" stroke="none"/><line x1="12" y1="1" x2="12" y2="5"/><line x1="12" y1="19" x2="12" y2="23"/><line x1="1" y1="12" x2="5" y2="12"/><line x1="19" y1="12" x2="23" y2="12"/>)svg"},
    // --- fill ---
    {"more", Fill, "",
     R"svg(<circle cx="6" cy="12" r="1.7"/><circle cx="12" cy="12" r="1.7"/><circle cx="18" cy="12" r="1.7"/>)svg"},
    // The app's mark, traced from packaging/icons/seventhslide-icon.png: its
    // two halves, the 7 the gap between them.
    {"logo", Fill, "61 199 902 626",
     R"svg(<path d="M274 199 290 199 290 317 591 317 315 820 257 818 179 810 176 808 169 808 150 803 133 795 116 783 96 761 82 733 72 687 73 683 69 662 65 617 61 528 65 400 74 319 82 285 89 269 102 249 117 234 135 222 153 214 192 206ZM754 200 804 203 810 205 846 208 849 210 856 210 871 214 883 219 907 234 922 249 924 254 931 262 941 282 950 319 957 376 961 433 963 528 961 550 961 585 955 662 948 709 939 741 928 761 908 783 896 792 881 800 856 808 794 816 720 819 708 821 519 825 488 824 754 324Z"/>)svg"},
    // --- solid ---
    {"blank", Solid, "",
     R"svg(<rect x="3" y="4" width="18" height="14" rx="2"/><line x1="9" y1="21" x2="15" y2="21" stroke-linecap="round" fill="none"/>)svg"},
    // --- raw ---
    {"advance", Raw, "",
     R"svg(<path d="M4 18l8.5-6L4 6v12zm9-12v12l8.5-6L13 6z" fill="{ink}" stroke="none"/>)svg"},
    {"caret", Raw, "0 0 12 12",
     R"svg(<path d="M2.5 4.5L6 8l3.5-3.5H2.5z" fill="{ink}" stroke="none"/>)svg"},
    {"tile/audio", Raw, "",
     R"svg(<path d="M4 9h3l5-4v14l-5-4H4z" fill="{ink}" stroke="none"/><path d="M15.5 9a4 4 0 0 1 0 6M18.5 6.5a8 8 0 0 1 0 11" fill="none" stroke="{ink}" stroke-width="2" stroke-linecap="round"/>)svg"},
    {"tile/folder", Raw, "",
     R"svg(<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7z" fill="{ink}" stroke="none"/>)svg"},
    {"tile/freeform", Raw, "",
     R"svg(<rect x="5" y="3" width="14" height="18" rx="2" fill="none" stroke="{ink}" stroke-width="2"/><path d="M8 8h8M8 12h8M8 16h5" fill="none" stroke="{ink}" stroke-width="2" stroke-linecap="round"/>)svg"},
    {"tile/image", Raw, "",
     R"svg(<rect x="3" y="5" width="18" height="14" rx="2" fill="none" stroke="{ink}" stroke-width="2"/><circle cx="8.5" cy="10" r="1.5" fill="{ink}"/><path d="M21 15l-5-5-8 8" fill="none" stroke="{ink}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>)svg"},
    {"tile/parent", Raw, "",
     R"svg(<path d="M12 19V5" fill="none" stroke="{ink}" stroke-width="2.5" stroke-linecap="round"/><polyline points="5 12 12 5 19 12" fill="none" stroke="{ink}" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>)svg"},
    {"tile/web", Raw, "",
     R"svg(<circle cx="12" cy="12" r="9" fill="none" stroke="{ink}" stroke-width="2"/><path d="M12 3a13 13 0 0 0 0 18 13 13 0 0 0 0-18M3 12h18" fill="none" stroke="{ink}" stroke-width="2"/>)svg"},
    {"tile/video", Raw, "",
     R"svg(<rect x="3" y="6" width="14" height="12" rx="2" fill="none" stroke="{ink}" stroke-width="2"/><path d="M17 10l4-2v8l-4-2z" fill="{ink}" stroke="none"/>)svg"},
};

}  // namespace

std::span<const Glyph> all() noexcept
{
    return k_glyphs;
}

const Glyph* find(std::string_view name) noexcept
{
    const auto it = std::ranges::find(k_glyphs, name, &Glyph::name);
    return it == std::end(k_glyphs) ? nullptr : &*it;
}

QString svg(std::string_view name, const QString& ink, double stroke_width)
{
    const QString c = ink.isEmpty() ? QStringLiteral("#cccccc") : ink;
    const Glyph* g = find(name);
    const QString view_box = g && !g->view_box.empty() ? QString::fromUtf8(g->view_box)
                                                       : QStringLiteral("0 0 24 24");
    QString body = g ? QString::fromUtf8(g->body) : QString();
    body.replace(QStringLiteral("{ink}"), c);
    const QString open = QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"")
                         + view_box + QLatin1Char('"');
    const QString close = QStringLiteral("</svg>");
    const Style style = g ? g->style : Stroke;
    switch (style) {
    case Fill:
        return open + QStringLiteral(" fill=\"%1\">").arg(c) + body + close;
    case Solid:
        return open
               + QStringLiteral(" fill=\"%1\" stroke=\"%1\" stroke-width=\"1.5\" "
                                "stroke-linejoin=\"round\">")
                     .arg(c)
               + body + close;
    case Raw:
        return open + QLatin1Char('>') + body + close;
    case Stroke:
        break;
    }
    return open
           + QStringLiteral(" fill=\"none\" stroke=\"%1\" stroke-width=\"%2\" "
                            "stroke-linecap=\"round\" stroke-linejoin=\"round\">")
                 .arg(c, QString::number(stroke_width))
           + body + close;
}

QString data_url(const QString& svg)
{
    return QStringLiteral("data:image/svg+xml;utf8,")
           + QString::fromUtf8(QUrl::toPercentEncoding(svg));
}

QString sprite()
{
    QString out = QStringLiteral(
        "<svg width=\"0\" height=\"0\" style=\"position:absolute\" aria-hidden=\"true\" "
        "focusable=\"false\"><defs>");
    for (const Glyph& g : k_glyphs) {
        const QString view_box =
            g.view_box.empty() ? QStringLiteral("0 0 24 24") : QString::fromUtf8(g.view_box);
        QString body = QString::fromUtf8(g.body);
        body.replace(QStringLiteral("{ink}"), QStringLiteral("currentColor"));
        // Painted through custom properties, which reach into a <use>, so
        // the page can still paint one differently (a folder filled).
        QString paint;
        switch (g.style) {
        case Fill:
            paint = QStringLiteral("fill:var(--ic-fill,currentColor)");
            break;
        case Solid:
            paint = QStringLiteral("fill:var(--ic-fill,currentColor);"
                                   "stroke:var(--ic-stroke,currentColor);stroke-width:1.5;"
                                   "stroke-linejoin:round");
            break;
        case Raw:
            break;
        case Stroke:
            // No width: the page's stroke-width reaches it through <use>.
            paint = QStringLiteral("fill:var(--ic-fill,none);stroke:var(--ic-stroke,currentColor);"
                                   "stroke-linecap:round;stroke-linejoin:round");
            break;
        }
        out += QStringLiteral("<symbol id=\"ic-%1\" viewBox=\"%2\">")
                   .arg(QString::fromUtf8(g.name), view_box);
        out += paint.isEmpty() ? body : QStringLiteral("<g style=\"%1\">%2</g>").arg(paint, body);
        out += QStringLiteral("</symbol>");
    }
    out += QStringLiteral("</defs></svg>");
    return out;
}

}  // namespace glyphs

QString Icons::url(const QString& kind, const QVariant& ink, double strokeWidth) const
{
    return svg_icon_url(glyphs::svg(kind.toStdString(), ink_name(ink), strokeWidth));
}

QString Icons::svgUrl(const QString& svg) const
{
    return svg_icon_url(svg);
}

QString Icons::ink_name(const QVariant& ink)
{
    if (ink.metaType() == QMetaType::fromType<QColor>()) {
        const auto color = ink.value<QColor>();
        // As QML writes a colour: #rrggbb, or #aarrggbb when it is see-through.
        return color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb);
    }
    return ink.toString();
}

}  // namespace seventhslide::app
