// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QPersistentModelIndex>
#include <QPointer>

#include "yuzu/game/styles/library_style_view.h"

class QScrollBar;

namespace LibraryStyles {

/**
 * Dense grid: small square covers packed edge to edge, a status bar at the bottom with the selected
 * title and the controls. The option (R / X) puts the names on the tiles or under them. Reference
 * style for the framework, see FRAMEWORK.md.
 */
class DenseGridStyle final : public LibraryStyleView {
    Q_OBJECT
public:
    explicit DenseGridStyle(QWidget* parent = nullptr);

    void SetModel(QAbstractItemModel* model) override;
    QModelIndex CurrentIndex() const override;
    void SetCurrentIndex(const QModelIndex& index) override;
    void SetShowNames(bool show) override;
    void SetShowInfo(bool show) override;
    QString CycleOption() override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    /// Rebuilds the flat list of entries from the model: games of every folder, then the
    /// "Add games folder" row. Favorites are skipped, their games are already in a folder.
    void Rebuild();
    void Relayout();
    void Select(int position);
    void EnsureVisible(int position);
    int PositionAt(const QPoint& point) const;
    QRect TileRect(int position) const;
    QRect GridArea() const;
    int TileSize() const;
    int CellHeight() const;

    QPointer<QAbstractItemModel> model;
    QList<QPersistentModelIndex> entries;
    int current = -1;
    int columns = 1;
    bool show_names = true;
    bool show_info = false;
    bool names_on_tiles = true;
    bool rebuild_pending = false;
    QScrollBar* scrollbar = nullptr;
};

} // namespace LibraryStyles
