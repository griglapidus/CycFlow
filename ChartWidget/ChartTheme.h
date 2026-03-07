#ifndef CHARTTHEME_H
#define CHARTTHEME_H

#include <QPalette>
#include <QColor>
#include <QList>

/**
 * @brief Factory that builds and applies application-wide colour palettes.
 *
 * Two hand-tuned palettes are provided — one for dark environments and one
 * for light.  The correct variant is automatically selected by inspecting
 * the operating-system colour scheme; it can also be overridden explicitly.
 *
 * ### Series colors
 * ChartTheme maintains two ordered lists of series colors — one per theme
 * variant.  Colors in the dark list are bright and vivid to stand out
 * against the dark plot background; colors in the light list use the same
 * hues but are deeper and more saturated to remain legible on a light
 * background.
 *
 * Use seriesColor() to resolve an auto-assigned color index to a QColor,
 * and seriesColorCount() to know how many distinct colors are available
 * before the palette wraps around.
 *
 * ### Typical usage
 * @code
 *   // main.cpp — call once before the first window is shown:
 *   ChartTheme::applyToApplication();   // auto-detects OS theme
 *
 *   // Force a specific variant:
 *   ChartTheme::applyToApplication(ChartTheme::Variant::Dark);
 *
 *   // Obtain a palette without applying it (e.g. for a single widget):
 *   myWidget->setPalette(ChartTheme::palette(ChartTheme::Variant::Light));
 *
 *   // Resolve the 3rd auto-assigned series color for the current theme:
 *   QColor c = ChartTheme::seriesColor(2, ChartTheme::systemVariant());
 * @endcode
 *
 * ### Customising colors
 * Edit ChartTheme::makeDarkPalette(), ChartTheme::makeLightPalette() and the
 * two static color lists (kSeriesColorsDark / kSeriesColorsLight) at the top
 * of ChartTheme.cpp.  All colors are gathered in one place with comments
 * describing where each QPalette role or series-color slot is used.
 */
class ChartTheme
{
public:
    /** @brief Selects between the two built-in colour schemes. */
    enum class Variant { Dark, Light };

    /**
     * @brief Detects the current OS colour scheme.
     *
     * On Qt 6.5+ this queries @c QGuiApplication::styleHints()->colorScheme().
     * On older Qt versions it falls back to comparing the lightness of
     * @c QPalette::Window against the 50 % threshold.
     */
    static Variant systemVariant();

    /**
     * @brief Builds and returns the QPalette for the requested variant.
     * @param v  Colour scheme to build.
     */
    static QPalette palette(Variant v);

    /**
     * @brief Applies the selected palette to the entire application.
     *
     * Calls @c QApplication::setPalette() followed by
     * @c QApplication::setStyleSheet() to style scroll bars consistently
     * with the chosen theme.
     *
     * @param v  Colour scheme to apply.
     */
    static void applyToApplication(Variant v);

    /**
     * @brief Overload that auto-detects the OS theme via systemVariant().
     */
    static void applyToApplication();

    // -------------------------------------------------------------------------
    //  Series color palette
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the number of distinct series colors in the palette.
     *
     * After this many series the palette wraps around modulo seriesColorCount().
     * Both variants always return the same count.
     */
    static int seriesColorCount();

    /**
     * @brief Resolves a series color index to a QColor for the given variant.
     *
     * Wraps automatically, so callers do not need to bounds-check the index.
     *
     * @param index  Zero-based color slot assigned to the series.
     * @param v      Active theme variant.
     * @return       Themed color appropriate for rendering on the plot background.
     */
    static QColor seriesColor(int index, Variant v);

    /**
     * @brief Returns the full ordered color list for the given variant.
     *
     * Useful for legend rendering or when all colors are needed at once.
     */
    static const QList<QColor>& seriesColors(Variant v);

    /**
     * @brief Constructs the dark palette.
     *
     * Edit the colour values in ChartTheme.cpp to tune the appearance.
     * See the role-to-usage table in that file for guidance.
     */
    static QPalette makeDarkPalette();

    /**
     * @brief Constructs the light palette.
     *
     * Edit the colour values in ChartTheme.cpp to tune the appearance.
     * See the role-to-usage table in that file for guidance.
     */
    static QPalette makeLightPalette();
};

#endif // CHARTTHEME_H
