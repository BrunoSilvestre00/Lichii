// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/model.h"
#include "yuzu/game/common.h"
#include "yuzu/game/game_card.h"
#include "yuzu/game/carousel.h"

GameCarousel::GameCarousel(QWidget* parent) : QListView{parent} {
    m_gameCard = new GameCard(this);
    setItemDelegate(m_gameCard);
    setObjectName(QStringLiteral("gameCarouselView"));

    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setMouseTracking(true);

    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setContextMenuPolicy(Qt::CustomContextMenu);

    setSpacing(12);
    setWordWrap(true);
    setTextElideMode(Qt::ElideRight);
    setFlow(QListView::LeftToRight);
    setWrapping(false);
    setStyleSheet(QStringLiteral(
        "QListView {"
        "  border: none;"
        "  outline: none;"
        "  padding: 8px 10px 12px 10px;"
        "}"));
}

void GameCarousel::SetModel(GameListModel* model) {
    QListView::setModel(model);
    UpdateIconSize();
}

void GameCarousel::ApplyFilter(const QString& edit_filter_text, GameListModel* model) {
    int row_count = model->rowCount();

    for (int i = 0; i < row_count; ++i) {
        QStandardItem* item = model->item(i, 0);
        if (!item)
            continue;

        if (Yuzu::FilterMatches(edit_filter_text, item)) {
            setRowHidden(i, false);
        } else {
            setRowHidden(i, true);
        }
    }
}

void GameCarousel::UpdateIconSize() {
    const bool show_game_name = UISettings::values.show_game_name.GetValue();
    const u32 display_icon_size = Yuzu::CardIconDisplaySize(show_game_name);

    // Same card as the grid: the square art plus a 1 px border, and the title block when names
    // are on. Names never change the art size.
    const int art = static_cast<int>(display_icon_size);
    const int card_w = art + 2;
    const int card_h = show_game_name ? art + GameCard::TitleBlockHeight : art + 2;
    const int gap = 16;

    const int item_width = card_w + GameCard::cardMargin + gap;
    const int content_height = card_h + GameCard::cardMargin + 1;
    const int grid_height = std::max(content_height + GameCard::cardMargin * 2, viewport()->height());

    const QSize content_size(item_width, content_height);
    const QSize grid_size(item_width, grid_height);
    if (gridSize() != grid_size) {
        setUpdatesEnabled(false);

        setGridSize(grid_size);
        m_gameCard->setSize(grid_size, content_size, gap, 0);

        setUpdatesEnabled(true);
    }
    viewport()->update();
}

QModelIndex GameCarousel::indexAt(const QPoint& point) const {
    QModelIndex index = QListView::indexAt(point);
    if (!index.isValid())
        return {};
    if (m_gameCard && !m_gameCard->hitTest(point, index, this, visualRect(index)))
        return {};
    return index;
}
