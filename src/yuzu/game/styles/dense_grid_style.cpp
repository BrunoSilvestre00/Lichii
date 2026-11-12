// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Layout from the "Tinfoil" library concept by inxomnyaa.

#include <algorithm>

#include <QAbstractItemModel>
#include <QContextMenuEvent>
#include <QFontInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStringList>
#include <QTimer>
#include <QWheelEvent>

#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "yuzu/game/styles/dense_grid_style.h"
#include "yuzu/game/styles/style_helpers.h"

namespace LibraryStyles {

namespace {

constexpr int Margin = 12;
constexpr int Gap = 6;
constexpr int BarHeight = 44;
constexpr int Radius = 4;

bool IsFolder(GameListItemType type) {
    return type == GameListItemType::CustomDir || type == GameListItemType::SdmcDir ||
           type == GameListItemType::UserNandDir || type == GameListItemType::SysNandDir;
}

LibraryStyleView* Make(QWidget* parent) {
    return new DenseGridStyle(parent);
}

} // namespace

[[maybe_unused]] static const bool registered =
    LibraryStyles::Register({"dense_grid", QT_TR_NOOP("Dense grid"), &Make});

DenseGridStyle::DenseGridStyle(QWidget* parent) : LibraryStyleView{parent} {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    scrollbar = new QScrollBar(Qt::Vertical, this);
    scrollbar->setSingleStep(24);
    connect(scrollbar, &QScrollBar::valueChanged, this, [this] { update(); });
}

void DenseGridStyle::SetModel(QAbstractItemModel* new_model) {
    if (model) {
        disconnect(model, nullptr, this, nullptr);
    }
    model = new_model;
    if (model) {
        // Population adds rows one by one: rebuild once per event loop pass.
        const auto schedule = [this] {
            QTimer::singleShot(0, this, &DenseGridStyle::Rebuild);
        };
        connect(model, &QAbstractItemModel::modelReset, this, schedule);
        connect(model, &QAbstractItemModel::layoutChanged, this, schedule);
        connect(model, &QAbstractItemModel::rowsInserted, this, schedule);
        connect(model, &QAbstractItemModel::rowsRemoved, this, schedule);
        connect(model, &QAbstractItemModel::rowsMoved, this, schedule);
        connect(model, &QAbstractItemModel::dataChanged, this, [this] { update(); });
    }
    Rebuild();
}

QModelIndex DenseGridStyle::CurrentIndex() const {
    if (current < 0 || current >= entries.size()) {
        return {};
    }
    return entries[current];
}

void DenseGridStyle::SetCurrentIndex(const QModelIndex& index) {
    // A rebuild may still be queued after the model changed: try once more after running it.
    for (int attempt = 0; attempt < 2; ++attempt) {
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i] == index) {
                Select(i);
                return;
            }
        }
        Rebuild();
    }
}

void DenseGridStyle::SetShowNames(bool show) {
    show_names = show;
    Relayout();
}

void DenseGridStyle::SetShowInfo(bool show) {
    show_info = show;
    update();
}

QString DenseGridStyle::CycleOption() {
    names_on_tiles = !names_on_tiles;
    Relayout();
    return names_on_tiles ? tr("Layout: names on tiles") : tr("Layout: names under tiles");
}

void DenseGridStyle::Rebuild() {
    const QPersistentModelIndex previous = CurrentIndex();
    entries.clear();
    QPersistentModelIndex add_folder;
    if (model) {
        const auto add_games = [this](const QModelIndex& parent) {
            for (int row = 0; row < model->rowCount(parent); ++row) {
                const QModelIndex index = model->index(row, 0, parent);
                if (index.data(GameListItem::TypeRole).value<GameListItemType>() ==
                    GameListItemType::Game) {
                    entries.append(index);
                }
            }
        };
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            const auto type = index.data(GameListItem::TypeRole).value<GameListItemType>();
            if (IsFolder(type)) {
                add_games(index);
            } else if (type == GameListItemType::Game) {
                entries.append(index);
            } else if (type == GameListItemType::AddDir) {
                add_folder = index;
            }
        }
    }
    if (add_folder.isValid()) {
        entries.append(add_folder);
    }

    current = -1;
    for (int i = 0; previous.isValid() && i < entries.size(); ++i) {
        if (entries[i] == previous) {
            current = i;
            break;
        }
    }
    if (current < 0 && !entries.isEmpty()) {
        current = 0;
    }
    Relayout();
    if (QPersistentModelIndex(CurrentIndex()) != previous) {
        emit CurrentChanged(CurrentIndex());
    }
}

int DenseGridStyle::TileSize() const {
    // Same art size as every other style; this one is dense through its tight gaps.
    return LibraryStyleContext::ArtSize();
}

QFont DenseGridStyle::TileFont() const {
    // Small tiles get small text: about 11 px on a 128 px tile, never above the UI font.
    QFont f = font();
    const int ui_px = QFontInfo(f).pixelSize();
    f.setPixelSize(std::clamp(TileSize() / 12, 9, std::max(9, ui_px)));
    return f;
}

int DenseGridStyle::CellHeight() const {
    const int tile = TileSize();
    if (!show_names || names_on_tiles) {
        return tile;
    }
    return tile + 4 + QFontMetrics(TileFont()).lineSpacing() * 2;
}

QRect DenseGridStyle::GridArea() const {
    return QRect(0, 0, width() - scrollbar->sizeHint().width(), std::max(0, height() - BarHeight));
}

void DenseGridStyle::Relayout() {
    const QRect area = GridArea();
    const int tile = TileSize();
    columns = std::max(1, (area.width() - Margin * 2 + Gap) / (tile + Gap));
    const int rows = (static_cast<int>(entries.size()) + columns - 1) / columns;
    const int content = Margin * 2 + rows * CellHeight() + std::max(0, rows - 1) * Gap;

    scrollbar->setGeometry(area.right() + 1, 0, scrollbar->sizeHint().width(), area.height());
    scrollbar->setPageStep(area.height());
    scrollbar->setRange(0, std::max(0, content - area.height()));
    scrollbar->setVisible(scrollbar->maximum() > 0);
    EnsureVisible(current);
    update();
}

QRect DenseGridStyle::TileRect(int position) const {
    const int tile = TileSize();
    const int column = position % columns;
    const int row = position / columns;
    return QRect(Margin + column * (tile + Gap),
                 Margin + row * (CellHeight() + Gap) - scrollbar->value(), tile, tile);
}

int DenseGridStyle::PositionAt(const QPoint& point) const {
    if (!GridArea().contains(point)) {
        return -1;
    }
    for (int i = 0; i < entries.size(); ++i) {
        QRect cell = TileRect(i);
        cell.setHeight(CellHeight());
        if (cell.contains(point)) {
            return i;
        }
    }
    return -1;
}

void DenseGridStyle::Select(int position) {
    if (entries.isEmpty()) {
        return;
    }
    position = std::clamp(position, 0, static_cast<int>(entries.size()) - 1);
    if (position == current) {
        return;
    }
    current = position;
    EnsureVisible(current);
    update();
    emit CurrentChanged(CurrentIndex());
}

void DenseGridStyle::EnsureVisible(int position) {
    if (position < 0 || position >= entries.size()) {
        return;
    }
    const QRect area = GridArea();
    QRect cell = TileRect(position);
    cell.setHeight(CellHeight());
    if (cell.top() < Margin) {
        scrollbar->setValue(scrollbar->value() + cell.top() - Margin);
    } else if (cell.bottom() > area.bottom() - Margin) {
        scrollbar->setValue(scrollbar->value() + cell.bottom() - area.bottom() + Margin);
    }
}

void DenseGridStyle::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QPalette& pal = palette();
    const QRect area = GridArea();

    painter.save();
    painter.setClipRect(area);
    const QFont tile_font = TileFont();
    const QFontMetrics tile_metrics(tile_font);
    for (int i = 0; i < entries.size(); ++i) {
        const QRect tile = TileRect(i);
        if (tile.bottom() + CellHeight() < 0 || tile.top() > area.bottom()) {
            continue;
        }
        const QModelIndex index = entries[i];
        LibraryStyleContext::PaintCover(&painter, tile, index, pal, Radius);
        if (show_names && names_on_tiles) {
            LibraryStyleContext::PaintCoverCaption(&painter, tile, index, tile_font,
                                                   show_info && i == current);
        } else if (show_names) {
            const QRect name(tile.left(), tile.bottom() + 4, tile.width(),
                             tile_metrics.lineSpacing() * 2);
            LibraryStyleContext::PaintName(&painter, name, LibraryStyleContext::Title(index),
                                           pal.text().color(), tile_font,
                                           Qt::AlignHCenter | Qt::AlignTop);
        }
        if (i == current) {
            LibraryStyleContext::PaintSelection(&painter, tile, pal, Radius, 3);
        }
    }
    painter.restore();
    const QFontMetrics metrics(font());

    // Status bar: selected title (and its info line) on the left, the controls on the right.
    const QRect bar(0, area.bottom() + 1, width(), BarHeight);
    QColor line = pal.text().color();
    line.setAlpha(40);
    painter.setPen(line);
    painter.drawLine(bar.topLeft(), bar.topRight());

    const QStringList hints{tr("A Launch"), tr("B Names"), tr("Y Info"), tr("X Layout")};
    const QString hint_text = hints.join(QStringLiteral("    "));
    const int hint_width = metrics.horizontalAdvance(hint_text);
    painter.setPen(LibraryStyleContext::MutedText(pal));
    painter.drawText(bar.adjusted(Margin, 0, -Margin, 0), Qt::AlignRight | Qt::AlignVCenter,
                     hint_text);

    const QModelIndex selected = CurrentIndex();
    if (selected.isValid()) {
        // The bar follows the same toggles as the tiles: no names, no title down here either.
        const GameInfo info = LibraryStyleContext::Info(selected);
        QStringList parts;
        if (show_names) {
            parts.append(info.title);
        }
        if (show_info && !info.info_line.isEmpty()) {
            parts.append(info.info_line);
        }
        const QString text = parts.join(QStringLiteral("    "));
        QFont bold = font();
        bold.setBold(true);
        const QRect title_rect(bar.left() + Margin, bar.top(),
                               std::max(0, bar.width() - Margin * 3 - hint_width), bar.height());
        LibraryStyleContext::PaintName(&painter, title_rect, text, pal.text().color(), bold,
                                       Qt::AlignLeft | Qt::AlignVCenter, 1);
    }
}

void DenseGridStyle::resizeEvent(QResizeEvent* event) {
    LibraryStyleView::resizeEvent(event);
    Relayout();
}

void DenseGridStyle::keyPressEvent(QKeyEvent* event) {
    if (entries.isEmpty()) {
        LibraryStyleView::keyPressEvent(event);
        return;
    }
    const int rows_per_page = std::max(1, GridArea().height() / (CellHeight() + Gap));
    switch (event->key()) {
    case Qt::Key_Left:
        Select(current - 1);
        break;
    case Qt::Key_Right:
        Select(current + 1);
        break;
    case Qt::Key_Up:
        Select(current - columns);
        break;
    case Qt::Key_Down:
        Select(current + columns);
        break;
    case Qt::Key_PageUp:
        Select(current - columns * rows_per_page);
        break;
    case Qt::Key_PageDown:
        Select(current + columns * rows_per_page);
        break;
    case Qt::Key_Home:
        Select(0);
        break;
    case Qt::Key_End:
        Select(static_cast<int>(entries.size()) - 1);
        break;
    default:
        LibraryStyleView::keyPressEvent(event);
        return;
    }
    event->accept();
}

void DenseGridStyle::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    const int position = PositionAt(event->position().toPoint());
    if (position >= 0) {
        Select(position);
    }
    LibraryStyleView::mousePressEvent(event);
}

void DenseGridStyle::mouseDoubleClickEvent(QMouseEvent* event) {
    const int position = PositionAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && position >= 0) {
        Select(position);
        emit LaunchRequested(CurrentIndex());
        return;
    }
    LibraryStyleView::mouseDoubleClickEvent(event);
}

void DenseGridStyle::contextMenuEvent(QContextMenuEvent* event) {
    const int position = PositionAt(event->pos());
    if (position >= 0) {
        Select(position);
    }
    emit ContextMenuRequested(position >= 0 ? QModelIndex{entries[position]} : QModelIndex{},
                              event->globalPos());
}

void DenseGridStyle::wheelEvent(QWheelEvent* event) {
    scrollbar->setValue(scrollbar->value() - event->angleDelta().y());
    event->accept();
}

} // namespace LibraryStyles
