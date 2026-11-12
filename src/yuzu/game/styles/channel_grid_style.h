// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCache>
#include <QColor>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <vector>

#include "yuzu/game/styles/library_style_view.h"

class QAbstractItemModel;
class QContextMenuEvent;

namespace LibraryStyles {

/// "Channels" library style: wide cover tiles on a soft background with a bottom bar that shows
/// the game count, the clock and the selected game. Concept by inxomnyaa.
class ChannelGridStyle final : public LibraryStyleView {
    Q_OBJECT

public:
    explicit ChannelGridStyle(QWidget* parent = nullptr);
    ~ChannelGridStyle() override;

    void SetModel(QAbstractItemModel* model) override;
    QModelIndex CurrentIndex() const override;
    void SetCurrentIndex(const QModelIndex& index) override;
    void SetShowNames(bool show) override;
    void SetShowInfo(bool show) override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Layout {
        int columns = 1;
        int margin = 24;
        int cell_w = 1;
        int tile_w = 1;
        int tile_h = 1;
        int row_h = 1;
        int top = 24;
        int view_h = 1;
        int dock_h = 1;
        int dock_top = 1;
        int curve = 1;
    };

    Layout ComputeLayout() const;
    int RowCount() const;
    int CurrentRow() const;
    QRect TileRect(const Layout& layout, int row) const;
    int RowAt(const QPoint& pos) const;
    int MaxScroll(const Layout& layout) const;
    void ClampScroll();
    void EnsureVisible(int row);
    void SetCurrentRow(int row, bool notify);
    QModelIndex IndexForRow(int row) const;
    void ScheduleRebuild();
    void Rebuild();
    void ScheduleClock();

    /// Cover art cropped to `size` with rounded corners (or the default art), cached.
    QPixmap CoverFor(const QModelIndex& index, const QSize& size, qreal radius, qreal dpr);
    void PaintTile(QPainter& painter, const Layout& layout, int row, bool selected);
    void PaintDock(QPainter& painter, const Layout& layout);

    QPointer<QAbstractItemModel> model;
    std::vector<QPersistentModelIndex> entries; // flattened games, same set as the other styles
    QPersistentModelIndex current;
    bool rebuild_pending = false;
    int current_hint = 0;
    QTimer clock_timer;
    QCache<QString, QPixmap> cover_cache{48 * 1024}; // cost in KiB
    int scroll = 0;
    QPixmap background;
    QSize background_size;
    qreal background_dpr = 0;
    QRgb background_base = 0;
    bool show_names = true;
    bool show_info = true;
};

} // namespace LibraryStyles
