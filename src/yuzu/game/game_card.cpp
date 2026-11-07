// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include "game_card.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"

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
    const QPalette palette = option.palette;
    const bool dark = palette.window().color().lightness() < 128;
    const int radius = 14;

    QColor surface = dark ? QColor(22, 27, 38, 235) : QColor(255, 255, 255, 245);
    QColor border = dark ? QColor(255, 255, 255, 18) : QColor(24, 34, 54, 18);
    QColor titleColor = palette.text().color();
    QColor subtitleColor = palette.text().color();
    subtitleColor.setAlpha(dark ? 150 : 165);

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
        surface = dark ? QColor(30, 37, 52, 245) : QColor(248, 250, 255, 252);
        border = palette.highlight().color();
        border.setAlpha(dark ? 95 : 75);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(dark ? QColor(0, 0, 0, hovered || selected ? 65 : 40)
                           : QColor(20, 30, 60, hovered || selected ? 28 : 18));
    painter->drawRoundedRect(cardRect.translated(0, selected ? 2 : 3), radius, radius);

    painter->setBrush(surface);
    painter->setPen(QPen(border, 1));
    painter->drawRoundedRect(cardRect, radius, radius);

    const u32 icon_size = UISettings::values.game_icon_size.GetValue();
    const QPixmap icon_pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
    const int inset = cardMargin + 2;
    const int titleHeight = UISettings::values.show_game_name.GetValue() ? 62 : 0;
    QRect iconArea(cardRect.left() + inset, cardRect.top() + inset, cardRect.width() - inset * 2,
                   cardRect.height() - inset * 2 - titleHeight);
    iconArea.setHeight(std::max(1, iconArea.height()));

    if (!icon_pixmap.isNull()) {
        QSize scaled = icon_pixmap.size();
        scaled.scale(iconArea.size(), Qt::KeepAspectRatio);
        QRect iconRect(iconArea.left() + (iconArea.width() - scaled.width()) / 2,
                       iconArea.top() + (iconArea.height() - scaled.height()) / 2, scaled.width(),
                       scaled.height());
        const int imageRadius = std::max(8, radius - 4);
        QPainterPath clip_path;
        clip_path.addRoundedRect(iconRect, imageRadius, imageRadius);
        painter->save();
        painter->setClipPath(clip_path);
        painter->drawPixmap(iconRect, icon_pixmap);
        painter->restore();

        painter->setPen(QPen(QColor(255, 255, 255, dark ? 14 : 24), 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(iconRect.adjusted(0, 0, -1, -1), imageRadius, imageRadius);
    } else {
        QRect placeholder = iconArea;
        placeholder.setHeight(std::min(placeholder.height(), placeholder.width()));
        placeholder.moveTop(iconArea.top());
        painter->setPen(Qt::NoPen);
        painter->setBrush(dark ? QColor(255, 255, 255, 10) : QColor(0, 0, 0, 8));
        painter->drawRoundedRect(placeholder, std::max(8, radius - 4), std::max(8, radius - 4));
    }

    if (UISettings::values.show_game_name.GetValue()) {
        QRect textRect = cardRect.adjusted(inset, 0, -inset, -inset);
        textRect.setTop(cardRect.bottom() - inset - titleHeight + 2);

        const QString full = index.data(GameListItemPath::TitleRole).toString();
        const QString title = full.isEmpty()
                                  ? index.data(Qt::DisplayRole).toString().section(QLatin1Char('\n'), 0, 0)
                                  : full;

        painter->setPen(titleColor);
        QFont titleFont = option.font;
        titleFont.setBold(true);
        titleFont.setPixelSize(std::clamp(static_cast<int>(std::sqrt(static_cast<double>(icon_size)) * 1.15), 12, 16));
        painter->setFont(titleFont);
        QRect titleRect = textRect;
        titleRect.setBottom(textRect.top() + titleHeight - 20);
        painter->drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, title);

        QStringList meta_parts;
        const QString size_text = index.sibling(index.row(), GameListModel::COLUMN_SIZE)
                                      .data(Qt::DisplayRole)
                                      .toString();
        if (!size_text.isEmpty()) {
            meta_parts.append(size_text);
        }
        const QString file_type = index.data(GameListItemPath::FileTypeRole).toString();
        if (!file_type.isEmpty()) {
            meta_parts.append(file_type);
        }
        if (!meta_parts.isEmpty()) {
            const QString meta = meta_parts.join(QStringLiteral(" \u2022 "));
            painter->setPen(subtitleColor);
            QFont metaFont = option.font;
            metaFont.setPixelSize(12);
            painter->setFont(metaFont);
            QRect metaRect = textRect;
            metaRect.setTop(textRect.bottom() - 18);
            painter->drawText(metaRect, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine, meta);
        }
    }

    painter->restore();
}

QRect GameCard::getCardRect(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int cell_width = option.rect.width();

    const int card_width = cell_width - m_padding;

    int card_left, card_top, card_height;

    if (m_columns >= 1) {
        // grid mode

        // center everything in-line, such that the leftmost and rightmost cards
        // have ~ equal padding to the edge of the viewport
        // spacing between each card is larger, but equal to each other
        const int column = index.row() % m_columns;

        const int row_width = m_columns * cell_width;
        const int total_gap = row_width - cardMargin * 2 - m_columns * card_width;
        const int gap = (m_columns > 1) ? (total_gap / (m_columns - 1)) : 0;
        card_left =
            option.rect.left() - column * cell_width + cardMargin + column * (card_width + gap) + 4;

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
