// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPersistentModelIndex>
#include <QPointer>
#include <QRect>
#include <QString>
#include <vector>

#include "yuzu/game/styles/library_style_view.h"

class QAbstractItemModel;

namespace LibraryStyles {

/// Canvas library style: rounded cover tiles over soft concentric colour arcs.
/// Concept by inxomnyaa. Only the visible tiles are painted, so large libraries stay smooth.
class CanvasStyle : public LibraryStyleView {
    Q_OBJECT
public:
    explicit CanvasStyle(QWidget* parent = nullptr);
    ~CanvasStyle() override;

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
    void changeEvent(QEvent* event) override;

private:
    struct Layout {
        int columns = 1;
        int tile = 128;
        int gap = 18;
        int margin = 28;
        int top = 28;
    };

    void ScheduleRebuild();
    void Rebuild();
    void UpdateLayout();
    Layout ComputeLayout() const;
    QRect TileRect(int row) const;
    int RowAt(const QPoint& pos) const;
    void SelectRow(int row, bool notify = true);
    void EnsureVisible(int row);
    void SetScroll(int value);
    int MaxScroll() const;
    int FooterHeight() const;
    QModelIndex IndexAt(int row) const;

    QPointer<QAbstractItemModel> model;
    std::vector<QPersistentModelIndex> entries;
    Layout layout;
    bool rebuild_pending = false;
    int current = -1;
    int scroll = 0;
    bool show_names = true;
    bool show_info = false;
};

} // namespace LibraryStyles
