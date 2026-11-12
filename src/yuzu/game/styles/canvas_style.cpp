// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Canvas library style. Concept by inxomnyaa.

#include <algorithm>
#include <cmath>

#include <QAbstractItemModel>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStringList>
#include <QTimer>
#include <QWheelEvent>

#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "yuzu/game/styles/canvas_style.h"

namespace LibraryStyles {

namespace {

LibraryStyleView* Make(QWidget* p) {
    return new CanvasStyle(p);
}

[[maybe_unused]] const bool registered =
    LibraryStyles::Register({"canvas", QT_TR_NOOP("Canvas"), &Make});

constexpr int SidePadding = 28;
constexpr int FooterBarHeight = 48;

bool IsGameType(GameListItemType type) {
    return type == GameListItemType::Game;
}

bool IsFolderType(GameListItemType type) {
    return type == GameListItemType::CustomDir || type == GameListItemType::SdmcDir ||
           type == GameListItemType::UserNandDir || type == GameListItemType::SysNandDir;
}

GameListItemType TypeOf(const QModelIndex& index) {
    return index.data(GameListItem::TypeRole).value<GameListItemType>();
}

struct GameInfo {
    QString title;
    QString play_time;
    QString version;
    QString program_id;
};

// Play time and version are only exposed through the tooltip ("Play Time: X" or "Never Played",
// then "Version: Y"), so read them from there by line position and strip the label.
GameInfo ReadInfo(const QModelIndex& index) {
    GameInfo info;
    info.title = index.data(GameListItemPath::TitleRole).toString();
    if (info.title.isEmpty()) {
        info.title = index.data(Qt::DisplayRole).toString().section(QLatin1Char('\n'), 0, 0);
    }
    const QStringList lines = index.data(Qt::ToolTipRole).toString().split(QLatin1Char('\n'));
    const auto value_of = [](const QString& line) {
        const int colon = line.indexOf(QLatin1Char(':'));
        return colon < 0 ? line.trimmed() : line.mid(colon + 1).trimmed();
    };
    if (lines.size() > 0) {
        info.play_time = value_of(lines[0]);
    }
    if (lines.size() > 1) {
        info.version = value_of(lines[1]);
    }
    const qulonglong pid = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    info.program_id = QStringLiteral("%1").arg(pid, 16, 16, QLatin1Char('0')).toUpper();
    return info;
}

// Greedy two line wrap: the first line breaks at the last space that fits, the rest is elided.
void TwoLines(const QString& text, const QFontMetrics& fm, int width, QString* first,
              QString* second) {
    *first = text;
    second->clear();
    if (fm.horizontalAdvance(text) <= width) {
        return;
    }
    int fit = 0;
    for (int i = 1; i <= text.size(); ++i) {
        if (fm.horizontalAdvance(text.left(i)) > width) {
            break;
        }
        fit = i;
    }
    fit = std::max(fit, 1);
    const int space = text.lastIndexOf(QLatin1Char(' '), fit - 1);
    const int cut = space > 0 ? space : fit;
    *first = text.left(cut).trimmed();
    *second = fm.elidedText(text.mid(cut).trimmed(), Qt::ElideRight, width);
}

QColor HueColor(uint seed, int saturation, int lightness) {
    return QColor::fromHsl(static_cast<int>(seed % 360), saturation, lightness);
}

void DrawPlaceholder(QPainter* painter, const QRectF& rect, const QString& title, uint seed,
                     bool dark) {
    QLinearGradient grad(rect.topLeft(), rect.bottomRight());
    grad.setColorAt(0.0, HueColor(seed, dark ? 90 : 120, dark ? 70 : 150));
    grad.setColorAt(1.0, HueColor(seed + 40, dark ? 100 : 130, dark ? 40 : 100));
    painter->fillRect(rect, grad);
    const QString initial = title.trimmed().left(1).toUpper();
    if (initial.isEmpty()) {
        return;
    }
    QFont font = painter->font();
    font.setBold(true);
    font.setPixelSize(std::max(12, static_cast<int>(rect.height() * 0.42)));
    painter->setFont(font);
    painter->setPen(QColor(255, 255, 255, 190));
    painter->drawText(rect, Qt::AlignCenter, initial);
}

void DrawGlyph(QPainter* painter, const QPointF& center, qreal radius, const QString& letter,
               const QColor& color) {
    painter->setPen(QPen(color, 1.4));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(center, radius, radius);
    QFont font = painter->font();
    font.setBold(true);
    font.setPixelSize(std::max(8, static_cast<int>(radius * 1.2)));
    painter->setFont(font);
    painter->drawText(QRectF(center.x() - radius, center.y() - radius, radius * 2, radius * 2),
                      Qt::AlignCenter, letter);
}

} // namespace

CanvasStyle::CanvasStyle(QWidget* parent) : LibraryStyleView(parent) {
    setObjectName(QStringLiteral("canvasLibraryStyle"));
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
}

CanvasStyle::~CanvasStyle() = default;

void CanvasStyle::SetModel(QAbstractItemModel* new_model) {
    if (model) {
        model->disconnect(this);
    }
    model = new_model;
    if (model) {
        const auto rebuild = [this] { ScheduleRebuild(); };
        connect(model, &QAbstractItemModel::modelReset, this, rebuild);
        connect(model, &QAbstractItemModel::layoutChanged, this, rebuild);
        connect(model, &QAbstractItemModel::rowsInserted, this, rebuild);
        connect(model, &QAbstractItemModel::rowsRemoved, this, rebuild);
        connect(model, &QAbstractItemModel::rowsMoved, this, rebuild);
        connect(model, &QAbstractItemModel::dataChanged, this, [this] { update(); });
    }
    current = -1;
    scroll = 0;
    Rebuild();
}

QModelIndex CanvasStyle::CurrentIndex() const {
    return IndexAt(current);
}

void CanvasStyle::SetCurrentIndex(const QModelIndex& index) {
    if (!index.isValid()) {
        SelectRow(-1);
        return;
    }
    const QPersistentModelIndex wanted{index};
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i] == wanted) {
            SelectRow(static_cast<int>(i));
            return;
        }
    }
}

void CanvasStyle::SetShowNames(bool show) {
    show_names = show;
    update();
}

void CanvasStyle::SetShowInfo(bool show) {
    show_info = show;
    update();
}

QModelIndex CanvasStyle::IndexAt(int row) const {
    if (row < 0 || row >= static_cast<int>(entries.size())) {
        return {};
    }
    return QModelIndex(entries[static_cast<size_t>(row)]);
}

void CanvasStyle::ScheduleRebuild() {
    if (rebuild_pending) {
        return;
    }
    rebuild_pending = true;
    QTimer::singleShot(0, this, [this] {
        rebuild_pending = false;
        Rebuild();
    });
}

void CanvasStyle::Rebuild() {
    const QPersistentModelIndex previous{IndexAt(current)};
    entries.clear();
    if (model) {
        // Games are shown flat: top level games plus the games inside each folder row.
        // Favorites and "add folder" rows are not tiles.
        const int top_rows = model->rowCount();
        for (int r = 0; r < top_rows; ++r) {
            const QModelIndex idx = model->index(r, 0);
            const GameListItemType type = TypeOf(idx);
            if (IsGameType(type)) {
                entries.emplace_back(idx);
            } else if (IsFolderType(type)) {
                const int children = model->rowCount(idx);
                for (int c = 0; c < children; ++c) {
                    const QModelIndex child = model->index(c, 0, idx);
                    if (IsGameType(TypeOf(child))) {
                        entries.emplace_back(child);
                    }
                }
            }
        }
    }

    int new_current = -1;
    if (previous.isValid()) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i] == previous) {
                new_current = static_cast<int>(i);
                break;
            }
        }
    }
    if (new_current < 0 && !entries.empty()) {
        new_current = std::clamp(current, 0, static_cast<int>(entries.size()) - 1);
    }
    const QModelIndex before = previous;
    current = new_current;
    UpdateLayout();
    if (IndexAt(current) != before) {
        emit CurrentChanged(IndexAt(current));
    }
    update();
}

CanvasStyle::Layout CanvasStyle::ComputeLayout() const {
    Layout l;
    l.gap = 18;
    const int target =
        std::max(96, static_cast<int>(UISettings::values.game_cover_size.GetValue()));
    const int avail = std::max(1, width() - 2 * SidePadding);
    l.columns = std::max(1, (avail + l.gap) / (target + l.gap));
    int tile = (avail - l.gap * (l.columns - 1)) / l.columns;
    tile = std::clamp(tile, 64, target * 3 / 2);
    l.tile = tile;
    const int grid_width = l.columns * tile + (l.columns - 1) * l.gap;
    l.margin = std::max(SidePadding, (width() - grid_width) / 2);
    l.top = SidePadding;
    return l;
}

int CanvasStyle::FooterHeight() const {
    return FooterBarHeight + 24;
}

void CanvasStyle::UpdateLayout() {
    layout = ComputeLayout();
    scroll = std::clamp(scroll, 0, MaxScroll());
}

int CanvasStyle::MaxScroll() const {
    const int count = static_cast<int>(entries.size());
    const int rows = (count + layout.columns - 1) / layout.columns;
    const int content = layout.top + rows * (layout.tile + layout.gap) + FooterHeight();
    return std::max(0, content - height());
}

QRect CanvasStyle::TileRect(int row) const {
    const int col = row % layout.columns;
    const int line = row / layout.columns;
    return QRect(layout.margin + col * (layout.tile + layout.gap),
                 layout.top + line * (layout.tile + layout.gap), layout.tile, layout.tile);
}

int CanvasStyle::RowAt(const QPoint& pos) const {
    const int x = pos.x() - layout.margin;
    const int y = pos.y() + scroll - layout.top;
    if (x < 0 || y < 0) {
        return -1;
    }
    const int step = layout.tile + layout.gap;
    const int col = x / step;
    const int line = y / step;
    if (col >= layout.columns || x % step >= layout.tile || y % step >= layout.tile) {
        return -1;
    }
    const int row = line * layout.columns + col;
    return row < static_cast<int>(entries.size()) ? row : -1;
}

void CanvasStyle::SetScroll(int value) {
    value = std::clamp(value, 0, MaxScroll());
    if (value != scroll) {
        scroll = value;
        update();
    }
}

void CanvasStyle::EnsureVisible(int row) {
    if (row < 0) {
        return;
    }
    const QRect tile = TileRect(row);
    const int pad = 12;
    const int visible_bottom = height() - FooterHeight() + 16;
    if (tile.top() - pad < scroll) {
        SetScroll(tile.top() - pad);
    } else if (tile.bottom() + pad > scroll + visible_bottom) {
        SetScroll(tile.bottom() + pad - visible_bottom);
    }
}

void CanvasStyle::SelectRow(int row, bool notify) {
    if (row >= static_cast<int>(entries.size())) {
        row = static_cast<int>(entries.size()) - 1;
    }
    if (row < -1) {
        row = -1;
    }
    if (row == current) {
        EnsureVisible(row);
        return;
    }
    current = row;
    EnsureVisible(row);
    update();
    if (notify) {
        emit CurrentChanged(IndexAt(current));
    }
}

void CanvasStyle::resizeEvent(QResizeEvent* event) {
    LibraryStyleView::resizeEvent(event);
    UpdateLayout();
    EnsureVisible(current);
}

void CanvasStyle::changeEvent(QEvent* event) {
    LibraryStyleView::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        update();
    }
}

void CanvasStyle::wheelEvent(QWheelEvent* event) {
    int delta = 0;
    if (!event->pixelDelta().isNull()) {
        delta = -event->pixelDelta().y();
    } else {
        delta = -event->angleDelta().y() * layout.tile * 6 / 10 / 120;
    }
    SetScroll(scroll + delta);
    event->accept();
}

void CanvasStyle::keyPressEvent(QKeyEvent* event) {
    const int count = static_cast<int>(entries.size());
    if (count == 0) {
        LibraryStyleView::keyPressEvent(event);
        return;
    }
    const int cols = layout.columns;
    int row = current < 0 ? 0 : current;
    const int page_rows = std::max(1, height() / (layout.tile + layout.gap) - 1);
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_A:
        row = std::max(0, row - 1);
        break;
    case Qt::Key_Right:
    case Qt::Key_D:
        row = std::min(count - 1, row + 1);
        break;
    case Qt::Key_Up:
    case Qt::Key_W:
        if (row - cols >= 0) {
            row -= cols;
        }
        break;
    case Qt::Key_Down:
    case Qt::Key_S:
        if (row + cols < count) {
            row += cols;
        } else if (row / cols < (count - 1) / cols) {
            row = count - 1;
        }
        break;
    case Qt::Key_Home:
        row = 0;
        break;
    case Qt::Key_End:
        row = count - 1;
        break;
    case Qt::Key_PageUp:
        row = std::max(0, row - cols * page_rows);
        break;
    case Qt::Key_PageDown:
        row = std::min(count - 1, row + cols * page_rows);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (current >= 0) {
            emit LaunchRequested(IndexAt(current));
        }
        return;
    default:
        LibraryStyleView::keyPressEvent(event);
        return;
    }
    SelectRow(row);
}

void CanvasStyle::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    const int row = RowAt(event->position().toPoint());
    if (row < 0) {
        LibraryStyleView::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        SelectRow(row);
    }
    if (event->button() == Qt::RightButton) {
        emit ContextMenuRequested(IndexAt(row), event->globalPosition().toPoint());
    }
    event->accept();
}

void CanvasStyle::mouseDoubleClickEvent(QMouseEvent* event) {
    const int row = RowAt(event->position().toPoint());
    if (row >= 0 && event->button() == Qt::LeftButton) {
        SelectRow(row);
        emit LaunchRequested(IndexAt(row));
        event->accept();
        return;
    }
    LibraryStyleView::mouseDoubleClickEvent(event);
}

void CanvasStyle::paintEvent([[maybe_unused]] QPaintEvent* event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPalette pal = palette();
    const bool dark = pal.window().color().lightness() < 128;
    const QColor accent = pal.highlight().color();
    const int w = width();
    const int h = height();

    // Day or night background and arcs follow the palette.
    const QColor bg = dark ? pal.window().color().darker(135) : QColor(246, 233, 212);
    p.fillRect(rect(), bg);

    const QColor warm[] = {dark ? QColor(92, 62, 58) : QColor(244, 203, 144),
                           dark ? QColor(98, 70, 54) : QColor(235, 164, 143),
                           dark ? QColor(70, 78, 62) : QColor(236, 178, 128)};
    const QColor cool[] = {dark ? QColor(56, 86, 88) : QColor(183, 216, 202),
                           dark ? QColor(60, 70, 96) : QColor(138, 185, 172),
                           dark ? QColor(76, 62, 92) : QColor(222, 171, 160)};
    const int band = std::max(w, h) / 17;
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < 6; ++i) {
        p.setPen(QPen(warm[i % 3], band, Qt::SolidLine, Qt::FlatCap));
        const qreal r = h * 0.30 + i * band * 2.0;
        p.drawEllipse(QPointF(w * 0.08, h * 1.18), r, r);
    }
    for (int i = 0; i < 5; ++i) {
        p.setPen(QPen(cool[i % 3], band, Qt::SolidLine, Qt::FlatCap));
        const qreal r = h * 0.45 + i * band * 2.0;
        p.drawEllipse(QPointF(w * 1.02, h * 1.12), r, r);
    }
    p.setPen(Qt::NoPen);

    // Tiles: only the visible rows are painted.
    const int count = static_cast<int>(entries.size());
    if (count > 0) {
        const int step = layout.tile + layout.gap;
        const int first_line = std::max(0, (scroll - layout.top) / step - 1);
        const int last_line = std::max(0, (scroll + h - layout.top) / step + 1);
        const int first = first_line * layout.columns;
        const int last = std::min(count - 1, (last_line + 1) * layout.columns - 1);
        const qreal radius = layout.tile * 0.14;

        for (int row = first; row <= last; ++row) {
            const QModelIndex index = IndexAt(row);
            if (!index.isValid()) {
                continue;
            }
            const QRect rect = TileRect(row).translated(0, -scroll);
            if (rect.bottom() < -8 || rect.top() > h + 8) {
                continue;
            }
            const bool selected = row == current;
            const QRectF rectf(rect);

            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, dark ? 90 : 45));
            p.drawRoundedRect(rectf.translated(0, 4), radius, radius);

            QPainterPath clip;
            clip.addRoundedRect(rectf, radius, radius);
            p.save();
            p.setClipPath(clip);

            const QString title = index.data(GameListItemPath::TitleRole).toString();
            const bool has_icon = index.data(GameListItemPath::HasIconRole).toBool();
            QPixmap art;
            if (has_icon) {
                art = index.data(GameListItemPath::CardIconRole).value<QPixmap>();
                if (art.isNull()) {
                    art = index.data(Qt::DecorationRole).value<QPixmap>();
                }
            }
            if (!art.isNull()) {
                QSizeF scaled = QSizeF(art.size());
                scaled.scale(rectf.size(), Qt::KeepAspectRatioByExpanding);
                const QRectF target(rectf.center().x() - scaled.width() / 2,
                                    rectf.center().y() - scaled.height() / 2, scaled.width(),
                                    scaled.height());
                p.drawPixmap(target, art, QRectF(art.rect()));
            } else {
                const uint seed = static_cast<uint>(
                    qHash(index.data(GameListItemPath::ProgramIdRole).toULongLong()));
                DrawPlaceholder(&p, rectf, title, seed, dark);
            }

            if (selected && show_names) {
                const GameInfo info = ReadInfo(index);
                const int pad = std::max(8, layout.tile / 18);
                QFont title_font = font();
                title_font.setBold(true);
                title_font.setPixelSize(std::clamp(layout.tile / 12, 11, 20));
                QFont sub_font = font();
                sub_font.setPixelSize(std::clamp(layout.tile / 15, 9, 16));
                const QFontMetrics tfm(title_font);
                const QFontMetrics sfm(sub_font);
                const int text_w = rect.width() - 2 * pad;
                QString line1;
                QString line2;
                TwoLines(info.title, tfm, text_w, &line1, &line2);
                const int lines = line2.isEmpty() ? 1 : 2;
                const int text_h = lines * tfm.lineSpacing() + sfm.lineSpacing() + 4;
                const int grad_h = text_h + pad * 3;
                const QRectF grad_rect(rectf.left(), rectf.bottom() - grad_h, rectf.width(),
                                       grad_h);
                QLinearGradient g(grad_rect.topLeft(), grad_rect.bottomLeft());
                g.setColorAt(0.0, QColor(0, 0, 0, 0));
                g.setColorAt(0.55, QColor(0, 0, 0, 170));
                g.setColorAt(1.0, QColor(0, 0, 0, 225));
                p.fillRect(grad_rect, g);

                p.setPen(Qt::white);
                p.setFont(sub_font);
                const QString sub =
                    info.version.isEmpty()
                        ? info.play_time
                        : QStringLiteral("%1 %2 %3")
                              .arg(info.play_time, QString(QChar(0x00B7)), info.version);
                const int sub_y = rect.bottom() - pad - sfm.lineSpacing();
                p.drawText(QRect(rect.left() + pad, sub_y, text_w, sfm.lineSpacing()),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           sfm.elidedText(sub, Qt::ElideRight, text_w));
                p.setFont(title_font);
                int y = sub_y - 2 - lines * tfm.lineSpacing();
                p.drawText(QRect(rect.left() + pad, y, text_w, tfm.lineSpacing()),
                           Qt::AlignLeft | Qt::AlignVCenter, line1);
                if (!line2.isEmpty()) {
                    y += tfm.lineSpacing();
                    p.drawText(QRect(rect.left() + pad, y, text_w, tfm.lineSpacing()),
                               Qt::AlignLeft | Qt::AlignVCenter, line2);
                }
            }
            p.restore();

            if (selected) {
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(accent, 3.5));
                p.drawRoundedRect(rectf.adjusted(-6, -6, 6, 6), radius + 6, radius + 6);
            }
        }
    }

    // Button hints, bottom centre.
    {
        QFont font_hint = font();
        font_hint.setBold(true);
        font_hint.setPixelSize(13);
        p.setFont(font_hint);
        const QFontMetrics fm(font_hint);
        struct Hint {
            QStringList glyphs;
            QString label;
        };
        const Hint hints[] = {{{QStringLiteral("A")}, tr("Play")},
                              {{QStringLiteral("B")}, tr("Names")},
                              {{QStringLiteral("Y")}, tr("Info")},
                              {{QStringLiteral("L"), QStringLiteral("R")}, tr("Style")}};
        const qreal radius = 9.5;
        const qreal glyph_gap = 4;
        const qreal item_gap = 22;
        qreal total = 0;
        for (const Hint& hint : hints) {
            total += hint.glyphs.size() * (radius * 2 + glyph_gap) +
                     fm.horizontalAdvance(hint.label);
        }
        total += item_gap * 3;
        const QRectF bar(w / 2.0 - total / 2 - 20, h - FooterBarHeight - 10, total + 40,
                         FooterBarHeight - 8);
        QColor bar_color = dark ? pal.window().color() : QColor(255, 255, 255);
        bar_color.setAlpha(200);
        p.setPen(Qt::NoPen);
        p.setBrush(bar_color);
        p.drawRoundedRect(bar, bar.height() / 2, bar.height() / 2);

        QColor ink = pal.text().color();
        ink.setAlpha(220);
        qreal x = bar.left() + 20;
        const qreal cy = bar.center().y();
        for (const Hint& hint : hints) {
            for (const QString& glyph : hint.glyphs) {
                DrawGlyph(&p, QPointF(x + radius, cy), radius, glyph, ink);
                x += radius * 2 + glyph_gap;
            }
            p.setFont(font_hint);
            p.setPen(ink);
            const int label_w = fm.horizontalAdvance(hint.label);
            p.drawText(QRectF(x, cy - 12, label_w + 2, 24), Qt::AlignLeft | Qt::AlignVCenter,
                       hint.label);
            x += label_w + item_gap;
        }
    }

    // Info panel for the selected game.
    if (show_info && current >= 0) {
        const GameInfo info = ReadInfo(IndexAt(current));
        QFont title_font = font();
        title_font.setBold(true);
        title_font.setPixelSize(16);
        QFont body_font = font();
        body_font.setPixelSize(13);
        const QFontMetrics tfm(title_font);
        const QFontMetrics bfm(body_font);
        const int panel_w = std::min(320, std::max(200, w / 3));
        const int inner = panel_w - 28;
        QString t1;
        QString t2;
        TwoLines(info.title, tfm, inner, &t1, &t2);
        const int title_lines = t2.isEmpty() ? 1 : 2;
        const int panel_h =
            14 + title_lines * tfm.lineSpacing() + 8 + 3 * bfm.lineSpacing() + 14;
        const QRectF panel(w - panel_w - 20, 20, panel_w, panel_h);
        QColor fill = dark ? pal.window().color().lighter(120) : QColor(255, 255, 255);
        fill.setAlpha(235);
        p.setBrush(fill);
        p.setPen(QPen(accent, 1.5));
        p.drawRoundedRect(panel, 14, 14);

        qreal y = panel.top() + 14;
        const qreal x = panel.left() + 14;
        p.setPen(pal.text().color());
        p.setFont(title_font);
        p.drawText(QRectF(x, y, inner, tfm.lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter, t1);
        y += tfm.lineSpacing();
        if (!t2.isEmpty()) {
            p.drawText(QRectF(x, y, inner, tfm.lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter,
                       t2);
            y += tfm.lineSpacing();
        }
        y += 8;
        p.setFont(body_font);
        const QString rows[] = {tr("Play time: %1").arg(info.play_time),
                                tr("Version: %1").arg(info.version),
                                tr("Program ID: %1").arg(info.program_id)};
        for (const QString& row : rows) {
            p.drawText(QRectF(x, y, inner, bfm.lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter,
                       bfm.elidedText(row, Qt::ElideRight, inner));
            y += bfm.lineSpacing();
        }
    }
}

} // namespace LibraryStyles
