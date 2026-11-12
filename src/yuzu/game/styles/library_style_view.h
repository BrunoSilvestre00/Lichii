// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <QModelIndex>
#include <QPoint>
#include <QWidget>
#include <vector>
class QAbstractItemModel;
namespace LibraryStyles {
class LibraryStyleView : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    ~LibraryStyleView() override = default;
    // The game list model the Modern grid uses (filtered); rows are games/folders with the roles from qt_common/game_list/game_list_p.h.
    virtual void SetModel(QAbstractItemModel* model) = 0;
    virtual QModelIndex CurrentIndex() const = 0;
    virtual void SetCurrentIndex(const QModelIndex& index) = 0;
    virtual void SetShowNames(bool show) = 0;
    virtual void SetShowInfo(bool show) = 0;
    // The style's own option (R / X), e.g. scattered on/off. Returns a short label for the current option or empty.
    virtual QString CycleOption() { return {}; }
signals:
    void LaunchRequested(const QModelIndex& index);
    void ContextMenuRequested(const QModelIndex& index, const QPoint& global_pos);
    void CurrentChanged(const QModelIndex& index);
};
using Factory = LibraryStyleView* (*)(QWidget* parent);
struct StyleInfo { const char* id; const char* display_name; Factory factory; };
bool Register(const StyleInfo& info);
const std::vector<StyleInfo>& All();
} // namespace LibraryStyles
