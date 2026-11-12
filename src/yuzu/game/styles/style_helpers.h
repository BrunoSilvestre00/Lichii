// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QFont>
#include <QModelIndex>
#include <QPalette>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>

#include "common/common_types.h"

class QPainter;
class QWidget;

namespace LibraryStyles {

class LibraryStyleView;

/// What a style needs to know about one row of the library model. Every field is filled for game
/// rows; the "Add games folder" row only has `is_add_folder` and `title`.
struct GameInfo {
    bool valid = false;
    bool is_game = false;
    bool is_add_folder = false;
    /// The title from the game metadata, or the file name when the game has none.
    QString title;
    QString file_name;
    QString path;
    u64 program_id = 0;
    /// "XCI", "NSP", "NUP", "NX" and so on.
    QString file_type;
    /// NUP or NX package: a title of the newer console generation. Use it for a badge or a
    /// different frame colour, never with a console or company name in the UI.
    bool is_nx2 = false;
    bool has_icon = false;
    u64 play_time_seconds = 0;
    /// "New" (never played), "45 min" or "12.5 h".
    QString play_time_text;
    /// Version of the applied update without a prefix ("1.4.3"), empty for the base version.
    QString update_version;
    /// "1.0 h played · v1.4.3": the play time and the update version, the line the concepts show.
    QString info_line;
    /// Readable size ("14.2 GiB").
    QString size_text;
    /// Program id as 16 upper-case hex digits.
    QString program_id_text;
};

/**
 * Shared data accessors and painting helpers for the library styles. All functions take indexes
 * of the model the view received in SetModel (or of the source model, both work: the data is read
 * through the index). Colours follow the palette of the widget, which follows the theme, so day
 * and night come for free.
 */
class LibraryStyleContext {
public:
    static GameInfo Info(const QModelIndex& index);

    static QString Title(const QModelIndex& index);
    static QString InfoLine(const QModelIndex& index);

    /// Cover art at `size` (cropped to fill, aspect kept). Games without art get a neutral
    /// gradient with the title initials; the "Add games folder" row gets a folder with a plus.
    static QPixmap Cover(const QModelIndex& index, const QSize& size, const QPalette& palette);
    /// The art of a game without an icon: a gradient picked from the title, with its initials.
    /// No console art, so it fits every style and the Modern grid.
    static void PaintPlaceholderArt(QPainter* painter, const QRect& rect, const QString& title,
                                    const QPalette& palette);

    static bool IsDark(const QPalette& palette);
    /// Selection colour of the theme.
    static QColor Accent(const QPalette& palette);
    /// Side of the square cover art in every grid style: View > Game Icon Size, at least 32 px.
    /// One setting gives the same art size in every style.
    static int ArtSize();
    /// Art of the one-row showcase styles (Home Row tiles, Showcase centre): 1.6x ArtSize.
    static int FeaturedArtSize();
    /// CSS-like box shadow for a rounded rect of `size`: blurred by `blur` px, `alpha` 0..1.
    /// Draw it at the rect's top left minus blur (plus your offset). Cached per size.
    static QPixmap SoftShadow(const QSize& size, qreal radius, int blur, qreal alpha, qreal dpr);
    /// Card surface colour behind covers and panels.
    static QColor Surface(const QPalette& palette);
    /// Secondary text colour (info lines, hints).
    static QColor MutedText(const QPalette& palette);

    /// Wooden tabletop shared by the Cartridges and Box Shelf styles, sized to the viewport.
    /// Draw it at the origin; it does not scroll.
    static QPixmap WoodTable(const QSize& size, qreal dpr, bool dark);
    /// Cover clipped to a rounded rectangle.
    static void PaintCover(QPainter* painter, const QRect& rect, const QModelIndex& index,
                           const QPalette& palette, int radius);
    /// Accent ring around `rect` (drawn just outside it), the selection mark of every style.
    static void PaintSelection(QPainter* painter, const QRect& rect, const QPalette& palette,
                               int radius, int width = 3);
    /// Text elided to `max_lines` lines inside `rect`.
    /// Title in up to `max_lines` lines. A line that is too wide ends in "...", unless the
    /// full text is no wider than the "..." would be: then it is drawn whole, running up to
    /// `max_overflow` px past the box (default: the width of "...").
    static void PaintName(QPainter* painter, const QRect& rect, const QString& text,
                          const QColor& color, const QFont& font, Qt::Alignment alignment,
                          int max_lines = 2, int max_overflow = -1);
    /// Rounded panel with the title on top and the info line under it.
    static void PaintInfoPanel(QPainter* painter, const QRect& rect, const QModelIndex& index,
                               const QPalette& palette, const QFont& font);
    /// Dark gradient at the bottom of a cover with the title and info line on it, for styles that
    /// show names on top of the art.
    static void PaintCoverCaption(QPainter* painter, const QRect& cover_rect,
                                  const QModelIndex& index, const QFont& font, bool show_info);
};

/// Minimal view used by the placeholder files until a style lands: a plain list of the titles under
/// the style name.
LibraryStyleView* MakePlaceholderView(QWidget* parent, const QString& style_name);

} // namespace LibraryStyles
