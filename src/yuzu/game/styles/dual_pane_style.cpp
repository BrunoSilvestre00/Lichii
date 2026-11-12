// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Dual Pane library style. Concept by inxomnyaa.

#include <algorithm>
#include <cmath>

#include <QAbstractItemModel>
#include <QColor>
#include <QDateTime>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QResizeEvent>
#include <QStringList>
#include <QWheelEvent>

#include "qt_common/game_list/game_list_p.h"
#include "yuzu/game/styles/dual_pane_style.h"

namespace LibraryStyles {

namespace {

constexpr int Margin = 12;
constexpr int PanePad = 14;
constexpr int StatusHeight = 26;
constexpr int PaneRadius = 16;
constexpr int DotSpacing = 18;
constexpr int FadeMs = 150;

bool IsDark(const QPalette& palette) {
    return palette.window().color().lightness() < 128;
}

bool IsGameType(int type) {
    return type == static_cast<int>(GameListItemType::Game);
}

bool IsFolderType(int type) {
    return type == static_cast<int>(GameListItemType::CustomDir) ||
           type == static_cast<int>(GameListItemType::SdmcDir) ||
           type == static_cast<int>(GameListItemType::UserNandDir) ||
           type == static_cast<int>(GameListItemType::SysNandDir);
}

QString TitleOf(const QModelIndex& index) {
    QString title = index.data(GameListItemPath::TitleRole).toString();
    if (title.isEmpty()) {
        title = index.data(Qt::DisplayRole).toString();
    }
    return title;
}

QPixmap SourceArt(const QModelIndex& index) {
    QPixmap art = index.data(GameListItemPath::CardIconRole).value<QPixmap>();
    if (art.isNull() && index.data(GameListItemPath::HasIconRole).toBool()) {
        art = index.data(Qt::DecorationRole).value<QPixmap>();
    }
    return art;
}

} // namespace

DualPaneStyle::DualPaneStyle(QWidget* parent)
    : LibraryStyleView(parent), art_cache_(48 * 1024 * 1024) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 280);

    fade_.setDuration(FadeMs);
    fade_.setStartValue(0.0);
    fade_.setEndValue(1.0);
    fade_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&fade_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        fade_value_ = value.toReal();
        update(top_rect_);
    });
    connect(&fade_, &QVariantAnimation::finished, this, [this] {
        fade_value_ = 1.0;
        previous_ = QPersistentModelIndex();
        update(top_rect_);
    });

    clock_timer_.setInterval(30 * 1000);
    connect(&clock_timer_, &QTimer::timeout, this, [this] { update(top_rect_); });
    clock_timer_.start();

    rebuild_timer_.setSingleShot(true);
    rebuild_timer_.setInterval(0);
    connect(&rebuild_timer_, &QTimer::timeout, this, [this] { Rebuild(); });
}

DualPaneStyle::~DualPaneStyle() = default;

void DualPaneStyle::SetModel(QAbstractItemModel* model) {
    if (model_) {
        disconnect(model_, nullptr, this, nullptr);
    }
    model_ = model;
    if (model_) {
        connect(model_, &QAbstractItemModel::modelReset, this, [this] { ScheduleRebuild(); });
        connect(model_, &QAbstractItemModel::layoutChanged, this, [this] { ScheduleRebuild(); });
        connect(model_, &QAbstractItemModel::rowsInserted, this, [this] { ScheduleRebuild(); });
        connect(model_, &QAbstractItemModel::rowsRemoved, this, [this] { ScheduleRebuild(); });
        connect(model_, &QAbstractItemModel::rowsMoved, this, [this] { ScheduleRebuild(); });
        connect(model_, &QAbstractItemModel::dataChanged, this, [this] { ScheduleRebuild(); });
        connect(model_, &QObject::destroyed, this, [this] {
            model_ = nullptr;
            items_.clear();
            current_ = -1;
            shown_ = QPersistentModelIndex();
            previous_ = QPersistentModelIndex();
            update();
        });
    }
    art_cache_.clear();
    current_ = -1;
    shown_ = QPersistentModelIndex();
    previous_ = QPersistentModelIndex();
    Rebuild();
}

QModelIndex DualPaneStyle::CurrentIndex() const {
    if (current_ < 0 || current_ >= static_cast<int>(items_.size())) {
        return {};
    }
    return QModelIndex(items_[static_cast<size_t>(current_)]);
}

void DualPaneStyle::SetCurrentIndex(const QModelIndex& index) {
    if (!index.isValid()) {
        return;
    }
    const QModelIndex first = index.siblingAtColumn(0);
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i] == first) {
            SetCurrentRow(static_cast<int>(i), true);
            return;
        }
    }
}

void DualPaneStyle::SetShowNames(bool show) {
    if (show_names_ == show) {
        return;
    }
    show_names_ = show;
    ComputeLayout();
    ClampScroll();
    EnsureVisible();
    update();
}

void DualPaneStyle::SetShowInfo(bool show) {
    if (show_info_ == show) {
        return;
    }
    show_info_ = show;
    update(top_rect_);
}

QString DualPaneStyle::OptionLabel() const {
    switch (icon_level_) {
    case 0:
        return tr("Icons: small");
    case 2:
        return tr("Icons: large");
    default:
        return tr("Icons: medium");
    }
}

QString DualPaneStyle::CycleOption() {
    icon_level_ = (icon_level_ + 1) % 3;
    art_cache_.clear();
    ComputeLayout();
    ClampScroll();
    EnsureVisible();
    update();
    return OptionLabel();
}

void DualPaneStyle::ScheduleRebuild() {
    if (!rebuild_timer_.isActive()) {
        rebuild_timer_.start();
    }
}

void DualPaneStyle::Rebuild() {
    const QModelIndex old_current = CurrentIndex();
    const bool had_current = current_ >= 0;
    items_.clear();

    if (model_) {
        const int rows = model_->rowCount();
        items_.reserve(static_cast<size_t>(rows));
        for (int r = 0; r < rows; ++r) {
            const QModelIndex top = model_->index(r, 0);
            const int type = top.data(GameListItem::TypeRole).toInt();
            if (IsGameType(type)) {
                items_.emplace_back(top);
            } else if (IsFolderType(type)) {
                const int children = model_->rowCount(top);
                for (int c = 0; c < children; ++c) {
                    const QModelIndex child = model_->index(c, 0, top);
                    if (IsGameType(child.data(GameListItem::TypeRole).toInt())) {
                        items_.emplace_back(child);
                    }
                }
            }
        }
    }

    int row = -1;
    if (old_current.isValid()) {
        for (size_t i = 0; i < items_.size(); ++i) {
            if (items_[i] == old_current) {
                row = static_cast<int>(i);
                break;
            }
        }
    }
    if (row < 0 && !current_path_.isEmpty()) {
        // The old index died in a model reset: find the same game again by its path.
        for (size_t i = 0; i < items_.size(); ++i) {
            if (items_[i].data(GameListItemPath::FullPathRole).toString() == current_path_) {
                row = static_cast<int>(i);
                break;
            }
        }
    }
    if (row < 0 && !items_.empty()) {
        row = 0;
    }

    ComputeLayout();
    current_ = row;
    const QModelIndex now = CurrentIndex();
    shown_ = QPersistentModelIndex(now);
    previous_ = QPersistentModelIndex();
    current_path_ = now.isValid() ? now.data(GameListItemPath::FullPathRole).toString() : QString();
    ClampScroll();
    EnsureVisible();
    update();
    if (now != old_current || (had_current && !now.isValid())) {
        emit CurrentChanged(now);
    }
}

void DualPaneStyle::ComputeLayout() {
    const int w = width();
    const int h = height();
    const int total_h = std::max(0, h - 3 * Margin);
    const int top_h = static_cast<int>(total_h * 0.45);
    top_rect_ = QRect(Margin, Margin, std::max(0, w - 2 * Margin), top_h);
    grid_pane_ = QRect(Margin, top_rect_.bottom() + 1 + Margin, top_rect_.width(),
                       std::max(0, total_h - top_h));
    grid_area_ = grid_pane_.adjusted(PanePad - 4, PanePad - 4, -(PanePad - 4), -(PanePad - 4));

    static constexpr int base_px[3] = {52, 72, 100};
    icon_px_ = base_px[std::clamp(icon_level_, 0, 2)];
    const int gap_w = icon_px_ / 4 + 6;
    cols_ = std::max(1, (grid_area_.width() + gap_w) / (icon_px_ + gap_w));
    cell_w_ = std::max(1, grid_area_.width() / cols_);
    const QFontMetrics fm(font());
    cell_h_ = icon_px_ + 14 + (show_names_ ? fm.height() + 4 : 0);
}

QRect DualPaneStyle::CellRect(int row) const {
    const int col = row % cols_;
    const int line = row / cols_;
    return QRect(grid_area_.left() + col * cell_w_, grid_area_.top() + line * cell_h_ - scroll_,
                 cell_w_, cell_h_);
}

int DualPaneStyle::RowAt(const QPoint& pos) const {
    if (!grid_area_.contains(pos) || items_.empty()) {
        return -1;
    }
    const int col = (pos.x() - grid_area_.left()) / cell_w_;
    const int line = (pos.y() - grid_area_.top() + scroll_) / cell_h_;
    if (col < 0 || col >= cols_ || line < 0) {
        return -1;
    }
    const int row = line * cols_ + col;
    return row < static_cast<int>(items_.size()) ? row : -1;
}

void DualPaneStyle::ClampScroll() {
    const int lines = (static_cast<int>(items_.size()) + cols_ - 1) / std::max(1, cols_);
    const int content = lines * cell_h_;
    const int max_scroll = std::max(0, content - grid_area_.height());
    scroll_ = std::clamp(scroll_, 0, max_scroll);
}

void DualPaneStyle::EnsureVisible() {
    if (current_ < 0 || grid_area_.height() <= 0) {
        return;
    }
    const int top = (current_ / cols_) * cell_h_;
    const int bottom = top + cell_h_;
    if (top < scroll_) {
        scroll_ = top;
    } else if (bottom > scroll_ + grid_area_.height()) {
        scroll_ = bottom - grid_area_.height();
    }
    ClampScroll();
}

void DualPaneStyle::SetCurrentRow(int row, bool emit_signal) {
    if (items_.empty()) {
        return;
    }
    row = std::clamp(row, 0, static_cast<int>(items_.size()) - 1);
    if (row == current_) {
        EnsureVisible();
        update();
        return;
    }
    fade_.stop();
    const QPersistentModelIndex old_shown = shown_;
    current_ = row;
    shown_ = items_[static_cast<size_t>(row)];
    current_path_ = shown_.data(GameListItemPath::FullPathRole).toString();
    previous_ = old_shown;
    fade_value_ = previous_.isValid() ? 0.0 : 1.0;
    if (previous_.isValid()) {
        fade_.start();
    }
    EnsureVisible();
    update();
    if (emit_signal) {
        emit CurrentChanged(CurrentIndex());
    }
}

QPixmap DualPaneStyle::Art(const QModelIndex& index, int logical_px) {
    const qreal dpr = devicePixelRatioF();
    const int px = std::max(1, static_cast<int>(std::lround(logical_px * dpr)));
    const QPixmap source = SourceArt(index);
    const QString title = TitleOf(index);
    const QString key = source.isNull()
                            ? QStringLiteral("n|%1|%2").arg(title).arg(px)
                            : QStringLiteral("%1|%2").arg(source.cacheKey()).arg(px);
    if (QPixmap* cached = art_cache_.object(key)) {
        return *cached;
    }

    QImage image(px, px, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath path;
        const qreal radius = px * 0.16;
        path.addRoundedRect(QRectF(0, 0, px, px), radius, radius);
        p.setClipPath(path);
        if (source.isNull()) {
            const int hue = static_cast<int>(qHash(title) % 360);
            p.fillRect(QRect(0, 0, px, px), QColor::fromHsl(hue, 90, 90));
            QFont f = font();
            f.setPixelSize(std::max(8, px / 2));
            f.setBold(true);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 230));
            const QString initial = title.isEmpty() ? QStringLiteral("?") : title.left(1).toUpper();
            p.drawText(QRect(0, 0, px, px), Qt::AlignCenter, initial);
        } else {
            const QPixmap scaled =
                source.scaled(px, px, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            p.drawPixmap((px - scaled.width()) / 2, (px - scaled.height()) / 2, scaled);
        }
    }
    QPixmap result = QPixmap::fromImage(image);
    result.setDevicePixelRatio(dpr);
    art_cache_.insert(key, new QPixmap(result),
                      static_cast<int>(std::min<qint64>(qint64(px) * px * 4, 1 << 30)));
    return result;
}

void DualPaneStyle::DrawDots(QPainter& painter, const QRect& area, const QColor& color) const {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const qreal r = 2.2;
    for (int y = area.top() + DotSpacing / 2; y < area.bottom(); y += DotSpacing) {
        const int shift = (((y - area.top()) / DotSpacing) % 2) * (DotSpacing / 2);
        for (int x = area.left() + DotSpacing / 2 + shift; x < area.right(); x += DotSpacing) {
            painter.drawEllipse(QPointF(x, y), r, r);
        }
    }
}

void DualPaneStyle::DrawPreview(QPainter& painter, const QModelIndex& index, qreal opacity) {
    if (!index.isValid() || opacity <= 0.0) {
        return;
    }
    const QPalette pal = palette();
    painter.save();
    painter.setOpacity(opacity);

    const QRect content = top_rect_.adjusted(PanePad, StatusHeight + 4, -PanePad, -PanePad);
    const int side =
        std::max(0, std::min(content.height(), static_cast<int>(content.width() * 0.4)));
    const QRect cover(content.left(), content.top() + (content.height() - side) / 2, side, side);

    if (side > 8) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 40));
        painter.drawRoundedRect(cover.translated(0, 4), side * 0.16, side * 0.16);
        painter.drawPixmap(cover.topLeft(), Art(index, side));
    }

    const QRect text_area(cover.right() + PanePad + 6, content.top(),
                          std::max(0, content.right() - cover.right() - PanePad - 6),
                          content.height());
    if (text_area.width() > 20) {
        QFont title_font = font();
        title_font.setPixelSize(std::clamp(content.height() / 6, 16, 36));
        title_font.setBold(true);
        const QFontMetrics tfm(title_font);
        const int max_title_h = tfm.lineSpacing() * 3;
        const int flags = Qt::TextWordWrap | Qt::TextWrapAnywhere | Qt::AlignLeft | Qt::AlignTop;
        const QString title = TitleOf(index);
        QRect title_box =
            tfm.boundingRect(QRect(0, 0, text_area.width(), max_title_h), flags, title);
        title_box.setHeight(std::min(title_box.height(), max_title_h));

        QStringList info;
        if (show_info_) {
            const QStringList tip =
                index.data(Qt::ToolTipRole).toString().split(QLatin1Char('\n'));
            for (const QString& line : tip) {
                if (!line.trimmed().isEmpty()) {
                    info << line.trimmed();
                }
            }
            const qulonglong pid = index.data(GameListItemPath::ProgramIdRole).toULongLong();
            if (pid != 0) {
                info << tr("Program ID: %1")
                            .arg(QString::number(pid, 16)
                                     .toUpper()
                                     .rightJustified(16, QLatin1Char('0')));
            }
        }
        QFont info_font = font();
        info_font.setPixelSize(std::clamp(content.height() / 12, 11, 16));
        const QFontMetrics ifm(info_font);
        const int info_h =
            info.isEmpty() ? 0 : static_cast<int>(info.size()) * ifm.lineSpacing() + 10;

        const int total = title_box.height() + info_h;
        int y = content.top() + std::max(0, (content.height() - total) / 2);

        painter.setFont(title_font);
        painter.setPen(pal.text().color());
        painter.setClipRect(text_area);
        painter.drawText(QRect(text_area.left(), y, text_area.width(), title_box.height()),
                         flags, title);
        y += title_box.height() + 10;

        painter.setFont(info_font);
        QColor sub = pal.text().color();
        sub.setAlpha(170);
        painter.setPen(sub);
        for (const QString& line : info) {
            painter.drawText(QRect(text_area.left(), y, text_area.width(), ifm.lineSpacing()),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             ifm.elidedText(line, Qt::ElideRight, text_area.width()));
            y += ifm.lineSpacing();
        }
    }
    painter.restore();
}

void DualPaneStyle::DrawGrid(QPainter& painter) {
    const QPalette pal = palette();
    const QColor accent = pal.highlight().color();
    const bool dark = IsDark(pal);
    const QFontMetrics fm(font());

    painter.save();
    painter.setClipRect(grid_area_);
    const int total = static_cast<int>(items_.size());
    const int first_line = std::max(0, scroll_ / cell_h_);
    const int last_line = (scroll_ + grid_area_.height()) / cell_h_;
    for (int line = first_line; line <= last_line; ++line) {
        for (int col = 0; col < cols_; ++col) {
            const int row = line * cols_ + col;
            if (row >= total) {
                break;
            }
            const QModelIndex index(items_[static_cast<size_t>(row)]);
            if (!index.isValid()) {
                continue;
            }
            const QRect cell = CellRect(row);
            const QRect tile(cell.left() + (cell.width() - icon_px_) / 2, cell.top() + 8, icon_px_,
                             icon_px_);
            const bool selected = row == current_;

            QColor card = dark ? QColor(255, 255, 255, 18) : QColor(255, 255, 255, 230);
            QColor card_border = pal.text().color();
            card_border.setAlpha(dark ? 40 : 28);
            painter.setPen(QPen(card_border, 1));
            painter.setBrush(card);
            painter.drawRoundedRect(QRectF(tile).adjusted(-3.5, -3.5, 3.5, 3.5), icon_px_ * 0.2,
                                    icon_px_ * 0.2);
            painter.drawPixmap(tile.topLeft(), Art(index, icon_px_));

            if (show_names_) {
                const QRect label(cell.left() + 2, tile.bottom() + 6, cell.width() - 4,
                                  fm.height());
                painter.setPen(pal.text().color());
                painter.setFont(font());
                painter.drawText(label, Qt::AlignHCenter | Qt::AlignVCenter,
                                 fm.elidedText(TitleOf(index), Qt::ElideRight, label.width()));
            }

            if (selected) {
                const QRectF box = QRectF(tile).adjusted(-7, -7, 7, 7);
                const qreal len = std::max(10.0, icon_px_ * 0.24);
                painter.setPen(QPen(accent, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                const QPointF tl = box.topLeft();
                const QPointF tr_pt = box.topRight();
                const QPointF bl = box.bottomLeft();
                const QPointF br = box.bottomRight();
                const QPointF c0[3] = {{tl.x(), tl.y() + len}, tl, {tl.x() + len, tl.y()}};
                const QPointF c1[3] = {{tr_pt.x() - len, tr_pt.y()},
                                       tr_pt,
                                       {tr_pt.x(), tr_pt.y() + len}};
                const QPointF c2[3] = {{bl.x(), bl.y() - len}, bl, {bl.x() + len, bl.y()}};
                const QPointF c3[3] = {{br.x() - len, br.y()}, br, {br.x(), br.y() - len}};
                painter.drawPolyline(c0, 3);
                painter.drawPolyline(c1, 3);
                painter.drawPolyline(c2, 3);
                painter.drawPolyline(c3, 3);
            }
        }
    }
    painter.restore();

    const int lines = (total + cols_ - 1) / std::max(1, cols_);
    const int content = lines * cell_h_;
    if (content > grid_area_.height() && grid_area_.height() > 0) {
        const qreal ratio = static_cast<qreal>(grid_area_.height()) / content;
        const int bar_h = std::max(24, static_cast<int>(grid_area_.height() * ratio));
        const int range = grid_area_.height() - bar_h;
        const int max_scroll = content - grid_area_.height();
        const int y = grid_area_.top() + (max_scroll > 0 ? range * scroll_ / max_scroll : 0);
        QColor bar = pal.text().color();
        bar.setAlpha(70);
        painter.setPen(Qt::NoPen);
        painter.setBrush(bar);
        painter.drawRoundedRect(QRect(grid_pane_.right() - 8, y, 4, bar_h), 2, 2);
    }
}

void DualPaneStyle::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPalette pal = palette();
    const bool dark = IsDark(pal);

    painter.fillRect(rect(), pal.window());

    const QColor pane = dark ? pal.base().color().lighter(112) : pal.base().color();
    QColor border = pal.text().color();
    border.setAlpha(dark ? 50 : 34);
    QColor dots = pal.text().color();
    dots.setAlpha(dark ? 18 : 14);

    for (const QRect& r : {top_rect_, grid_pane_}) {
        if (r.width() <= 0 || r.height() <= 0) {
            continue;
        }
        QPainterPath path;
        path.addRoundedRect(QRectF(r), PaneRadius, PaneRadius);
        painter.setPen(QPen(border, 1));
        painter.setBrush(pane);
        painter.drawPath(path);
        painter.save();
        painter.setClipPath(path);
        DrawDots(painter, r, dots);
        painter.restore();
    }

    {
        QFont f = font();
        f.setPixelSize(12);
        painter.setFont(f);
        QColor sub = pal.text().color();
        sub.setAlpha(160);
        painter.setPen(sub);
        const QRect status(top_rect_.left() + PanePad, top_rect_.top() + 6,
                           top_rect_.width() - 2 * PanePad, StatusHeight - 6);
        painter.drawText(status, Qt::AlignLeft | Qt::AlignVCenter,
                         tr("%n game(s)", nullptr, static_cast<int>(items_.size())));
        painter.drawText(status, Qt::AlignRight | Qt::AlignVCenter,
                         QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat));
    }

    if (items_.empty()) {
        painter.setPen(pal.text().color());
        painter.setFont(font());
        painter.drawText(grid_pane_, Qt::AlignCenter, tr("No games found"));
        return;
    }

    if (previous_.isValid() && fade_value_ < 1.0) {
        DrawPreview(painter, QModelIndex(previous_), 1.0 - fade_value_);
    }
    DrawPreview(painter, QModelIndex(shown_), previous_.isValid() ? fade_value_ : 1.0);

    DrawGrid(painter);
}

void DualPaneStyle::resizeEvent(QResizeEvent* event) {
    LibraryStyleView::resizeEvent(event);
    ComputeLayout();
    ClampScroll();
    EnsureVisible();
}

void DualPaneStyle::changeEvent(QEvent* event) {
    LibraryStyleView::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange ||
        event->type() == QEvent::StyleChange) {
        art_cache_.clear();
        ComputeLayout();
        ClampScroll();
        EnsureVisible();
        update();
    }
}

void DualPaneStyle::keyPressEvent(QKeyEvent* event) {
    if (items_.empty()) {
        LibraryStyleView::keyPressEvent(event);
        return;
    }
    const int count = static_cast<int>(items_.size());
    const int page = std::max(1, grid_area_.height() / std::max(1, cell_h_)) * cols_;
    int row = current_ < 0 ? 0 : current_;
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
        row = row - cols_ >= 0 ? row - cols_ : row;
        break;
    case Qt::Key_Down:
    case Qt::Key_S:
        if (row + cols_ < count) {
            row += cols_;
        } else if (row / cols_ < (count - 1) / cols_) {
            row = count - 1;
        }
        break;
    case Qt::Key_PageUp:
        row = std::max(0, row - page);
        break;
    case Qt::Key_PageDown:
        row = std::min(count - 1, row + page);
        break;
    case Qt::Key_Home:
        row = 0;
        break;
    case Qt::Key_End:
        row = count - 1;
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (current_ >= 0) {
            emit LaunchRequested(CurrentIndex());
        }
        return;
    default:
        LibraryStyleView::keyPressEvent(event);
        return;
    }
    SetCurrentRow(row, true);
}

void DualPaneStyle::mousePressEvent(QMouseEvent* event) {
    setFocus();
    const int row = RowAt(event->position().toPoint());
    if (row < 0) {
        LibraryStyleView::mousePressEvent(event);
        return;
    }
    SetCurrentRow(row, true);
    if (event->button() == Qt::RightButton) {
        emit ContextMenuRequested(CurrentIndex(), event->globalPosition().toPoint());
    }
}

void DualPaneStyle::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const int row = RowAt(event->position().toPoint());
    if (row < 0) {
        return;
    }
    SetCurrentRow(row, true);
    emit LaunchRequested(CurrentIndex());
}

void DualPaneStyle::wheelEvent(QWheelEvent* event) {
    int delta = 0;
    if (!event->pixelDelta().isNull()) {
        delta = -event->pixelDelta().y();
    } else {
        delta = -event->angleDelta().y() * cell_h_ / 240;
    }
    const int before = scroll_;
    scroll_ += delta;
    ClampScroll();
    if (scroll_ != before) {
        update(grid_pane_);
    }
    event->accept();
}

namespace {
LibraryStyleView* Make(QWidget* p) {
    return new DualPaneStyle(p);
}
[[maybe_unused]] const bool registered =
    LibraryStyles::Register({"dual_pane", QT_TR_NOOP("Dual Pane"), &Make});
} // namespace

} // namespace LibraryStyles
