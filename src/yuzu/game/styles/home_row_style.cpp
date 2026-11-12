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

#include <QFileInfo>
#include <fmt/format.h>

#include "common/fs/path_util.h"
#include "common/settings.h"
#include "core/constants.h"
#include "core/core.h"
#include "core/hle/service/acc/profile_manager.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/qt_common.h"
#include "yuzu/game/styles/home_row_style.h"
#include "yuzu/game/styles/style_helpers.h"

namespace LibraryStyles {

namespace {

constexpr double kSelectedScale = 1.08;
constexpr int kAnimationMs = 180;

LibraryStyleView* Make(QWidget* p) {
    return new HomeRowStyle(p);
}

[[maybe_unused]] const bool registered =
    LibraryStyles::Register({"home_row", QT_TR_NOOP("Home Row"), &Make});

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
        connect(model, &QAbstractItemModel::modelReset, this, [this] { ModelChanged(true); });
        connect(model, &QAbstractItemModel::layoutChanged, this, [this] { ModelChanged(true); });
        connect(model, &QAbstractItemModel::rowsInserted, this, [this] { ModelChanged(false); });
        connect(model, &QAbstractItemModel::rowsRemoved, this, [this] { ModelChanged(false); });
        connect(model, &QAbstractItemModel::rowsMoved, this, [this] { ModelChanged(false); });
        connect(model, &QAbstractItemModel::dataChanged, this, [this] {
            m_cache.clear();
            update();
        });
    }
    m_cache.clear();
    RebuildItems();
    m_current = RowCount() > 0 ? 0 : -1;
    AnimateToCurrent(false);
    update();
    if (m_current >= 0) {
        emit CurrentChanged(CurrentIndex());
    }
}

QModelIndex HomeRowStyle::CurrentIndex() const {
    if (!m_model || m_current < 0 || m_current >= RowCount()) {
        return {};
    }
    return IndexAt(m_current);
}

void HomeRowStyle::SetCurrentIndex(const QModelIndex& index) {
    if (!m_model || !index.isValid() || index.model() != m_model.data()) {
        return;
    }
    const auto it = std::find(m_items.begin(), m_items.end(), QPersistentModelIndex(index));
    if (it == m_items.end()) {
        return;
    }
    const int row = static_cast<int>(it - m_items.begin());
    if (row == m_current) {
        return;
    }
    m_current = row;
    AnimateToCurrent(isVisible());
    update();
    emit CurrentChanged(CurrentIndex());
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
    return static_cast<int>(m_items.size());
}

QModelIndex HomeRowStyle::IndexAt(int row) const {
    if (row < 0 || row >= RowCount()) {
        return {};
    }
    return QModelIndex(m_items[static_cast<size_t>(row)]);
}

void HomeRowStyle::RebuildItems() {
    m_items.clear();
    if (!m_model) {
        return;
    }
    QPersistentModelIndex add_folder;
    const auto type_of = [](const QModelIndex& index) {
        return index.data(GameListItem::TypeRole).value<GameListItemType>();
    };
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const QModelIndex index = m_model->index(r, 0);
        switch (type_of(index)) {
        case GameListItemType::Game:
            m_items.emplace_back(index);
            break;
        case GameListItemType::AddDir:
            add_folder = index;
            break;
        case GameListItemType::Favorites:
            break; // Its games are listed in their folders too.
        default:
            for (int c = 0; c < m_model->rowCount(index); ++c) {
                const QModelIndex child = m_model->index(c, 0, index);
                if (type_of(child) == GameListItemType::Game) {
                    m_items.emplace_back(child);
                }
            }
            break;
        }
    }
    if (add_folder.isValid()) {
        m_items.push_back(add_folder);
    }
}

HomeRowStyle::Metrics HomeRowStyle::ComputeMetrics() const {
    Metrics m{};
    // Tiles are the featured size (1.6x the icon size), shrunk only to fit the window.
    const int by_height = static_cast<int>(height() * 0.42);
    const int by_width = static_cast<int>(width() / 2.4);
    m.side = std::max(64, std::min({LibraryStyleContext::FeaturedArtSize(), by_height, by_width}));
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
    const QModelIndex index = IndexAt(row);
    QString title = index.data(GameListItemPath::TitleRole).toString();
    if (title.isEmpty()) {
        title = index.data(Qt::DisplayRole).toString();
    }
    return title;
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

void HomeRowStyle::ModelChanged(bool notify) {
    m_cache.clear();
    const QPersistentModelIndex selected = IndexAt(m_current);
    RebuildItems();
    const int count = RowCount();
    const int previous = m_current;
    if (selected.isValid()) {
        const auto it = std::find(m_items.begin(), m_items.end(), selected);
        if (it != m_items.end()) {
            m_current = static_cast<int>(it - m_items.begin());
        }
    }
    m_current = count == 0 ? -1 : std::clamp(m_current, 0, count - 1);
    AnimateToCurrent(false);
    update();
    // Row inserts arrive one by one while the library loads, so only report a real change.
    if (notify || m_current != previous) {
        emit CurrentChanged(CurrentIndex());
    }
}

QPixmap HomeRowStyle::CoverPixmap(int row, int side) {
    const QModelIndex index = IndexAt(row);
    const qreal dpr = devicePixelRatioF();
    const int target = std::max(1, static_cast<int>(std::ceil(side * kSelectedScale * dpr)));
    const QString key = QStringLiteral("%1|%2|%3")
                            .arg(index.data(GameListItemPath::FullPathRole).toString())
                            .arg(row)
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

namespace {

/// Picture of the active profile, or the default account picture; reloaded only when the
/// profile or its image file changes.
QPixmap UserAvatar() {
    static QPixmap cached;
    static QString cached_key;
    QString path;
    if (QtCommon::system) {
        const auto uuid = QtCommon::system->GetProfileManager().GetUser(
            static_cast<std::size_t>(Settings::values.current_user.GetValue()));
        if (uuid.has_value()) {
            const auto file = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) /
                              fmt::format("system/save/8000000000000010/su/avators/{}.jpg",
                                          uuid->FormattedString());
            path = QString::fromStdString(Common::FS::PathToUTF8String(file));
        }
    }
    const QFileInfo info(path);
    const QString key =
        path + QLatin1Char('|') + QString::number(info.exists() ? info.lastModified().toSecsSinceEpoch() : 0);
    if (!cached.isNull() && key == cached_key) {
        return cached;
    }
    QPixmap pixmap;
    if (!path.isEmpty() && info.exists()) {
        pixmap.load(path);
    }
    if (pixmap.isNull()) {
        pixmap.loadFromData(Core::Constants::ACCOUNT_BACKUP_JPEG.data(),
                            static_cast<uint>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
    }
    cached = pixmap;
    cached_key = key;
    return cached;
}

} // namespace

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

    const QColor accent = LibraryStyleContext::Accent(pal);
    const Metrics m = ComputeMetrics();
    const int count = RowCount();

    // Profile picture, top left.
    {
        const QPixmap avatar = UserAvatar();
        const double d = std::clamp(m.side / 3.6, 36.0, 72.0);
        const QRectF circle(m.side / 4.0, m.side / 9.0, d, d);
        if (!avatar.isNull()) {
            painter.save();
            QPainterPath clip;
            clip.addEllipse(circle);
            painter.setClipPath(clip);
            const double scale = std::max(d / avatar.width(), d / avatar.height());
            const QSizeF fitted(avatar.width() * scale, avatar.height() * scale);
            painter.drawPixmap(QRectF(circle.center() - QPointF(fitted.width(), fitted.height()) / 2,
                                      fitted),
                               avatar, QRectF(avatar.rect()));
            painter.restore();
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 230), std::max(2.0, d / 22.0)));
        painter.drawEllipse(circle);
    }

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

    if (count == 0 || !m_model) {
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

        // Title and info on one line, centred over the selected tile.
        const GameInfo game = LibraryStyleContext::Info(index);
        const QString name = m_show_names ? title : QString{};
        const QString info = m_show_info ? game.info_line : QString{};
        if (!name.isEmpty() || !info.isEmpty()) {
            QFont title_font = painter.font();
            title_font.setPixelSize(std::max(15, m.side / 8));
            title_font.setBold(true);
            QFont info_font = painter.font();
            info_font.setPixelSize(std::max(12, m.side / 13));
            info_font.setBold(true);
            const QFontMetrics title_fm(title_font);
            const QFontMetrics info_fm(info_font);
            const int gap = name.isEmpty() || info.isEmpty() ? 0 : title_fm.height() / 2;
            const int max_w = static_cast<int>(width() * 0.92);
            // Measured widths plus a pixel of slack: eliding to the exact advance can cut the
            // last word through rounding even when it fits.
            int info_w = info.isEmpty() ? 0 : info_fm.horizontalAdvance(info) + 2;
            const int name_w = std::min(name.isEmpty() ? 0 : title_fm.horizontalAdvance(name) + 2,
                                        std::max(0, max_w - info_w - gap));
            info_w = std::min(info_w, max_w - name_w - gap);
            const int total = name_w + gap + info_w;
            const int left = std::clamp(static_cast<int>(cover.center().x()) - total / 2,
                                        static_cast<int>(width() * 0.04),
                                        std::max(static_cast<int>(width() * 0.04),
                                                 static_cast<int>(width() * 0.96) - total));
            const int baseline =
                static_cast<int>(cover.top()) - m.side / 14 - title_fm.descent();
            if (!name.isEmpty()) {
                painter.setFont(title_font);
                painter.setPen(accent);
                painter.drawText(QPoint(left, baseline),
                                 title_fm.elidedText(name, Qt::ElideRight, name_w));
            }
            if (!info.isEmpty()) {
                QColor muted = pal.color(QPalette::WindowText);
                muted.setAlphaF(0.6);
                painter.setFont(info_font);
                painter.setPen(muted);
                painter.drawText(QPoint(left + name_w + gap, baseline),
                                 info_fm.elidedText(info, Qt::ElideRight, info_w));
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
