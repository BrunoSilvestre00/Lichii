// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <vector>
#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QStringList>
#include <QWidget>
#include "game_card.h"
#include "yuzu/game/common.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/game/styles/style_helpers.h"

namespace {

/// Title of a game row for its placeholder art: the metadata title, else the file name.
QString PlaceholderTitle(const QModelIndex& index) {
    const QString title = index.data(GameListItemPath::TitleRole).toString();
    if (!title.isEmpty()) {
        return title;
    }
    return QFileInfo(index.data(GameListItemPath::FullPathRole).toString()).completeBaseName();
}

void DrawAddFolderArtwork(QPainter* painter, const QRect& bounds, const QColor& accent, bool dark) {
    const QRectF rect(bounds);
    const QColor surface = dark ? QColor(29, 30, 32) : QColor(243, 243, 244);
    painter->fillRect(rect, surface);

    const qreal width = rect.width();
    const qreal height = rect.height();
    const QRectF folder(rect.left() + width * 0.27, rect.top() + height * 0.31,
                        width * 0.46, height * 0.4);

    QPainterPath folder_path;
    folder_path.moveTo(folder.left(), folder.top() + folder.height() * 0.22);
    folder_path.lineTo(folder.left(), folder.top() + folder.height() * 0.08);
    folder_path.quadTo(folder.left(), folder.top(), folder.left() + folder.width() * 0.12,
                       folder.top());
    folder_path.lineTo(folder.left() + folder.width() * 0.4, folder.top());
    folder_path.lineTo(folder.left() + folder.width() * 0.53,
                       folder.top() + folder.height() * 0.2);
    folder_path.lineTo(folder.right() - folder.width() * 0.1,
                       folder.top() + folder.height() * 0.2);
    folder_path.quadTo(folder.right(), folder.top() + folder.height() * 0.2, folder.right(),
                       folder.top() + folder.height() * 0.3);
    folder_path.lineTo(folder.right(), folder.bottom() - folder.height() * 0.1);
    folder_path.quadTo(folder.right(), folder.bottom(), folder.right() - folder.width() * 0.1,
                       folder.bottom());
    folder_path.lineTo(folder.left() + folder.width() * 0.1, folder.bottom());
    folder_path.quadTo(folder.left(), folder.bottom(), folder.left(),
                       folder.bottom() - folder.height() * 0.1);
    folder_path.closeSubpath();

    QColor folder_fill = accent;
    folder_fill.setAlpha(dark ? 20 : 14);
    painter->setPen(QPen(accent, std::max(2.0, std::min(width, height) * 0.012), Qt::SolidLine,
                         Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(folder_fill);
    painter->drawPath(folder_path);

    const qreal badge_size = std::min(width, height) * 0.22;
    const QPointF badge_center(folder.right() - badge_size * 0.05,
                               folder.bottom() - badge_size * 0.04);
    const QRectF badge(badge_center.x() - badge_size / 2, badge_center.y() - badge_size / 2,
                       badge_size, badge_size);
    painter->setPen(Qt::NoPen);
    painter->setBrush(accent);
    painter->drawEllipse(badge);

    const qreal plus_half = badge_size * 0.22;
    painter->setPen(QPen(dark ? QColor(12, 29, 43) : QColor(Qt::white), badge_size * 0.09,
                         Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(QPointF(badge_center.x() - plus_half, badge_center.y()),
                      QPointF(badge_center.x() + plus_half, badge_center.y()));
    painter->drawLine(QPointF(badge_center.x(), badge_center.y() - plus_half),
                      QPointF(badge_center.x(), badge_center.y() + plus_half));
}

void DrawGameFolderArtwork(QPainter* painter, const QRect& bounds, const QColor& accent,
                           bool dark) {
    const QRectF rect(bounds);
    painter->fillRect(rect, dark ? QColor(29, 30, 32) : QColor(243, 243, 244));

    const qreal width = rect.width();
    const qreal height = rect.height();
    const QRectF folder(rect.left() + width * 0.25, rect.top() + height * 0.31,
                        width * 0.5, height * 0.39);
    QPainterPath path;
    path.moveTo(folder.left(), folder.top() + folder.height() * 0.2);
    path.lineTo(folder.left(), folder.top() + folder.height() * 0.08);
    path.quadTo(folder.left(), folder.top(), folder.left() + folder.width() * 0.12,
                folder.top());
    path.lineTo(folder.left() + folder.width() * 0.4, folder.top());
    path.lineTo(folder.left() + folder.width() * 0.53, folder.top() + folder.height() * 0.2);
    path.lineTo(folder.right() - folder.width() * 0.1, folder.top() + folder.height() * 0.2);
    path.quadTo(folder.right(), folder.top() + folder.height() * 0.2, folder.right(),
                folder.top() + folder.height() * 0.3);
    path.lineTo(folder.right(), folder.bottom() - folder.height() * 0.1);
    path.quadTo(folder.right(), folder.bottom(), folder.right() - folder.width() * 0.1,
                folder.bottom());
    path.lineTo(folder.left() + folder.width() * 0.1, folder.bottom());
    path.quadTo(folder.left(), folder.bottom(), folder.left(),
                folder.bottom() - folder.height() * 0.1);
    path.closeSubpath();

    QColor fill = dark ? QColor(72, 73, 76) : QColor(224, 225, 227);
    QColor outline = accent;
    outline.setAlpha(dark ? 120 : 100);
    painter->setPen(QPen(outline, std::max(2.0, std::min(width, height) * 0.012), Qt::SolidLine,
                         Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(fill);
    painter->drawPath(path);
}

bool IsGameDirectory(GameListItemType type) {
    return type == GameListItemType::CustomDir || type == GameListItemType::SdmcDir ||
           type == GameListItemType::UserNandDir || type == GameListItemType::SysNandDir;
}

}

QPixmap GameListFallbackIcon(int size, const QString& title, const QPalette& palette) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    LibraryStyles::LibraryStyleContext::PaintPlaceholderArt(&painter, pixmap.rect(), title,
                                                            palette);
    return pixmap;
}

namespace {

/// Info parts packed into lines of at most `width`: as many parts per line as fit, a part
/// that is too wide alone gets its own line (and is elided when drawn).
QStringList PackInfo(const QStringList& parts, const QFontMetrics& fm, int width) {
    const QString dot = QStringLiteral(" \u00B7 ");
    QStringList lines;
    QString line;
    for (const QString& part : parts) {
        const QString joined = line.isEmpty() ? part : line + dot + part;
        if (line.isEmpty() || fm.horizontalAdvance(joined) <= width) {
            line = joined;
        } else {
            lines << line;
            line = part;
        }
    }
    if (!line.isEmpty()) {
        lines << line;
    }
    return lines;
}

bool Fits(const QStringList& lines, const QFontMetrics& fm, int width) {
    return std::all_of(lines.begin(), lines.end(),
                       [&](const QString& line) { return fm.horizontalAdvance(line) <= width; });
}

} // namespace

GameCard::TextLayout GameCard::Layout(int art, bool names, bool info) {
    TextLayout t;
    // Without art the title is the card: always show it.
    names = names || Yuzu::CardArtHidden();
    t.names = names;
    t.meta = info;
    if (!names && !info) {
        return t;
    }
    const QFont base = QGuiApplication::font();
    t.title_px = std::clamp(static_cast<int>(std::sqrt(static_cast<double>(art)) * 1.15), 11, 16);
    t.meta_px = std::clamp(t.title_px - 3, 10, 12);
    t.pad = std::clamp(art / 10, 5, 12);
    QFont title_font = base;
    title_font.setBold(true);
    title_font.setPixelSize(t.title_px);
    QFont meta_font = base;
    meta_font.setPixelSize(t.meta_px);
    const QFontMetrics tfm(title_font);
    const QFontMetrics mfm(meta_font);
    // The card is the art plus 16 px wide (Yuzu::CardWidthMargin minus cardMargin); split the
    // line at its dot when a long one won't fit.
    const int text_w = art + Yuzu::CardWidthMargin - cardMargin - 2 * t.pad;
    QStringList sample_parts;
    if (UISettings::values.show_play_time.GetValue()) {
        sample_parts << QStringLiteral("88.8 h played");
    }
    if (UISettings::values.show_add_ons.GetValue()) {
        sample_parts << QStringLiteral("v8.88.8");
    }
    if (UISettings::values.show_size.GetValue()) {
        sample_parts << QStringLiteral("88.8 GB");
    }
    if (UISettings::values.show_types.GetValue()) {
        sample_parts << QStringLiteral("NSP");
    }
    // As many lines as the parts need at this width; the short play time ("88.8 h") when the
    // long one would take more lines or not fit.
    t.meta_lines = 0;
    if (info) {
        const QStringList full = PackInfo(sample_parts, mfm, text_w);
        QStringList short_parts = sample_parts;
        const bool play = UISettings::values.show_play_time.GetValue();
        if (play) {
            short_parts.first() = QStringLiteral("88.8 h");
        }
        const QStringList shorter = PackInfo(short_parts, mfm, text_w);
        t.short_play = play && !(Fits(full, mfm, text_w) && full.size() <= shorter.size());
        const QStringList& chosen = t.short_play ? short_parts : sample_parts;
        // Assign each part a fixed line by packing the worst-case sample once for the view.
        const bool on[4] = {play, UISettings::values.show_add_ons.GetValue(),
                            UISettings::values.show_size.GetValue(),
                            UISettings::values.show_types.GetValue()};
        const QString dot = QStringLiteral(" \u00B7 ");
        int line = 0;
        QString current;
        int sample = 0;
        for (int kind = 0; kind < 4; ++kind) {
            if (!on[kind]) {
                continue;
            }
            const QString& part = chosen[sample++];
            const QString joined = current.isEmpty() ? part : current + dot + part;
            if (!current.isEmpty() && mfm.horizontalAdvance(joined) > text_w) {
                ++line;
                current = part;
            } else {
                current = joined;
            }
            t.line_of[kind] = line;
        }
        t.meta_lines = line + 1;
    }
    // No bottom padding of its own: the card's inset under the text already gives the room.
    const int title_lines = Yuzu::ScrollLongNames() ? 1 : 2;
    t.height =
        6 + (names ? tfm.lineSpacing() * title_lines + 3 : 0) + t.meta_lines * mfm.lineSpacing();
    return t;
}

int GameCard::HeightMargin(int art, bool names, bool info) {
    // GameCard::paint draws the artwork 11 px shorter than the cell; a text block adds its
    // height plus the 8 px card inset (cardMargin + 2 under it, minus the 2 px art border).
    const TextLayout t = Layout(art, names, info);
    // Hidden art (icon size None): drop the 14 px the artwork would take beyond the icon size.
    const int art_extra = Yuzu::CardArtHidden() ? 11 : 25;
    return art_extra + (t.height > 0 ? t.height + cardMargin : 0);
}

GameCard::GameCard(QObject* parent) : QStyledItemDelegate{parent} {
    setObjectName("GameCard");
}

void GameCard::paint(QPainter* painter, const QStyleOptionViewItem& option,
                     const QModelIndex& index) const {
    if (!index.isValid())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRect cardRect = getCardRect(option, index);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const auto item_type = index.data(GameListItem::TypeRole).value<GameListItemType>();
    const bool add_folder = item_type == GameListItemType::AddDir;
    const bool game_folder = IsGameDirectory(item_type);
    const QPalette palette = option.palette;
    const bool dark = palette.window().color().lightness() < 128;
    const int radius = 10;

    QColor surface = dark ? QColor(30, 31, 33) : QColor(255, 255, 255);
    QColor border = dark ? QColor(53, 54, 57) : QColor(20, 20, 20, 22);
    QColor titleColor = palette.text().color();
    QColor subtitleColor = dark ? QColor(166, 167, 170) : palette.text().color();
    subtitleColor.setAlpha(dark ? 230 : 165);

    if (selected) {
        QColor accent = palette.highlight().color();
        surface = accent;
        surface.setAlpha(dark ? 95 : 55);
        border = palette.highlight().color();
        border.setAlpha(190);
        titleColor = palette.highlightedText().color();
        subtitleColor = palette.highlightedText().color();
        subtitleColor.setAlpha(190);
    } else if (hovered) {
        surface = dark ? QColor(38, 39, 42) : QColor(248, 248, 249);
        border = palette.highlight().color();
        border.setAlpha(dark ? 120 : 100);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(dark ? QColor(0, 0, 0, hovered || selected ? 35 : 22)
                           : QColor(20, 20, 20, hovered || selected ? 18 : 10));
    painter->drawRoundedRect(cardRect.translated(0, selected ? 1 : 2), radius, radius);

    painter->setBrush(surface);
    painter->setPen(QPen(border, 1));
    painter->drawRoundedRect(cardRect, radius, radius);

    const u32 icon_size = UISettings::values.game_cover_size.GetValue();
    const QPixmap icon_pixmap = game_folder || add_folder
                                    ? QPixmap{}
                                    : index.data(GameListItemPath::CardIconRole).value<QPixmap>();
    const bool has_icon = !game_folder && !add_folder &&
                          index.data(GameListItemPath::HasIconRole).toBool();
    const bool folder_cards = option.widget &&
                              option.widget->objectName() == QStringLiteral("gameGridView");
    const bool show_game_name = UISettings::values.show_game_name.GetValue() ||
                                (folder_cards && (game_folder || add_folder));
    const int inset = cardMargin + 2;
    const bool show_info = Yuzu::ShowAnyGameInfo();
    const TextLayout text = Layout(static_cast<int>(Yuzu::CardWidthBase()),
                                   show_game_name, show_info);
    const int titleHeight = text.height;
    const bool art_hidden = Yuzu::CardArtHidden();
    const QRect artworkRect(cardRect.left() + 1, cardRect.top() + 1,
                            std::max(1, cardRect.width() - 2),
                            art_hidden ? 0
                                       : std::max(1, cardRect.height() - titleHeight -
                                                         (titleHeight > 0 ? inset : 2)));
    QPainterPath artwork_clip;
    artwork_clip.addRoundedRect(artworkRect, radius - 1, radius - 1);
    painter->save();
    painter->setClipPath(artwork_clip);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (art_hidden) {
        // Icon size None: text only.
    } else if (add_folder) {
        const QColor accent = selected ? palette.highlightedText().color()
                                       : palette.highlight().color();
        DrawAddFolderArtwork(painter, artworkRect, accent, dark);
    } else if (game_folder) {
        const QColor accent = selected ? palette.highlightedText().color()
                                       : palette.highlight().color();
        DrawGameFolderArtwork(painter, artworkRect, accent, dark);
    } else if (const QPixmap artwork =
                   // Fall back to the small icon when no card artwork is available.
                   !has_icon ? QPixmap{}
                   : icon_pixmap.isNull() ? index.data(Qt::DecorationRole).value<QPixmap>()
                                          : icon_pixmap;
               !artwork.isNull()) {
        QSize scaled = artwork.size();
        scaled.scale(artworkRect.size(), Qt::KeepAspectRatioByExpanding);
        const QRect imageRect(artworkRect.center().x() - scaled.width() / 2,
                              artworkRect.center().y() - scaled.height() / 2, scaled.width(),
                              scaled.height());
        painter->drawPixmap(imageRect, artwork);
    } else {
        LibraryStyles::LibraryStyleContext::PaintPlaceholderArt(painter, artworkRect,
                                                                PlaceholderTitle(index), palette);
    }
    painter->restore();

    // Accent ring around the selected card, drawn over the art so it is never hidden.
    if (selected) {
        QPen ring(LibraryStyles::LibraryStyleContext::Accent(palette), 2.5);
        painter->setPen(ring);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(cardRect).adjusted(-0.75, -0.75, 0.75, 0.75), radius + 0.75,
                                 radius + 0.75);
    }

    if (text.height > 0) {
        const QRect textRect(cardRect.left() + text.pad, artworkRect.bottom() + 1 + 6,
                             cardRect.width() - 2 * text.pad, titleHeight - 12);
        int y = textRect.top();

        QString title;
        if (add_folder) {
            title = tr("Add games folder");
        } else if (game_folder) {
            title = index.data(Qt::DisplayRole).toString();
            if (item_type == GameListItemType::CustomDir) {
                const int dir_index = index.data(GameListDir::GameDirRole).toInt();
                if (dir_index >= 0 && dir_index < UISettings::values.game_dirs.size()) {
                    const QString path =
                        QString::fromStdString(UISettings::values.game_dirs[dir_index].path);
                    const QString name = QFileInfo(path).fileName();
                    if (!name.isEmpty()) {
                        title = name;
                    }
                }
            }
        } else {
            const QString full = index.data(GameListItemPath::TitleRole).toString();
            title = full.isEmpty()
                        ? index.data(Qt::DisplayRole).toString().section(QLatin1Char('\n'), 0, 0)
                        : full;
            if (title.isEmpty()) {
                // No metadata title: show the file name, like the library styles do.
                title = PlaceholderTitle(index);
            }
        }

        if (text.names) {
            QFont titleFont = option.font;
            titleFont.setBold(true);
            titleFont.setPixelSize(text.title_px);
            const QFontMetrics tfm(titleFont);
            const int line = tfm.lineSpacing();
            const int overflow = std::max(0, text.pad - 2);
            if (Yuzu::ScrollLongNames()) {
                // One line. The selected card's long name glides to its end and back
                // (the views repaint the selected card on a timer); others are cut.
                const QRect line_rect(textRect.left(), y, textRect.width(), line);
                const int extra = tfm.horizontalAdvance(title) - line_rect.width();
                if (selected && extra > overflow) {
                    constexpr qint64 pause = 1200;
                    constexpr qint64 ms_per_px = 25;
                    const qint64 travel = extra * ms_per_px;
                    const qint64 t =
                        QDateTime::currentMSecsSinceEpoch() % (2 * (pause + travel));
                    qint64 x = 0;
                    if (t >= pause && t < pause + travel) {
                        x = (t - pause) / ms_per_px;
                    } else if (t >= pause + travel && t < 2 * pause + travel) {
                        x = extra;
                    } else if (t >= 2 * pause + travel) {
                        x = extra - (t - 2 * pause - travel) / ms_per_px;
                    }
                    painter->save();
                    painter->setClipRect(line_rect);
                    painter->setFont(titleFont);
                    painter->setPen(titleColor);
                    painter->drawText(QRect(line_rect.left() - static_cast<int>(x), y,
                                            tfm.horizontalAdvance(title) + 2, line),
                                      Qt::AlignLeft | Qt::AlignTop, title);
                    painter->restore();
                } else {
                    LibraryStyles::LibraryStyleContext::PaintName(
                        painter, line_rect, title, titleColor, titleFont,
                        Qt::AlignLeft | Qt::AlignTop, 1, overflow);
                }
                y += line + 3;
            } else {
                // Two lines at most, cut with "..."; never runs into the info line below.
                LibraryStyles::LibraryStyleContext::PaintName(
                    painter, QRect(textRect.left(), y, textRect.width(), line * 2), title,
                    titleColor, titleFont, Qt::AlignLeft | Qt::AlignTop, 2, overflow);
                y += line * 2 + 3;
            }
        }

        QStringList meta_parts;
        if (add_folder) {
            meta_parts.append(tr("Add a location to your library"));
        } else if (game_folder) {
            meta_parts.append(tr("%n game(s)", "", index.model()->rowCount(index)));
        } else if (show_info) {
            // The parts switched on under View > Game Info.
            meta_parts.append(LibraryStyles::LibraryStyleContext::Info(index).info_line);
        }
        meta_parts.removeAll(QString{});
        if (text.meta && !meta_parts.isEmpty()) {
            QFont metaFont = option.font;
            metaFont.setPixelSize(text.meta_px);
            const QFontMetrics mfm(metaFont);
            painter->setPen(subtitleColor);
            painter->setFont(metaFont);
            // Every part on its fixed line (TextLayout::line_of), so play time, version, size
            // and type sit in the same place on every card; placeholders keep the line_parts filled.
            QStringList lines;
            const auto game = LibraryStyles::LibraryStyleContext::Info(index);
            if (show_info && game.is_game) {
                const QString parts[4] = {
                    text.short_play || game.play_time_seconds == 0
                        ? game.play_time_text
                        : tr("%1 played").arg(game.play_time_text),
                    game.update_version.isEmpty() ? tr("Base")
                                                  : QStringLiteral("v") + game.update_version,
                    game.size_text.isEmpty() ? QStringLiteral("NaN GB") : game.size_text,
                    game.file_type,
                };
                std::vector<QStringList> line_parts(static_cast<size_t>(std::max(1, text.meta_lines)));
                for (int kind = 0; kind < 4; ++kind) {
                    const int line = text.line_of[static_cast<size_t>(kind)];
                    if (line >= 0 && line < static_cast<int>(line_parts.size()) && !parts[kind].isEmpty()) {
                        line_parts[static_cast<size_t>(line)] << parts[kind];
                    }
                }
                for (const QStringList& slot_parts : line_parts) {
                    lines << slot_parts.join(QStringLiteral(" \u00B7 "));
                }
            } else {
                lines << meta_parts.join(QStringLiteral(" \u00B7 "));
            }
            for (const QString& line : lines) {
                painter->drawText(QRect(textRect.left(), y, textRect.width(), mfm.lineSpacing()),
                                  Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                                  mfm.elidedText(line, Qt::ElideRight, textRect.width()));
                y += mfm.lineSpacing();
            }
        }
    }

    painter->restore();
}

QRect GameCard::getCardRect(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int cell_width = option.rect.width();

    const int card_width = cell_width - m_padding;

    int card_left, card_top, card_height;

    if (m_columns >= 1) {
        // grid mode: the cells share the row width, the card keeps its own width centered in
        // its cell. The column is not derived from the row: hidden rows (the favorites row, a
        // search) shift the cells.
        card_left = option.rect.left() + (cell_width - (card_width - cardMargin)) / 2;

        // fill cell vertically
        card_top = option.rect.top() + cardMargin;
        card_height = option.rect.height() - cardMargin - 1;
    } else {
        // carousel mode
        card_left = option.rect.left() + cardMargin + 4;

        // the delegate itself takes up the full height, but the card itself
        // gets centered
        const int content_height = m_contentSize.height() - cardMargin;
        const int cell_height = option.rect.height();

        card_height = std::min(content_height, cell_height - cardMargin * 2) - 1;
        card_top = option.rect.top() + (cell_height - card_height) / 2;
    }

    return QRect(card_left, card_top, card_width - cardMargin, card_height);
}

bool GameCard::hitTest(const QPoint& point, const QModelIndex& index, const QWidget* widget,
                       const QRect& cellRect) const {
    QStyleOptionViewItem option;
    option.initFrom(widget);
    option.rect = cellRect;
    return getCardRect(option, index).contains(point);
}

QSize GameCard::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return m_size;
}

void GameCard::setSize(const QSize& newSize, const QSize& contentSize, const int padding,
                       const int columns) {
    m_size = newSize;
    m_contentSize = contentSize;
    m_padding = padding;
    m_columns = columns;
}
