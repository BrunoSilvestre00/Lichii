// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Home Row library style. Concept by inxomnyaa.

#include <algorithm>
#include <cmath>

#include <QAbstractItemModel>
#include <QColor>
#include <QDateTime>
#include <QEasingCurve>
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
#include <QPen>
#include <QResizeEvent>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>

#include "qt_common/game_list/game_list_p.h"
#include "yuzu/game/styles/home_row_style.h"
#include "yuzu/game/styles/style_helpers.h"

namespace LibraryStyles {

namespace {

constexpr double kSelectedScale = 1.08;

bool IsFolderType(GameListItemType type) {
    return type == GameListItemType::CustomDir || type == GameListItemType::SdmcDir ||
           type == GameListItemType::UserNandDir || type == GameListItemType::SysNandDir;
}

GameListItemType TypeOf(const QModelIndex& index) {
    return index.data(GameListItem::TypeRole).value<GameListItemType>();
}

/// True when a dataChanged with these roles can change the cover art.
bool ArtRolesChanged(const QList<int>& roles) {
    if (roles.isEmpty()) {
        return true;
    }
    for (const int role : roles) {
        if (role == Qt::DecorationRole || role == GameListItemPath::CardIconRole ||
            role == GameListItemPath::HasIconRole) {
            return true;
        }
    }
    return false;
}
constexpr int kAnimationMs = 180;

LibraryStyleView* Make(QWidget* p) {
    return new HomeRowStyle(p);
}

[[maybe_unused]] const bool registered =
    LibraryStyles::Register({"home_row", QT_TRANSLATE_NOOP("LibraryStyles", "Home Row"), &Make});

} // namespace

HomeRowStyle::HomeRowStyle(QWidget* parent) : LibraryStyleView(parent) {
    setFocusPolicy(Qt::StrongFocus);

    m_animation = new QVariantAnimation(this);
    m_animation->setDuration(kAnimationMs);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_offset = value.toDouble();
        update();
    });

    // The clock only needs a repaint now and then.
    m_clock = new QTimer(this);
    m_clock->setInterval(15 * 1000);
    connect(m_clock, &QTimer::timeout, this, [this] { update(); });
    m_clock->start();
}

HomeRowStyle::~HomeRowStyle() = default;

void HomeRowStyle::SetModel(QAbstractItemModel* model) {
    if (m_model) {
        disconnect(m_model.data(), nullptr, this, nullptr);
    }
    m_model = model;
    if (m_model) {
        // Population adds rows one by one: rebuild once per event loop pass.
        const auto schedule = [this] { ScheduleRebuild(); };
        connect(model, &QAbstractItemModel::modelReset, this, schedule);
        connect(model, &QAbstractItemModel::layoutChanged, this, schedule);
        connect(model, &QAbstractItemModel::rowsInserted, this, schedule);
        connect(model, &QAbstractItemModel::rowsRemoved, this, schedule);
        connect(model, &QAbstractItemModel::rowsMoved, this, schedule);
        connect(model, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                    if (ArtRolesChanged(roles)) {
                        m_cache.clear();
                    }
                    update();
                });
    }
    m_cache.clear();
    m_current = -1;
    Rebuild();
}

QModelIndex HomeRowStyle::CurrentIndex() const {
    return IndexAt(m_current);
}

QModelIndex HomeRowStyle::IndexAt(int row) const {
    if (row < 0 || row >= RowCount()) {
        return {};
    }
    return QModelIndex(m_entries[static_cast<size_t>(row)]);
}

void HomeRowStyle::SetCurrentIndex(const QModelIndex& index) {
    if (m_rebuild_pending) {
        Rebuild();
    }
    if (!index.isValid()) {
        return;
    }
    const QPersistentModelIndex wanted(index.siblingAtColumn(0));
    for (int row = 0; row < RowCount(); ++row) {
        if (m_entries[static_cast<size_t>(row)] == wanted) {
            if (row != m_current) {
                m_current = row;
                AnimateToCurrent(false);
                update();
                emit CurrentChanged(CurrentIndex());
            }
            return;
        }
    }
}

void HomeRowStyle::SetShowNames(bool show) {
    m_show_names = show;
    update();
}

void HomeRowStyle::SetShowInfo(bool show) {
    m_show_info = show;
    update();
}

int HomeRowStyle::RowCount() const {
    return static_cast<int>(m_entries.size());
}

HomeRowStyle::Metrics HomeRowStyle::ComputeMetrics() const {
    Metrics m{};
    const int by_width = static_cast<int>(width() / 5.6);
    const int by_height = static_cast<int>(height() * 0.42);
    m.side = std::max(64, std::min(by_width, by_height));
    m.step = static_cast<int>(m.side * 1.14);
    m.center_y = static_cast<int>(height() * 0.5);
    return m;
}

QRectF HomeRowStyle::CoverRect(int row, const Metrics& m) const {
    const double cx = width() / 2.0 + (row - m_offset) * m.step;
    double side = m.side;
    if (row == m_current) {
        side *= kSelectedScale;
    }
    return QRectF(cx - side / 2.0, m.center_y - side / 2.0, side, side);
}

QString HomeRowStyle::TitleAt(int row) const {
    return LibraryStyleContext::Title(IndexAt(row));
}

int HomeRowStyle::RowAt(const QPoint& pos) const {
    const int count = RowCount();
    if (count == 0) {
        return -1;
    }
    const Metrics m = ComputeMetrics();
    const int guess = static_cast<int>(std::lround(m_offset + (pos.x() - width() / 2.0) / m.step));
    for (int row = std::max(0, guess - 1); row <= std::min(count - 1, guess + 1); ++row) {
        if (CoverRect(row, m).contains(pos)) {
            return row;
        }
    }
    return -1;
}

void HomeRowStyle::MoveTo(int row) {
    const int count = RowCount();
    if (count == 0) {
        return;
    }
    row = std::clamp(row, 0, count - 1);
    if (row == m_current) {
        return;
    }
    m_current = row;
    AnimateToCurrent(true);
    update();
    emit CurrentChanged(CurrentIndex());
}

void HomeRowStyle::AnimateToCurrent(bool animate) {
    m_animation->stop();
    const double target = std::max(0, m_current);
    if (!animate) {
        m_offset = target;
        return;
    }
    m_animation->setStartValue(m_offset);
    m_animation->setEndValue(target);
    m_animation->start();
}

void HomeRowStyle::ScheduleRebuild() {
    if (m_rebuild_pending) {
        return;
    }
    m_rebuild_pending = true;
    QTimer::singleShot(0, this, [this] {
        if (m_rebuild_pending) {
            Rebuild();
        }
    });
}

void HomeRowStyle::Rebuild() {
    m_rebuild_pending = false;
    const QPersistentModelIndex previous(CurrentIndex());
    m_entries.clear();
    // Games are shown flat: top level games plus the games inside each folder row. Favorites and
    // the "Add games folder" row are not covers.
    if (m_model) {
        const int top_rows = m_model->rowCount();
        for (int r = 0; r < top_rows; ++r) {
            const QModelIndex index = m_model->index(r, 0);
            const GameListItemType type = TypeOf(index);
            if (type == GameListItemType::Game) {
                m_entries.emplace_back(index);
            } else if (IsFolderType(type)) {
                const int children = m_model->rowCount(index);
                for (int c = 0; c < children; ++c) {
                    const QModelIndex child = m_model->index(c, 0, index);
                    if (TypeOf(child) == GameListItemType::Game) {
                        m_entries.emplace_back(child);
                    }
                }
            }
        }
    }
    int row = -1;
    if (previous.isValid()) {
        for (int i = 0; i < RowCount(); ++i) {
            if (m_entries[static_cast<size_t>(i)] == previous) {
                row = i;
                break;
            }
        }
    }
    if (row < 0 && RowCount() > 0) {
        row = std::clamp(m_current, 0, RowCount() - 1);
    }
    m_current = row;
    AnimateToCurrent(false);
    update();
    if (QPersistentModelIndex(CurrentIndex()) != previous) {
        emit CurrentChanged(CurrentIndex());
    }
}

QPixmap HomeRowStyle::CoverPixmap(int row, int side) {
    const QModelIndex index = IndexAt(row);
    const qreal dpr = devicePixelRatioF();
    const int target = std::max(1, static_cast<int>(std::ceil(side * kSelectedScale * dpr)));
    const QString key = QStringLiteral("%1|%2")
                            .arg(index.data(GameListItemPath::FullPathRole).toString())
                            .arg(target);
    if (const QPixmap* cached = m_cache.object(key)) {
        return *cached;
    }

    if (!index.data(GameListItemPath::HasIconRole).toBool()) {
        return {};
    }
    QPixmap art = index.data(GameListItemPath::CardIconRole).value<QPixmap>();
    if (art.isNull()) {
        art = index.data(Qt::DecorationRole).value<QPixmap>();
    }
    if (art.isNull()) {
        return {};
    }

    QPixmap result(target, target);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    const double radius = target * 0.06;
    clip.addRoundedRect(QRectF(0, 0, target, target), radius, radius);
    painter.setClipPath(clip);
    QSizeF scaled(art.size());
    scaled.scale(QSizeF(target, target), Qt::KeepAspectRatioByExpanding);
    painter.drawPixmap(QRectF((target - scaled.width()) / 2.0, (target - scaled.height()) / 2.0,
                              scaled.width(), scaled.height()),
                       art, QRectF(QPointF(0, 0), QSizeF(art.size())));
    painter.end();
    result.setDevicePixelRatio(dpr);
    m_cache.insert(key, new QPixmap(result));
    return result;
}

void HomeRowStyle::DrawPlaceholder(QPainter& painter, const QRectF& rect, const QString& title) {
    const QPalette pal = palette();
    QPainterPath path;
    const double radius = rect.width() * 0.06;
    path.addRoundedRect(rect, radius, radius);
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, pal.color(QPalette::Mid).lighter(115));
    gradient.setColorAt(1.0, pal.color(QPalette::Mid).darker(130));
    painter.fillPath(path, gradient);
    QFont font = painter.font();
    font.setPixelSize(std::max(12, static_cast<int>(rect.width() * 0.4)));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(pal.color(QPalette::WindowText));
    painter.drawText(rect, Qt::AlignCenter, title.left(1).toUpper());
}

void HomeRowStyle::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPalette pal = palette();
    const QColor base = pal.color(QPalette::Window);
    QLinearGradient background(0, 0, 0, height());
    background.setColorAt(0.0, base.lighter(104));
    background.setColorAt(1.0, base.darker(104));
    painter.fillRect(rect(), background);

    const QColor accent = pal.color(QPalette::Highlight);
    const Metrics m = ComputeMetrics();
    const int count = RowCount();

    // Status cluster: the clock only.
    {
        QFont font = painter.font();
        font.setPixelSize(std::max(13, m.side / 9));
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(pal.color(QPalette::WindowText));
        const QString time =
            QLocale().toString(QDateTime::currentDateTime().time(), QLocale::ShortFormat);
        painter.drawText(rect().adjusted(0, m.side / 8, -m.side / 4, 0),
                         Qt::AlignTop | Qt::AlignRight, time);
    }

    if (count == 0) {
        QFont font = painter.font();
        font.setPixelSize(std::max(14, m.side / 8));
        painter.setFont(font);
        painter.setPen(pal.color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, tr("No games to show"));
        return;
    }

    // Only the covers that can be on screen are drawn.
    const int half = static_cast<int>(std::ceil(width() / 2.0 / m.step)) + 2;
    const int first = std::max(0, static_cast<int>(std::floor(m_offset)) - half);
    const int last = std::min(count - 1, static_cast<int>(std::ceil(m_offset)) + half);

    for (int row = first; row <= last; ++row) {
        if (row == m_current) {
            continue; // drawn last so it sits above its neighbors
        }
        const QRectF cover = CoverRect(row, m);
        if (cover.right() < 0 || cover.left() > width()) {
            continue;
        }
        const QPixmap pix = CoverPixmap(row, m.side);
        if (pix.isNull()) {
            DrawPlaceholder(painter, cover, TitleAt(row));
        } else {
            painter.drawPixmap(cover, pix, QRectF(QPointF(0, 0), QSizeF(pix.size())));
        }
    }

    if (m_current >= 0 && m_current < count) {
        const QModelIndex index = IndexAt(m_current);
        const QRectF cover = CoverRect(m_current, m);
        const double radius = cover.width() * 0.06;

        // Soft shadow: a few stacked translucent rounded rects.
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 14));
        for (int i = 6; i >= 1; --i) {
            const double grow = i * cover.width() * 0.012;
            painter.drawRoundedRect(cover.adjusted(-grow, -grow * 0.4, grow, grow * 1.6),
                                    radius + grow, radius + grow);
        }

        const QString title = TitleAt(m_current);
        const QPixmap pix = CoverPixmap(m_current, m.side);
        if (pix.isNull()) {
            DrawPlaceholder(painter, cover, title);
        } else {
            painter.drawPixmap(cover, pix, QRectF(QPointF(0, 0), QSizeF(pix.size())));
        }

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(accent, std::max(2.0, cover.width() * 0.018)));
        painter.drawRoundedRect(cover, radius, radius);

        // Title and info centred under the row.
        int text_y = static_cast<int>(cover.bottom()) + m.side / 10;
        if (m_show_names) {
            QFont font = painter.font();
            font.setPixelSize(std::max(15, m.side / 8));
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(pal.color(QPalette::WindowText));
            const QFontMetrics fm(font);
            const QRect text(static_cast<int>(width() * 0.06), text_y,
                             static_cast<int>(width() * 0.88), fm.height());
            painter.drawText(text, Qt::AlignHCenter | Qt::AlignVCenter,
                             fm.elidedText(title, Qt::ElideRight, text.width()));
            text_y += fm.height() + 4;
        }

        if (m_show_info) {
            const QString info = LibraryStyleContext::InfoLine(index);
            if (!info.isEmpty()) {
                QFont font = painter.font();
                font.setPixelSize(std::max(12, m.side / 11));
                font.setBold(false);
                painter.setFont(font);
                QColor muted = pal.color(QPalette::WindowText);
                muted.setAlphaF(0.7);
                painter.setPen(muted);
                const QFontMetrics fm(font);
                const QRect text(static_cast<int>(width() * 0.08), text_y,
                                 static_cast<int>(width() * 0.84), fm.height() + 4);
                painter.drawText(text, Qt::AlignHCenter | Qt::AlignTop,
                                 fm.elidedText(info, Qt::ElideRight, text.width()));
            }
        }
    }
}

void HomeRowStyle::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_A:
        MoveTo(m_current - 1);
        break;
    case Qt::Key_Right:
    case Qt::Key_D:
        MoveTo(m_current + 1);
        break;
    case Qt::Key_PageUp:
        MoveTo(m_current - 5);
        break;
    case Qt::Key_PageDown:
        MoveTo(m_current + 5);
        break;
    case Qt::Key_Home:
        MoveTo(0);
        break;
    case Qt::Key_End:
        MoveTo(RowCount() - 1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (const QModelIndex index = CurrentIndex(); index.isValid()) {
            emit LaunchRequested(index);
        }
        break;
    default:
        LibraryStyleView::keyPressEvent(event);
        return;
    }
    event->accept();
}

void HomeRowStyle::mousePressEvent(QMouseEvent* event) {
    setFocus();
    const int row = RowAt(event->position().toPoint());
    if (row < 0) {
        event->ignore();
        return;
    }
    MoveTo(row);
    if (event->button() == Qt::RightButton) {
        emit ContextMenuRequested(IndexAt(row), event->globalPosition().toPoint());
    }
    event->accept();
}

void HomeRowStyle::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    const int row = RowAt(event->position().toPoint());
    if (row < 0) {
        event->ignore();
        return;
    }
    MoveTo(row);
    emit LaunchRequested(IndexAt(row));
    event->accept();
}

void HomeRowStyle::wheelEvent(QWheelEvent* event) {
    const QPoint delta = event->angleDelta();
    const int amount = delta.y() != 0 ? delta.y() : delta.x();
    if (amount == 0) {
        event->ignore();
        return;
    }
    // Touchpads send many small deltas; move one cover per notch (120 units).
    m_wheel_remainder += amount;
    const int steps = m_wheel_remainder / 120;
    if (steps != 0) {
        m_wheel_remainder -= steps * 120;
        MoveTo(m_current - steps);
    }
    event->accept();
}

void HomeRowStyle::resizeEvent(QResizeEvent* event) {
    LibraryStyleView::resizeEvent(event);
    m_cache.clear();
}

} // namespace LibraryStyles
