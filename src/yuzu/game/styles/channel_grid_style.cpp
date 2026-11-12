// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Channels library style. Concept by inxomnyaa.

#include <algorithm>
#include <cmath>

#include <QAbstractItemModel>
#include <QColor>
#include <QContextMenuEvent>
#include <QDate>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QLocale>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QStringList>
#include <QTimer>
#include <QTime>
#include <QWheelEvent>

#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/game/styles/channel_grid_style.h"
#include "yuzu/game/styles/style_helpers.h"

namespace LibraryStyles {

namespace {

constexpr int Gap = 22;
constexpr int NameHeight = 30;

bool IsDark(const QPalette& palette) {
    return palette.window().color().lightness() < 128;
}

int ItemType(const QModelIndex& index) {
    return index.data(GameListItem::TypeRole).toInt();
}

bool IsAddFolder(const QModelIndex& index) {
    return ItemType(index) == static_cast<int>(GameListItemType::AddDir);
}

bool IsFolder(const QModelIndex& index) {
    const int type = ItemType(index);
    return type == static_cast<int>(GameListItemType::CustomDir) ||
           type == static_cast<int>(GameListItemType::SdmcDir) ||
           type == static_cast<int>(GameListItemType::UserNandDir) ||
           type == static_cast<int>(GameListItemType::SysNandDir);
}

bool IsGame(const QModelIndex& index) {
    return ItemType(index) == static_cast<int>(GameListItemType::Game);
}

QString TitleOf(const QModelIndex& index) {
    if (IsAddFolder(index)) {
        return QObject::tr("Add games folder");
    }
    if (!IsGame(index)) {
        return index.data(Qt::DisplayRole).toString();
    }
    const QString full = index.data(GameListItemPath::TitleRole).toString();
    return full.isEmpty() ? index.data(Qt::DisplayRole).toString().section(QLatin1Char('\n'), 0, 0)
                          : full;
}

} // namespace

ChannelGridStyle::ChannelGridStyle(QWidget* parent) : LibraryStyleView(parent) {
    setFocusPolicy(Qt::StrongFocus);
    clock_timer.setSingleShot(true);
    connect(&clock_timer, &QTimer::timeout, this, [this] {
        update();
        ScheduleClock();
    });
    ScheduleClock();
}

ChannelGridStyle::~ChannelGridStyle() = default;

void ChannelGridStyle::ScheduleClock() {
    // Fire right after the next minute starts so the clock never shows a stale minute.
    const QTime now = QTime::currentTime();
    const int ms = (60 - now.second()) * 1000 - now.msec() + 50;
    clock_timer.start(std::clamp(ms, 100, 61000));
}

void ChannelGridStyle::SetModel(QAbstractItemModel* new_model) {
    if (model) {
        model->disconnect(this);
    }
    model = new_model;
    current = QPersistentModelIndex{};
    entries.clear();
    scroll = 0;
    cover_cache.clear();
    if (model) {
        const auto rebuild = [this] { ScheduleRebuild(); };
        connect(model, &QAbstractItemModel::modelReset, this, rebuild);
        connect(model, &QAbstractItemModel::layoutChanged, this, rebuild);
        connect(model, &QAbstractItemModel::rowsInserted, this, rebuild);
        connect(model, &QAbstractItemModel::rowsRemoved, this, rebuild);
        connect(model, &QAbstractItemModel::rowsMoved, this, rebuild);
        connect(model, &QAbstractItemModel::dataChanged, this, [this] {
            cover_cache.clear();
            update();
        });
    }
    Rebuild();
}

void ChannelGridStyle::ScheduleRebuild() {
    if (rebuild_pending) {
        return;
    }
    rebuild_pending = true;
    QTimer::singleShot(0, this, [this] {
        rebuild_pending = false;
        Rebuild();
    });
}

void ChannelGridStyle::Rebuild() {
    // Games are shown flat: top level games plus the games inside each folder row.
    // Favorites and the "add folder" row are not tiles.
    const QModelIndex before{current};
    entries.clear();
    if (model) {
        const int top_rows = model->rowCount();
        for (int r = 0; r < top_rows; ++r) {
            const QModelIndex idx = model->index(r, 0);
            if (IsGame(idx)) {
                entries.emplace_back(idx);
            } else if (IsFolder(idx)) {
                const int children = model->rowCount(idx);
                for (int c = 0; c < children; ++c) {
                    const QModelIndex child = model->index(c, 0, idx);
                    if (IsGame(child)) {
                        entries.emplace_back(child);
                    }
                }
            }
        }
    }
    const int count = static_cast<int>(entries.size());
    if (count == 0) {
        current = QPersistentModelIndex{};
    } else if (CurrentRow() < 0) {
        // Never set, or the row went away: stay near the last known position.
        current = entries[static_cast<size_t>(std::clamp(current_hint, 0, count - 1))];
    }
    cover_cache.clear();
    ClampScroll();
    if (QModelIndex{current} != before) {
        emit CurrentChanged(QModelIndex{current});
    }
    update();
}

int ChannelGridStyle::RowCount() const {
    return static_cast<int>(entries.size());
}

QModelIndex ChannelGridStyle::IndexForRow(int row) const {
    return (row >= 0 && row < RowCount()) ? QModelIndex{entries[static_cast<size_t>(row)]}
                                           : QModelIndex{};
}

int ChannelGridStyle::CurrentRow() const {
    if (!current.isValid()) {
        return -1;
    }
    const auto it = std::find(entries.begin(), entries.end(), current);
    return it == entries.end() ? -1 : static_cast<int>(it - entries.begin());
}

QModelIndex ChannelGridStyle::CurrentIndex() const {
    return QModelIndex{current};
}

void ChannelGridStyle::SetCurrentIndex(const QModelIndex& index) {
    if (!model || !index.isValid() || index.model() != model) {
        return;
    }
    const QPersistentModelIndex wanted{index.sibling(index.row(), 0)};
    const auto it = std::find(entries.begin(), entries.end(), wanted);
    if (it != entries.end()) {
        SetCurrentRow(static_cast<int>(it - entries.begin()), false);
    }
}

void ChannelGridStyle::SetShowNames(bool show) {
    show_names = show;
    ClampScroll();
    update();
}

void ChannelGridStyle::SetShowInfo(bool show) {
    show_info = show;
    update();
}

ChannelGridStyle::Layout ChannelGridStyle::ComputeLayout() const {
    Layout l;
    const int w = std::max(1, width());
    const int h = std::max(1, height());
    l.margin = std::clamp(w / 40, 12, 48);
    // Wide channel tiles: as tall as View > Game Icon Size, 16:10. Extra width spreads the
    // columns instead of growing the tiles.
    const int avail = std::max(1, w - 2 * l.margin);
    l.tile_w = std::min(LibraryStyleContext::ArtSize() * 16 / 10, std::max(1, avail - Gap));
    l.tile_h = std::max(1, l.tile_w * 10 / 16);
    l.columns = std::max(1, avail / (l.tile_w + Gap));
    l.cell_w = std::max(1, avail / l.columns);
    l.row_h = l.tile_h + Gap + (show_names ? NameHeight : 0);
    l.top = l.margin;
    l.dock_h = std::clamp(h / 5, 110, 200);
    l.curve = l.dock_h * 14 / 100;
    l.dock_top = h - l.dock_h;
    l.view_h = std::max(1, l.dock_top);
    return l;
}

int ChannelGridStyle::MaxScroll(const Layout& l) const {
    const int rows = (RowCount() + l.columns - 1) / l.columns;
    const int content = l.top + rows * l.row_h + l.curve;
    return std::max(0, content - l.view_h);
}

void ChannelGridStyle::ClampScroll() {
    scroll = std::clamp(scroll, 0, MaxScroll(ComputeLayout()));
}

QRect ChannelGridStyle::TileRect(const Layout& l, int row) const {
    const int col = row % l.columns;
    const int r = row / l.columns;
    return QRect(l.margin + col * l.cell_w + (l.cell_w - l.tile_w) / 2,
                 l.top + r * l.row_h - scroll, l.tile_w, l.tile_h);
}

int ChannelGridStyle::RowAt(const QPoint& pos) const {
    const Layout l = ComputeLayout();
    if (pos.y() >= l.dock_top) {
        return -1;
    }
    const int y = pos.y() + scroll - l.top + Gap / 2;
    const int x = pos.x() - l.margin;
    if (y < 0 || x < 0 || x >= l.cell_w * l.columns) {
        return -1;
    }
    const int row = (y / l.row_h) * l.columns + x / l.cell_w;
    if (row < 0 || row >= RowCount()) {
        return -1;
    }
    const QRect hit = TileRect(l, row).adjusted(-Gap / 2, -Gap / 2, Gap / 2,
                                                show_names ? NameHeight : 0);
    return hit.contains(pos) ? row : -1;
}

void ChannelGridStyle::EnsureVisible(int row) {
    const Layout l = ComputeLayout();
    const int top = l.top + (row / l.columns) * l.row_h;
    const int bottom = top + l.row_h;
    if (top - Gap / 2 < scroll) {
        scroll = std::max(0, top - l.top);
    } else if (bottom + l.curve > scroll + l.view_h) {
        scroll = bottom + l.curve - l.view_h;
    }
    ClampScroll();
}

void ChannelGridStyle::SetCurrentRow(int row, bool notify) {
    const int count = RowCount();
    if (count == 0) {
        return;
    }
    row = std::clamp(row, 0, count - 1);
    const QModelIndex index = IndexForRow(row);
    if (!index.isValid()) {
        return; // stale entry while a rebuild is pending
    }
    const bool changed = CurrentRow() != row;
    current = QPersistentModelIndex{index};
    current_hint = row;
    EnsureVisible(row);
    update();
    if (changed && notify) {
        emit CurrentChanged(index);
    }
}

void ChannelGridStyle::resizeEvent(QResizeEvent* event) {
    LibraryStyleView::resizeEvent(event);
    cover_cache.clear();
    if (CurrentRow() >= 0) {
        EnsureVisible(CurrentRow());
    }
    ClampScroll();
}

void ChannelGridStyle::keyPressEvent(QKeyEvent* event) {
    const int count = RowCount();
    if (count == 0) {
        LibraryStyleView::keyPressEvent(event);
        return;
    }
    const Layout l = ComputeLayout();
    const int row = std::max(0, CurrentRow());
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_A:
        SetCurrentRow(row - 1, true);
        break;
    case Qt::Key_Right:
    case Qt::Key_D:
        SetCurrentRow(row + 1, true);
        break;
    case Qt::Key_Up:
    case Qt::Key_W:
        SetCurrentRow(row - l.columns, true);
        break;
    case Qt::Key_Down:
    case Qt::Key_S:
        SetCurrentRow(row + l.columns, true);
        break;
    case Qt::Key_Home:
        SetCurrentRow(0, true);
        break;
    case Qt::Key_End:
        SetCurrentRow(count - 1, true);
        break;
    case Qt::Key_PageUp:
        SetCurrentRow(row - l.columns * std::max(1, l.view_h / l.row_h), true);
        break;
    case Qt::Key_PageDown:
        SetCurrentRow(row + l.columns * std::max(1, l.view_h / l.row_h), true);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (CurrentRow() >= 0) {
            emit LaunchRequested(QModelIndex{current});
        }
        break;
    default:
        LibraryStyleView::keyPressEvent(event);
        return;
    }
    event->accept();
}

void ChannelGridStyle::contextMenuEvent(QContextMenuEvent* event) {
    // Keyboard menu key: open the menu for the selected tile.
    const int row = CurrentRow();
    if (event->reason() != QContextMenuEvent::Keyboard || row < 0) {
        event->ignore();
        return;
    }
    const QPoint center = TileRect(ComputeLayout(), row).center();
    emit ContextMenuRequested(IndexForRow(row), mapToGlobal(center));
    event->accept();
}

void ChannelGridStyle::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    const int row = RowAt(event->position().toPoint());
    if (row >= 0 && IndexForRow(row).isValid() && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
        SetCurrentRow(row, true);
        if (event->button() == Qt::RightButton) {
            emit ContextMenuRequested(IndexForRow(row), event->globalPosition().toPoint());
        }
        event->accept();
        return;
    }
    LibraryStyleView::mousePressEvent(event);
}

void ChannelGridStyle::mouseDoubleClickEvent(QMouseEvent* event) {
    const int row = RowAt(event->position().toPoint());
    if (row >= 0 && event->button() == Qt::LeftButton && IndexForRow(row).isValid()) {
        SetCurrentRow(row, true);
        emit LaunchRequested(IndexForRow(row));
        event->accept();
        return;
    }
    LibraryStyleView::mouseDoubleClickEvent(event);
}

void ChannelGridStyle::wheelEvent(QWheelEvent* event) {
    const Layout l = ComputeLayout();
    int delta = 0;
    if (!event->pixelDelta().isNull()) {
        delta = -event->pixelDelta().y();
    } else {
        delta = -event->angleDelta().y() * l.row_h / 240;
    }
    const int old = scroll;
    scroll = std::clamp(scroll + delta, 0, MaxScroll(l));
    if (scroll != old) {
        update();
    }
    event->accept();
}

QPixmap ChannelGridStyle::CoverFor(const QModelIndex& index, const QSize& size, qreal dpr) {
    if (!IsGame(index) || !index.data(GameListItemPath::HasIconRole).toBool()) {
        return {};
    }
    const QString key = QStringLiteral("%1|%2|%3x%4")
                            .arg(index.data(GameListItemPath::FullPathRole).toString())
                            .arg(index.data(GameListItemPath::ProgramIdRole).toULongLong())
                            .arg(size.width())
                            .arg(size.height());
    if (const QPixmap* cached = cover_cache.object(key)) {
        return *cached;
    }
    QPixmap source = index.data(GameListItemPath::CardIconRole).value<QPixmap>();
    if (source.isNull()) {
        source = index.data(Qt::DecorationRole).value<QPixmap>();
    }
    if (source.isNull()) {
        return {};
    }
    // Crop to fill: scale so both sides cover the target, then cut the centre.
    const QSize device(std::max(1, int(std::lround(size.width() * dpr))),
                       std::max(1, int(std::lround(size.height() * dpr))));
    const QPixmap scaled =
        source.scaled(device, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap out = scaled.copy(QRect((scaled.width() - device.width()) / 2,
                                    (scaled.height() - device.height()) / 2, device.width(),
                                    device.height()));
    out.setDevicePixelRatio(dpr);
    const int cost = std::max(1, device.width() * device.height() * 4 / 1024);
    cover_cache.insert(key, new QPixmap(out), cost);
    return out;
}

void ChannelGridStyle::PaintTile(QPainter& p, const Layout& l, int row, bool selected) {
    const QModelIndex index = IndexForRow(row);
    const QPalette pal = palette();
    const bool dark = IsDark(pal);
    const QRectF tile = TileRect(l, row);
    const qreal radius = tile.height() * 0.11;
    const QColor accent = LibraryStyleContext::Accent(pal);

    // Soft drop shadow.
    p.setPen(Qt::NoPen);
    for (int i = 3; i >= 1; --i) {
        p.setBrush(QColor(0, 0, 0, dark ? 28 : 14));
        p.drawRoundedRect(tile.adjusted(-i, -i, i, i).translated(0, 3 + i), radius + i, radius + i);
    }

    // Glow for the selected tile.
    if (selected) {
        for (int i = 9; i >= 1; --i) {
            QColor glow = accent;
            glow.setAlpha(14 + (9 - i) * 6);
            p.setPen(QPen(glow, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(tile.adjusted(-i, -i, i, i), radius + i, radius + i);
        }
    }

    // Shell with a soft inner bezel.
    QLinearGradient shell(tile.topLeft(), tile.bottomLeft());
    shell.setColorAt(0.0, dark ? pal.window().color().lighter(165) : pal.base().color());
    shell.setColorAt(1.0,
                     dark ? pal.window().color().lighter(125) : pal.window().color().darker(106));
    p.setBrush(shell);
    QColor edge = pal.text().color();
    edge.setAlpha(dark ? 60 : 40);
    p.setPen(QPen(edge, 1.0));
    p.drawRoundedRect(tile, radius, radius);

    const qreal bezel = std::max(3.0, tile.height() * 0.035);
    const QRectF art = tile.adjusted(bezel, bezel, -bezel, -bezel);
    const qreal art_radius = std::max(2.0, radius - bezel * 0.6);
    QPainterPath clip;
    clip.addRoundedRect(art, art_radius, art_radius);

    p.save();
    p.setClipPath(clip);
    const QPixmap cover = CoverFor(index, art.size().toSize(), devicePixelRatioF());
    if (!cover.isNull()) {
        p.drawPixmap(art.topLeft(), cover);
    } else {
        // Default art: a gradient from the palette with the initial of the title.
        QLinearGradient bg(art.topLeft(), art.bottomRight());
        bg.setColorAt(0.0, accent.lighter(dark ? 115 : 135));
        bg.setColorAt(1.0, accent.darker(dark ? 170 : 125));
        p.fillRect(art, bg);
        const QString title = TitleOf(index);
        const QString glyph = IsAddFolder(index)
                                  ? QStringLiteral("+")
                                  : (title.isEmpty() ? QString{} : title.left(1).toUpper());
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(std::max(12, int(art.height() * 0.42)));
        p.setFont(f);
        QColor ink = pal.highlightedText().color();
        ink.setAlpha(210);
        p.setPen(ink);
        p.drawText(art, Qt::AlignCenter, glyph);
    }
    p.restore();

    // Inner bezel line around the art.
    QColor inner = Qt::black;
    inner.setAlpha(dark ? 120 : 55);
    p.setPen(QPen(inner, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(art, art_radius, art_radius);

    // Glossy highlight over the upper part of the art.
    p.save();
    p.setClipPath(clip);
    QPainterPath gloss;
    gloss.moveTo(art.left(), art.top());
    gloss.lineTo(art.right(), art.top());
    gloss.lineTo(art.right(), art.top() + art.height() * 0.30);
    gloss.quadTo(art.center().x(), art.top() + art.height() * 0.52, art.left(),
                 art.top() + art.height() * 0.30);
    gloss.closeSubpath();
    // Strong enough to read over busy cover art; it stays faintly visible down to its curved
    // lower edge, like the shine on a glossy plastic tile.
    QLinearGradient gl(art.topLeft(), QPointF(art.left(), art.top() + art.height() * 0.52));
    gl.setColorAt(0.0, QColor(255, 255, 255, dark ? 95 : 110));
    gl.setColorAt(1.0, QColor(255, 255, 255, dark ? 22 : 30));
    p.setPen(Qt::NoPen);
    p.setBrush(gl);
    p.drawPath(gloss);
    p.restore();

    if (selected) {
        p.setPen(QPen(accent, 3.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(tile.adjusted(-1.5, -1.5, 1.5, 1.5), radius + 1.5, radius + 1.5);
    }

    if (show_names) {
        QFont f = font();
        f.setPixelSize(std::clamp(l.tile_w / 22, 11, 16));
        f.setBold(selected);
        p.setFont(f);
        p.setPen(pal.windowText().color());
        const QRectF text(tile.left(), tile.bottom() + 6, tile.width(), NameHeight - 6);
        p.drawText(text, Qt::AlignHCenter | Qt::AlignTop,
                   QFontMetrics(f).elidedText(TitleOf(index), Qt::ElideRight,
                                              int(tile.width()) - 8));
    }
}

void ChannelGridStyle::PaintDock(QPainter& p, const Layout& l) {
    const QPalette pal = palette();
    const bool dark = IsDark(pal);
    const int w = width();
    const int h = height();
    const qreal top = l.dock_top;

    QPainterPath dock;
    dock.moveTo(0, top + l.curve);
    dock.quadTo(w / 2.0, top - l.curve, w, top + l.curve);
    dock.lineTo(w, h);
    dock.lineTo(0, h);
    dock.closeSubpath();

    const QColor base = pal.window().color();
    QLinearGradient fill(0, top - l.curve, 0, h);
    fill.setColorAt(0.0, dark ? base.lighter(135) : base.lighter(104));
    fill.setColorAt(1.0, dark ? base.lighter(112) : base.darker(106));

    // Shadow cast upward on the tiles.
    p.setPen(Qt::NoPen);
    for (int i = 4; i >= 1; --i) {
        p.setBrush(QColor(0, 0, 0, dark ? 22 : 10));
        p.save();
        p.translate(0, -i * 2);
        p.drawPath(dock);
        p.restore();
    }
    p.setBrush(fill);
    QColor edge = LibraryStyleContext::Accent(pal);
    edge.setAlpha(dark ? 110 : 90);
    p.setPen(QPen(edge, 1.5));
    p.drawPath(dock);

    // Faint horizontal lines on the dock.
    QColor line = pal.text().color();
    line.setAlpha(dark ? 14 : 10);
    p.save();
    p.setClipPath(dock);
    p.setPen(QPen(line, 1.0));
    for (int y = int(top) + 6; y < h; y += 6) {
        p.drawLine(0, y, w, y);
    }
    p.restore();

    const int mid_y = int(top) + l.curve + (l.dock_h - l.curve) / 2;
    const int margin = l.margin;

    // Centre: clock and date.
    const QLocale locale;
    const QString time_text = locale.toString(QTime::currentTime(), QLocale::ShortFormat);
    const QString date_text = locale.toString(QDate::currentDate(), QLocale::LongFormat);
    QFont clock_font = font();
    clock_font.setBold(true);
    clock_font.setPixelSize(std::clamp(l.dock_h * 30 / 100, 20, 64));
    QFont date_font = font();
    date_font.setPixelSize(std::clamp(l.dock_h * 11 / 100, 11, 20));
    const int clock_h = QFontMetrics(clock_font).height();
    const int date_h = QFontMetrics(date_font).height();
    const int block_h = clock_h + date_h;
    const int center_w = std::max(120, w / 3);
    const QRect clock_rect(w / 2 - center_w / 2, mid_y - block_h / 2, center_w, clock_h);
    const QRect date_rect(clock_rect.left(), clock_rect.bottom(), center_w, date_h);
    QColor ink = pal.windowText().color();
    ink.setAlpha(220);
    p.setPen(ink);
    p.setFont(clock_font);
    p.drawText(clock_rect, Qt::AlignCenter, time_text);
    ink.setAlpha(150);
    p.setPen(ink);
    p.setFont(date_font);
    p.drawText(date_rect, Qt::AlignHCenter | Qt::AlignTop,
               QFontMetrics(date_font).elidedText(date_text, Qt::ElideRight, center_w));

    const int clock_half = QFontMetrics(clock_font).horizontalAdvance(time_text) / 2;

    // Left: round app name button (branding only).
    const int diameter = std::clamp(l.dock_h * 52 / 100, 48, 120);
    const QRectF button(margin + 8, mid_y - diameter / 2.0, diameter, diameter);
    if (button.right() < w / 2 - clock_half - 8) {
        const QColor accent = LibraryStyleContext::Accent(pal);
        QRadialGradient rg(button.center() - QPointF(diameter * 0.15, diameter * 0.2),
                           diameter * 0.75);
        rg.setColorAt(0.0, accent.lighter(130));
        rg.setColorAt(1.0, accent.darker(dark ? 150 : 115));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, dark ? 40 : 22));
        p.drawEllipse(button.translated(0, 3));
        p.setBrush(rg);
        QColor ring = pal.highlightedText().color();
        ring.setAlpha(110);
        p.setPen(QPen(ring, 1.5));
        p.drawEllipse(button);
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(std::clamp(diameter * 22 / 100, 10, 22));
        p.setFont(f);
        p.setPen(pal.highlightedText().color());
        p.drawText(button, Qt::AlignCenter, tr("Lichii"));
    }

    // Right: info card for the selected game.
    if (show_info && CurrentRow() >= 0) {
        const QModelIndex index{current};
        const int card_w = std::clamp(w * 26 / 100, 160, 460);
        const int card_h = std::clamp(l.dock_h * 58 / 100, 56, 110);
        const QRectF card(w - margin - 8 - card_w, mid_y - card_h / 2.0, card_w, card_h);
        if (card.left() > w / 2 + clock_half + 12) {
            const QColor accent = LibraryStyleContext::Accent(pal);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, dark ? 40 : 20));
            p.drawRoundedRect(card.translated(0, 3), 14, 14);
            p.setBrush(dark ? pal.window().color().lighter(150) : pal.base().color());
            QColor outline = accent;
            outline.setAlpha(200);
            p.setPen(QPen(outline, 2.0));
            p.drawRoundedRect(card, 14, 14);

            const QStringList meta{LibraryStyleContext::Info(index).info_line};
            QFont tf = font();
            tf.setBold(true);
            tf.setPixelSize(std::clamp(card_h * 20 / 100, 11, 18));
            QFont mf = font();
            mf.setPixelSize(std::clamp(card_h * 15 / 100, 10, 14));
            const QRectF inner = card.adjusted(14, 8, -14, -8);
            const int meta_h = QFontMetrics(mf).height();
            QRectF title_rect = inner;
            title_rect.setBottom(inner.bottom() - meta_h - 2);
            p.setFont(tf);
            p.setPen(pal.windowText().color());
            p.drawText(title_rect, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                       QFontMetrics(tf).elidedText(TitleOf(index), Qt::ElideRight,
                                                   int(title_rect.width()) * 2 - 24));
            QColor sub = pal.windowText().color();
            sub.setAlpha(165);
            p.setFont(mf);
            p.setPen(sub);
            p.drawText(QRectF(inner.left(), inner.bottom() - meta_h, inner.width(), meta_h),
                       Qt::AlignHCenter | Qt::AlignVCenter,
                       meta.join(QStringLiteral(" · ")));
        }
    }
}

void ChannelGridStyle::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QPalette pal = palette();
    const bool dark = IsDark(pal);
    const Layout l = ComputeLayout();

    // Background: a gentle vertical gradient with faint horizontal lines.
    const QColor base = pal.window().color();
    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0.0, dark ? base.lighter(118) : base.lighter(105));
    bg.setColorAt(1.0, dark ? base : base.darker(104));
    p.fillRect(rect(), bg);
    QColor line = pal.text().color();
    line.setAlpha(dark ? 12 : 9);
    p.setPen(QPen(line, 1.0));
    for (int y = 3; y < l.dock_top + l.curve; y += 5) {
        p.drawLine(0, y, width(), y);
    }

    // Only the rows in view are painted.
    const int count = RowCount();
    if (count > 0) {
        p.save();
        p.setClipRect(QRect(0, 0, width(), l.dock_top + l.curve));
        const int first_row = std::max(0, (scroll - l.top - Gap) / l.row_h);
        const int last_row = std::min((scroll + l.view_h + l.curve - l.top) / l.row_h,
                                      (count - 1) / l.columns);
        const int current_row = CurrentRow();
        for (int r = first_row; r <= last_row; ++r) {
            for (int c = 0; c < l.columns; ++c) {
                const int row = r * l.columns + c;
                if (row >= count) {
                    break;
                }
                if (row != current_row) {
                    PaintTile(p, l, row, false);
                }
            }
        }
        // The selected tile goes last so its glow is not covered by neighbours.
        if (current_row >= 0 && current_row < count) {
            const QRect t = TileRect(l, current_row);
            if (t.bottom() > -20 && t.top() < l.dock_top + l.curve) {
                PaintTile(p, l, current_row, true);
            }
        }
        p.restore();
    }

    PaintDock(p, l);
}

namespace {

LibraryStyleView* Make(QWidget* parent) {
    return new ChannelGridStyle(parent);
}

[[maybe_unused]] const bool registered =
    LibraryStyles::Register({"channel_grid", QT_TR_NOOP("Channels"), &Make});

} // namespace

} // namespace LibraryStyles
