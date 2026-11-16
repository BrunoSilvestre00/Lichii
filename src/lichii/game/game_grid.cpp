// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QScroller>
#include <QScrollerProperties>

#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/model.h"
#include "lichii/game/common.h"
#include "lichii/game/game_card.h"
#include "lichii/game/game_grid.h"

GameGrid::GameGrid(QWidget* parent) : QListView{parent} {
    m_gameCard = new GameCard(this);
    setItemDelegate(m_gameCard);

    setViewMode(QListView::ListMode);
    setResizeMode(QListView::Fixed);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setContextMenuPolicy(Qt::CustomContextMenu);

    setSpacing(10);
    setWordWrap(true);
    setTextElideMode(Qt::ElideRight);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
}

void GameGrid::SetModel(GameListModel* model) {
    QListView::setModel(model);
    UpdateIconSize();
}

void GameGrid::ApplyFilter(const QString& edit_filter_text, GameListModel* model) {
    last_filter_text = edit_filter_text;
    int row_count = model->rowCount();

    for (int i = 0; i < row_count; ++i) {
        QStandardItem* item = model->item(i, 0);
        if (!item)
            continue;

        bool visible = Lichii::FilterMatches(edit_filter_text, item);
        if (browsed_dir >= 0) {
            const auto type = item->data(GameListItem::TypeRole).value<GameListItemType>();
            if (type == GameListItemType::Game) {
                visible = item->data(GameListItemPath::OwnerDirRole).toInt() == browsed_dir &&
                          Lichii::FilterMatches(edit_filter_text, item);
            } else if (type == GameListItemType::CustomDir ||
                       type == GameListItemType::AddDir) {
                // Folder cards stay visible so another folder can be opened or the browsed one
                // left again.
                visible = true;
            } else {
                visible = false;
            }
        }

        setRowHidden(i, !visible);
    }

    HighlightBrowsedFolder(model);
}

void GameGrid::HighlightBrowsedFolder(GameListModel* model) {
    QModelIndex current;
    if (browsed_dir >= 0) {
        for (int i = 0; i < model->rowCount(); ++i) {
            QStandardItem* item = model->item(i, 0);
            if (!item) {
                continue;
            }
            if (item->data(GameListItem::TypeRole).value<GameListItemType>() ==
                    GameListItemType::CustomDir &&
                item->data(GameListDir::GameDirRole).toInt() == browsed_dir) {
                current = item->index();
                break;
            }
        }
    }

    if (current.isValid()) {
        setCurrentIndex(current);
        scrollTo(current);
    } else {
        clearSelection();
        setCurrentIndex({});
    }
}

void GameGrid::SetBrowsedDirectory(int dir_index) {
    if (browsed_dir == dir_index) {
        return;
    }
    browsed_dir = dir_index;
    if (auto* model = qobject_cast<GameListModel*>(QListView::model())) {
        ApplyFilter(last_filter_text, model);
    }
}

void GameGrid::BrowseDirectory(int dir_index) {
    SetBrowsedDirectory(browsed_dir == dir_index ? -1 : dir_index);
}

void GameGrid::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && browsed_dir >= 0) {
        SetBrowsedDirectory(-1);
        return;
    }
    QListView::keyPressEvent(event);
}

void GameGrid::UpdateIconSize() {
    const u32 icon_size = UISettings::values.game_cover_size.GetValue();

    int heightMargin = 0;
    // The card hugs the icon; the title wraps to the icon's width, so extra width is only
    // needed for the "no icon" case. Leftover row space becomes the gap between cards.
    int widthMargin = 24;

    if (UISettings::values.show_game_name) {
        switch (icon_size) {
        case 128:
        case 160:
            heightMargin = 65;
            break;
        case 0:
            widthMargin = 120;
            heightMargin = 120;
            break;
        case 44:
        case 64:
            heightMargin = 77;
            break;
        case 32:
        case 256:
            heightMargin = 81;
            break;
        }
    } else {
        widthMargin = 24;
        heightMargin = 24;
    }

    const int view_width = viewport()->width();

    const double spacing = 0.01;
    const int min_item_width = icon_size + widthMargin;

    int columns = std::max(1, (view_width - 16) / min_item_width);
    int stretched_width = ((view_width) - (spacing * (columns - 1))) / columns;

    QSize grid_size(stretched_width, icon_size + heightMargin);
    if (gridSize() != grid_size) {
        setUpdatesEnabled(false);

        setGridSize(grid_size);
        m_gameCard->setSize(grid_size, grid_size, stretched_width - min_item_width, columns);

        setUpdatesEnabled(true);
    }
}

QModelIndex GameGrid::indexAt(const QPoint& point) const {
    QModelIndex index = QListView::indexAt(point);
    if (!index.isValid())
        return {};
    if (m_gameCard && !m_gameCard->hitTest(point, index, this, visualRect(index)))
        return {};
    return index;
}
