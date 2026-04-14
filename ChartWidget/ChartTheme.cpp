// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "ChartTheme.h"

#include <QApplication>
#include <QStyleHints>

// =============================================================================
//  HOW TO EDIT COLOURS
// =============================================================================
//
//  Each palette constructor (makeDarkPalette / makeLightPalette) sets a full
//  QPalette using the helper:
//
//      set(role, active, inactive, disabled)
//
//  QPalette roles used by Chart components:
//
//  +-----------------------+--------------------------------------------------+
//  | Role                  | Used in                                          |
//  +-----------------------+--------------------------------------------------+
//  | Window                | ChartHeaderView panel background                 |
//  | WindowText            | Series name, row label text                      |
//  | Base                  | Plot area background (ChartDelegate)             |
//  | AlternateBase         | Alternating rows (set for completeness, unused)  |
//  | Text                  | Standard widget text (not used directly)         |
//  | Button                | Toolbar button background                        |
//  | ButtonText            | Toolbar button text                              |
//  | Highlight             | Row selection border, checked button accent      |
//  | HighlightedText       | Text over Highlight background (not used)        |
//  | Mid                   | Row dividers, resize grips, hint text,           |
//  |                       | toolbar button borders, scroll bar handles       |
//  | Dark                  | Scroll bar handle hover state (lighter than Mid) |
//  | Light                 | Highlight accents (not used directly)            |
//  | Link                  | ×scale annotation, cursor timestamp label        |
//  | LinkVisited           | Reserved                                         |
//  | ToolTipBase           | Tooltip background                               |
//  | ToolTipText           | Tooltip text                                     |
//  | PlaceholderText       | Input field placeholder text                     |
//  +-----------------------+--------------------------------------------------+
//
//  Three colour groups (ColorGroup):
//    Active   — widget has focus / window is active
//    Inactive — widget is unfocused (usually matches Active for Chart)
//    Disabled — unavailable state: dimColor used for "no data" indicator
//
// =============================================================================
//
//  HOW TO EDIT SERIES COLOURS
//  ===========================
//
//  Two ordered lists below define the cycling series color palette — one for
//  dark mode, one for light mode.  Both lists MUST have the same length.
//  Entry N in both lists represents the same logical "color slot": the same
//  hue family, adapted for the respective background.
//
//  Design rules
//  ------------
//  Dark colors  — bright and vivid, legible on the dark plot base (#0A0D14).
//                 Aim for HSL lightness ≥ 60 %.
//  Light colors — same hue family, deeper and more saturated, legible on the
//                 light plot base (#F8F9FC).  HSL lightness in 30–50 % works
//                 well.
//
//  Avoid colors that conflict with fixed semantic UI colors:
//    * Warm yellow/amber (cursor marker): do not use near #FFC840
//    * Selection highlight blues:         do not use near #2D5AB4 / #3778D2
//
//  The first four entries should be maximally distinct (≥ 90° hue apart) so
//  that charts with just a few series already look well differentiated.
//
// =============================================================================

namespace {

// clang-format off

// -----------------------------------------------------------------------------
//  Dark-theme series colors — bright, vivid, high contrast on #0A0D14
// -----------------------------------------------------------------------------
const QList<QColor> kSeriesColorsDark = {
    QColor(0x00, 0xD4, 0xFF),  //  0  cyan
    QColor(0xFF, 0x70, 0x30),  //  1  orange
    QColor(0x50, 0xFF, 0x80),  //  2  lime green
    QColor(0xB0, 0x70, 0xFF),  //  3  violet
    QColor(0xFF, 0xB0, 0x20),  //  4  amber
    QColor(0xFF, 0x40, 0xB0),  //  5  hot pink
    QColor(0x20, 0xFF, 0xCC),  //  6  teal / spring green
    QColor(0xFF, 0x50, 0x50),  //  7  coral red
    QColor(0x60, 0xC0, 0xFF),  //  8  sky blue
    QColor(0xD0, 0xFF, 0x40),  //  9  yellow-green
};

// -----------------------------------------------------------------------------
//  Light-theme series colors — same hue families, deeper, legible on #F8F9FC
// -----------------------------------------------------------------------------
const QList<QColor> kSeriesColorsLight = {
    QColor(0x00, 0x7A, 0xA8),  //  0  cyan   → steel-cyan
    QColor(0xBE, 0x3E, 0x00),  //  1  orange → burnt orange
    QColor(0x1A, 0x7A, 0x38),  //  2  green  → forest green
    QColor(0x5B, 0x20, 0xC0),  //  3  violet → indigo
    QColor(0xA0, 0x60, 0x00),  //  4  amber  → dark amber
    QColor(0xB0, 0x10, 0x68),  //  5  pink   → raspberry
    QColor(0x00, 0x7A, 0x68),  //  6  teal
    QColor(0xC0, 0x20, 0x30),  //  7  red    → crimson
    QColor(0x1A, 0x50, 0xC8),  //  8  blue   → royal blue
    QColor(0x5A, 0x80, 0x00),  //  9  lime   → olive
};

// clang-format on

/// Lightness threshold for dark/light theme auto-detection (0–255 scale).
/// Window colours whose lightness is strictly below this value are treated
/// as belonging to a dark theme.
constexpr int kDarkLightnessThreshold = 128;

} // anonymous namespace

// =============================================================================
//  Series color API
// =============================================================================

int ChartTheme::seriesColorCount()
{
    return kSeriesColorsDark.size();
}

const QList<QColor>& ChartTheme::seriesColors(Variant v)
{
    return (v == Variant::Dark) ? kSeriesColorsDark : kSeriesColorsLight;
}

QColor ChartTheme::seriesColor(int index, Variant v)
{
    const QList<QColor> &list = seriesColors(v);
    // Normalise to [0, size) so that negative indices also wrap correctly.
    return list[((index % list.size()) + list.size()) % list.size()];
}

// =============================================================================
//  Internal helper — sets a role for all three colour groups in one call
// =============================================================================

static void set(QPalette &p,
                QPalette::ColorRole role,
                QColor active,
                QColor inactive = {},
                QColor disabled = {})
{
    if (!inactive.isValid()) inactive = active;
    if (!disabled.isValid()) disabled = active;

    p.setColor(QPalette::Active,   role, active);
    p.setColor(QPalette::Inactive, role, inactive);
    p.setColor(QPalette::Disabled, role, disabled);
}

// =============================================================================
//  DARK PALETTE
// =============================================================================

QPalette ChartTheme::makeDarkPalette()
{
    QPalette p;

    // --- Backgrounds ---------------------------------------------------------
    set(p, QPalette::Window,        QColor( 18,  22,  32));
    set(p, QPalette::Base,          QColor( 10,  13,  20));
    set(p, QPalette::AlternateBase, QColor( 14,  18,  28));
    set(p, QPalette::Button,        QColor( 22,  28,  42));
    set(p, QPalette::ToolTipBase,   QColor( 22,  28,  42));

    // --- Text ----------------------------------------------------------------
    set(p, QPalette::WindowText,
        QColor(200, 210, 230),
        QColor(160, 170, 190),
        QColor( 80,  90, 110));

    set(p, QPalette::Text,
        QColor(200, 210, 230),
        QColor(160, 170, 190),
        QColor( 80,  90, 110));

    set(p, QPalette::ButtonText,
        QColor(136, 153, 187),
        QColor(100, 115, 148),
        QColor( 60,  70,  95));

    set(p, QPalette::ToolTipText,     QColor(200, 210, 230));
    set(p, QPalette::PlaceholderText, QColor( 80,  90, 110));

    // --- Accents -------------------------------------------------------------
    set(p, QPalette::Highlight,        QColor( 45,  90, 180));
    set(p, QPalette::HighlightedText,  QColor(220, 235, 255));
    set(p, QPalette::Link,             QColor( 90, 150, 220));
    set(p, QPalette::LinkVisited,      QColor(130, 100, 200));

    // --- Neutrals ------------------------------------------------------------
    set(p, QPalette::Mid,    QColor( 50,  62,  90));
    set(p, QPalette::Dark,   QColor( 75,  92, 135));
    set(p, QPalette::Light,  QColor( 30,  38,  58));
    set(p, QPalette::Shadow, QColor(  4,   5,  10));

    return p;
}

// =============================================================================
//  LIGHT PALETTE
// =============================================================================

QPalette ChartTheme::makeLightPalette()
{
    QPalette p;

    // --- Backgrounds ---------------------------------------------------------
    set(p, QPalette::Window,        QColor(236, 238, 244));
    set(p, QPalette::Base,          QColor(248, 249, 252));
    set(p, QPalette::AlternateBase, QColor(240, 242, 246));
    set(p, QPalette::Button,        QColor(226, 229, 236));
    set(p, QPalette::ToolTipBase,   QColor(255, 255, 220));

    // --- Text ----------------------------------------------------------------
    set(p, QPalette::WindowText,
        QColor( 30,  38,  55),
        QColor( 60,  70,  90),
        QColor(155, 162, 178));

    set(p, QPalette::Text,
        QColor( 30,  38,  55),
        QColor( 60,  70,  90),
        QColor(155, 162, 178));

    set(p, QPalette::ButtonText,
        QColor( 55,  70, 100),
        QColor( 80, 100, 135),
        QColor(160, 168, 182));

    set(p, QPalette::ToolTipText,     QColor( 40,  40,  40));
    set(p, QPalette::PlaceholderText, QColor(155, 162, 178));

    // --- Accents -------------------------------------------------------------
    set(p, QPalette::Highlight,       QColor( 55, 120, 210));
    set(p, QPalette::HighlightedText, QColor(255, 255, 255));
    set(p, QPalette::Link,            QColor( 20,  90, 180));
    set(p, QPalette::LinkVisited,     QColor(100,  50, 170));

    // --- Neutrals ------------------------------------------------------------
    set(p, QPalette::Mid,    QColor(185, 190, 205));
    set(p, QPalette::Dark,   QColor(175, 180, 195));
    set(p, QPalette::Light,  QColor(255, 255, 255));
    set(p, QPalette::Shadow, QColor(130, 135, 150));

    return p;
}

// =============================================================================
//  OS THEME DETECTION
// =============================================================================

ChartTheme::Variant ChartTheme::systemVariant()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark)  return Variant::Dark;
    if (scheme == Qt::ColorScheme::Light) return Variant::Light;
#endif
    const QColor win = QApplication::palette().color(QPalette::Window);
    return (win.lightness() < kDarkLightnessThreshold) ? Variant::Dark : Variant::Light;
}

// =============================================================================
//  APPLICATION-LEVEL PALETTE AND STYLE SHEET
// =============================================================================

QPalette ChartTheme::palette(Variant v)
{
    return (v == Variant::Dark) ? makeDarkPalette() : makeLightPalette();
}

void ChartTheme::applyToApplication(Variant v)
{
    QApplication::setPalette(palette(v));

    qApp->setStyleSheet(
        "QScrollBar:horizontal { background: palette(window); height: 10px; margin: 0; }"
        "QScrollBar:vertical   { background: palette(window); width:  10px; margin: 0; }"
        "QScrollBar::handle:horizontal { background: palette(mid); border-radius: 4px;"
        "    min-width: 20px; margin: 2px 0; }"
        "QScrollBar::handle:vertical   { background: palette(mid); border-radius: 4px;"
        "    min-height: 20px; margin: 0 2px; }"
        "QScrollBar::handle:horizontal:hover, QScrollBar::handle:vertical:hover"
        "    { background: palette(dark); }"
        "QScrollBar::handle:horizontal:pressed, QScrollBar::handle:vertical:pressed"
        "    { background: palette(highlight); }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
    );
}

void ChartTheme::applyToApplication()
{
    applyToApplication(systemVariant());
}
