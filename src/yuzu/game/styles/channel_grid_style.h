// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCache>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QTimer>

#include "yuzu/game/styles/library_style_view.h"

class QAbstractItemModel;

namespace LibraryStyles {

/// "Channels" library style: wide cover tiles on a soft background with a curved bottom dock
/// that shows the clock, the selected game and the app name. Concept by inxomnyaa.
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
    QRect TileRect(const Layout& layout, int row) const;
    int RowAt(const QPoint& pos) const;
    int MaxScroll(const Layout& layout) const;
    void ClampScroll();
    void EnsureVisible(int row);
    void SetCurrentRow(int row, bool notify);
    QModelIndex IndexForRow(int row) const;
    void OnModelChanged();
    void ScheduleClock();

    QPixmap CoverFor(const QModelIndex& index, const QSize& size, qreal dpr);
    void PaintTile(QPainter& painter, const Layout& layout, int row, bool selected);
    void PaintDock(QPainter& painter, const Layout& layout);

    QPointer<QAbstractItemModel> model;
    QPersistentModelIndex current;
    QTimer clock_timer;
    QCache<QString, QPixmap> cover_cache{48 * 1024}; // cost in KiB
    int scroll = 0;
    bool show_names = true;
    bool show_info = true;
};

} // namespace LibraryStyles
