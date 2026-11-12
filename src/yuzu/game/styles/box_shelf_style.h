// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>
#include <QCache>
#include <QColor>
#include <QHash>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QPointer>
#include <QRectF>
#include <QString>

#include "yuzu/game/styles/library_style_view.h"

class QAbstractItemModel;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QResizeEvent;
class QWheelEvent;

namespace LibraryStyles {

/// "Box Shelf": games stand as cases on wooden shelves, or as spines only. Concept by inxomnyaa.
class BoxShelfStyle final : public LibraryStyleView {
    Q_OBJECT

public:
    explicit BoxShelfStyle(QWidget* parent = nullptr);
    ~BoxShelfStyle() override;

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
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Metrics {
        int cols = 1;
        double left = 0.0;
        double cell_w = 0.0;
        double case_w = 0.0;
        double case_h = 0.0;
        double row_h = 0.0;
        double top_pad = 0.0;
        double plank_h = 0.0;
        double top_face = 0.0;
        double bubble_h = 0.0;
    };

    Metrics ComputeMetrics() const;
    int ItemCount() const {
        return static_cast<int>(m_rows.size());
    }
    int CurrentPos() const;
    QModelIndex IndexAtPos(int pos) const;
    QRectF ItemRect(const Metrics& m, int pos) const;
    int HitTest(const QPointF& point) const;
    double MaxScroll(const Metrics& m) const;
    void ClampScroll();
    void EnsureVisible(int pos);
    void MoveTo(int pos);

    void ScheduleRebuild();
    void EnsureRows();
    void RebuildRows();

    bool IsDark() const;
    void DrawWall(QPainter& p, bool dark);
    void DrawPlank(QPainter& p, const Metrics& m, int row, bool dark);
    void DrawItem(QPainter& p, const Metrics& m, int pos, bool selected, bool dark);
    void DrawBubble(QPainter& p, const Metrics& m, const QRectF& base, const QString& title,
                    bool selected, bool dark);

    QPixmap CasePixmap(const QModelIndex& index, const Metrics& m, bool dark);
    QPixmap SpinePixmap(const QModelIndex& index, const Metrics& m, bool dark);
    QColor SpineColor(const QModelIndex& index);
    QPixmap WoodPixmap(int w, int h, int top_face, bool dark);

    QPointer<QAbstractItemModel> m_model;
    QPersistentModelIndex m_current;
    std::vector<int> m_rows;       // model row for every shown position
    std::vector<int> m_row_to_pos; // position for every model row, -1 when hidden
    bool m_rebuild_pending = false;

    bool m_show_names = true;
    bool m_show_info = true;
    bool m_spines = false;
    double m_scroll = 0.0;

    QCache<QString, QPixmap> m_pixmaps;
    QHash<QString, QColor> m_colors;
    QHash<QString, QPixmap> m_wood;
};

} // namespace LibraryStyles
