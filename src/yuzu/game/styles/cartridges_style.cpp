// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Concept by inxomnyaa.

#include <algorithm>
#include <cmath>

#include <QAbstractItemModel>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QImage>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygonF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStringList>
#include <QTimer>
#include <QWheelEvent>

#include "qt_common/game_list/game_list_p.h"
#include "yuzu/game/styles/cartridges_style.h"
#include "yuzu/game/styles/style_helpers.h"

namespace LibraryStyles {

namespace {

constexpr qreal TargetCellWidth = 260.0;
constexpr qreal SideMargin = 18.0;
constexpr qreal TopMargin = 24.0;
constexpr qreal LiftAmount = 10.0;

quint32 Hash32(quint32 x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

quint64 Hash64(quint64 x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

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

bool IsDark(const QPalette& palette) {
    return palette.color(QPalette::Window).lightness() < 128;
}

QColor BodyColor(const QPalette& palette) {
    const int window_lightness = palette.color(QPalette::Window).lightness();
    const int lightness = std::clamp(window_lightness * 12 / 100 + 36, 40, 84);
    return QColor::fromHsl(220, 22, lightness);
}

qreal CardMargin(qreal card_w) {
    return card_w * 0.07;
}

QPainterPath BodyPath(qreal w, qreal h) {
    QPainterPath body;
    body.addRoundedRect(QRectF(0, 0, w, h), w * 0.08, w * 0.08);
    QPainterPath notch;
    notch.addRoundedRect(QRectF(w * 0.39, -2.0, w * 0.22, h * 0.03 + 2.0), w * 0.02, w * 0.02);
    return body.subtracted(notch);
}

/// Average colour of the cover, weighted towards saturated mid tones, then normalised so the
/// header strip is always readable.
QColor DominantColor(const QPixmap& cover, quint32 fallback_seed) {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double weight_sum = 0.0;
    if (!cover.isNull()) {
        const QImage small = cover.toImage()
                                 .scaled(12, 12, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                                 .convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < small.height(); ++y) {
            for (int x = 0; x < small.width(); ++x) {
                const QColor c = QColor::fromRgba(small.pixel(x, y));
                if (c.alpha() < 128) {
                    continue;
                }
                const double weight =
                    0.05 + c.hsvSaturationF() * (1.0 - std::abs(c.lightnessF() - 0.5) * 1.4);
                r += c.redF() * weight;
                g += c.greenF() * weight;
                b += c.blueF() * weight;
                weight_sum += weight;
            }
        }
    }
    if (weight_sum <= 0.0) {
        return QColor::fromHslF((fallback_seed % 360) / 360.0f, 0.55f, 0.45f);
    }
    const QColor average = QColor::fromRgbF(static_cast<float>(r / weight_sum),
                                            static_cast<float>(g / weight_sum),
                                            static_cast<float>(b / weight_sum));
    float hue = 0.0f;
    float sat = 0.0f;
    float light = 0.0f;
    average.getHslF(&hue, &sat, &light);
    if (hue < 0.0f) {
        return QColor::fromHslF(0.0f, 0.0f, std::clamp(light, 0.35f, 0.55f));
    }
    return QColor::fromHslF(hue, std::clamp(sat, 0.5f, 0.9f), std::clamp(light, 0.38f, 0.52f));
}

QFont FitFont(QFont font, const QString& text, qreal max_w, qreal max_h) {
    // Every card of a size asks the same question: remember the answer instead of measuring
    // the text at each pixel size again for every card.
    static QHash<QString, int> fitted;
    const QString key = font.key() + QLatin1Char('|') + text + QLatin1Char('|') +
                        QString::number(qRound(max_w * 10)) + QLatin1Char('|') +
                        QString::number(qRound(max_h * 10));
    const auto found = fitted.constFind(key);
    if (found != fitted.constEnd()) {
        font.setPixelSize(*found);
        return font;
    }
    int pixel_size = std::max(6, static_cast<int>(max_h));
    font.setPixelSize(pixel_size);
    while (pixel_size > 6 && QFontMetricsF(font).horizontalAdvance(text) > max_w) {
        --pixel_size;
        font.setPixelSize(pixel_size);
    }
    if (fitted.size() > 256) {
        fitted.clear();
    }
    fitted.insert(key, pixel_size);
    return font;
}

/// The empty cartridge shell (body gradient and outline), the same for every card of a size.
QPixmap BodyTemplate(const QColor& body_color, qreal w, qreal h, qreal dpr) {
    static QHash<QString, QPixmap> templates;
    const QString key = QStringLiteral("%1|%2|%3|%4")
                            .arg(qRound(w * 10))
                            .arg(qRound(h * 10))
                            .arg(qRound(dpr * 100))
                            .arg(body_color.rgba());
    const auto found = templates.constFind(key);
    if (found != templates.constEnd()) {
        return *found;
    }
    const qreal m = CardMargin(w);
    QPixmap pixmap(QSize(static_cast<int>(std::ceil((w + 2 * m) * dpr)),
                         static_cast<int>(std::ceil((h + 2 * m) * dpr))));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(m, m);
    QLinearGradient body_gradient(0, 0, 0, h);
    body_gradient.setColorAt(0.0, body_color.lighter(118));
    body_gradient.setColorAt(1.0, body_color.darker(118));
    p.setPen(QPen(body_color.lighter(150), 1.0));
    p.setBrush(body_gradient);
    p.drawPath(BodyPath(w, h));
    p.end();
    if (templates.size() > 8) {
        templates.clear();
    }
    templates.insert(key, pixmap);
    return pixmap;
}

struct CardData {
    QPixmap cover;
    QString title;
    QString header_text;
    QString id_text;
    QString play_time;
    QString version;
    QColor strip;
    QColor body;
    int tag_seed = 0;
};

void DrawTag(QPainter& p, const QPointF& center, const QString& text, qreal angle, qreal px) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setBold(true);
    font.setPixelSize(std::max(6, static_cast<int>(px)));
    const QFontMetricsF fm(font);
    const qreal tw = fm.horizontalAdvance(text) + px * 1.1;
    const qreal th = px * 1.7;
    p.save();
    p.translate(center);
    p.rotate(angle);
    const QRectF rect(-tw / 2, -th / 2, tw, th);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 60));
    p.drawRoundedRect(rect.translated(0, px * 0.18), th * 0.3, th * 0.3);
    p.setPen(QPen(QColor(196, 150, 40), 1.0));
    p.setBrush(QColor(255, 218, 110));
    p.drawRoundedRect(rect, th * 0.3, th * 0.3);
    p.setPen(QColor(64, 42, 8));
    p.setFont(font);
    p.drawText(rect, Qt::AlignCenter, text);
    p.restore();
}

QPixmap RenderCard(const CardData& d, qreal w, qreal h, qreal dpr) {
    const qreal m = CardMargin(w);
    QPixmap pixmap(QSize(static_cast<int>(std::ceil((w + 2 * m) * dpr)),
                         static_cast<int>(std::ceil((h + 2 * m) * dpr))));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.drawPixmap(0, 0, BodyTemplate(d.body, w, h, dpr));
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.translate(m, m);

    const qreal pad = w * 0.07;
    const QFont base_font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);

    // Header strip, coloured from the cover.
    const QRectF header(pad, h * 0.06, w - 2 * pad, h * 0.075);
    p.setPen(Qt::NoPen);
    p.setBrush(d.strip);
    p.drawRoundedRect(header, 3.0, 3.0);
    if (header.height() >= 12.0 && !d.header_text.isEmpty()) {
        QFont font =
            FitFont(base_font, d.header_text, header.width() * 0.6, header.height() * 0.55);
        font.setBold(true);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        p.setFont(font);
        p.setPen(d.strip.lightness() > 140 ? QColor(30, 30, 30) : QColor(250, 250, 250));
        p.drawText(header, Qt::AlignCenter, d.header_text);
    }

    // Label with the cover art.
    const QRectF label(pad, h * 0.145, w - 2 * pad, h * 0.69);
    QPainterPath label_path;
    label_path.addRoundedRect(label, 4.0, 4.0);
    p.save();
    p.setClipPath(label_path);
    p.fillRect(label, d.strip.darker(220));
    if (!d.cover.isNull()) {
        QSizeF scaled = QSizeF(d.cover.size()) / std::max(1.0, d.cover.devicePixelRatio());
        scaled.scale(label.size(), Qt::KeepAspectRatioByExpanding);
        const QRectF target(label.center().x() - scaled.width() / 2,
                            label.center().y() - scaled.height() / 2, scaled.width(),
                            scaled.height());
        p.drawPixmap(target, d.cover, QRectF(d.cover.rect()));
    } else {
        QLinearGradient art(label.topLeft(), label.bottomRight());
        art.setColorAt(0.0, d.strip.darker(130));
        art.setColorAt(1.0, d.strip.darker(280));
        p.fillRect(label, art);
        QFont font = base_font;
        font.setBold(true);
        font.setPixelSize(std::max(8, static_cast<int>(w * 0.09)));
        p.setFont(font);
        p.setPen(QColor(245, 245, 245));
        p.drawText(label.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, d.title);
    }
    QLinearGradient gloss(label.topLeft(), label.bottomLeft());
    gloss.setColorAt(0.0, QColor(255, 255, 255, 38));
    gloss.setColorAt(0.35, QColor(255, 255, 255, 0));
    p.fillRect(label, gloss);
    p.restore();
    p.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(label, 4.0, 4.0);

    // Program id label.
    if (!d.id_text.isEmpty()) {
        const QRectF id_rect(pad, h * 0.865, w - 2 * pad, h * 0.08);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(id_rect, 3.0, 3.0);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setBold(true);
        mono = FitFont(mono, QString(d.id_text.size(), QLatin1Char('0')), id_rect.width() - 8.0,
                       id_rect.height() * 0.6);
        p.setFont(mono);
        p.setPen(QColor(228, 230, 235));
        p.drawText(id_rect, Qt::AlignCenter, d.id_text);
    }

    // Stickers on the top right of the label.
    const qreal tag_px = std::max(7.0, h * 0.04);
    QFont tag_font = base_font;
    tag_font.setBold(true);
    tag_font.setPixelSize(static_cast<int>(tag_px));
    const QFontMetricsF tag_metrics(tag_font);
    int slot = 0;
    const auto place_tag = [&](const QString& text) {
        if (text.isEmpty()) {
            return;
        }
        const QString shown = tag_metrics.elidedText(text, Qt::ElideRight, w * 0.4);
        const qreal tw = tag_metrics.horizontalAdvance(shown) + tag_px * 1.1;
        const qreal angle = (slot % 2 == 0 ? 6.0 : -5.0) + (d.tag_seed % 3) - 1;
        DrawTag(p,
                QPointF(label.right() - tw * 0.4,
                        label.top() + tag_px * 1.2 + slot * tag_px * 2.2),
                shown, angle, tag_px);
        ++slot;
    };
    place_tag(d.play_time);
    place_tag(d.version);

    p.end();
    return pixmap;
}

} // namespace

CartridgesStyle::CartridgesStyle(QWidget* parent)
    : LibraryStyleView{parent}, card_cache_{64 * 1024 * 1024} {
    setObjectName(QStringLiteral("cartridgesStyleView"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    bar_ = new QScrollBar(Qt::Vertical, this);
    connect(bar_, &QScrollBar::valueChanged, this, [this] { update(); });
    bar_->hide();
}

CartridgesStyle::~CartridgesStyle() = default;

void CartridgesStyle::SetModel(QAbstractItemModel* model) {
    for (const QMetaObject::Connection& connection : connections_) {
        disconnect(connection);
    }
    connections_.clear();
    model_ = model;
    current_ = QPersistentModelIndex{};
    current_row_ = -1;
    entries_.clear();
    card_cache_.clear();
    if (model_) {
        // Structural changes rebuild the flat list (deferred, so a burst of inserts costs one
        // rebuild). Data changes only repaint: the card cache key covers cover, title and tooltip.
        const auto structural = [this] { ScheduleRebuild(); };
        const auto repaint = [this] { update(); };
        connections_.push_back(connect(model_, &QAbstractItemModel::dataChanged, this, repaint));
        connections_.push_back(
            connect(model_, &QAbstractItemModel::rowsInserted, this, structural));
        connections_.push_back(connect(model_, &QAbstractItemModel::rowsRemoved, this, structural));
        connections_.push_back(connect(model_, &QAbstractItemModel::rowsMoved, this, structural));
        connections_.push_back(connect(model_, &QAbstractItemModel::modelReset, this, structural));
        connections_.push_back(
            connect(model_, &QAbstractItemModel::layoutChanged, this, structural));
    }
    Rebuild();
}

QModelIndex CartridgesStyle::CurrentIndex() const {
    return current_.isValid() ? QModelIndex(current_) : QModelIndex{};
}

QModelIndex CartridgesStyle::IndexAt(int row) const {
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return {};
    }
    return QModelIndex(entries_[static_cast<size_t>(row)]);
}

void CartridgesStyle::ScheduleRebuild() {
    if (rebuild_pending_) {
        return;
    }
    rebuild_pending_ = true;
    QTimer::singleShot(0, this, [this] {
        if (rebuild_pending_) {
            Rebuild();
        }
    });
}

void CartridgesStyle::Rebuild() {
    rebuild_pending_ = false;
    const QPersistentModelIndex previous = current_;
    entries_.clear();
    if (model_) {
        // Games are shown flat: top level games plus the games inside each folder row.
        // Favorites and "add folder" rows are not cards.
        const int top_rows = model_->rowCount();
        for (int r = 0; r < top_rows; ++r) {
            const QModelIndex idx = model_->index(r, 0);
            const GameListItemType type = TypeOf(idx);
            if (IsGameType(type)) {
                entries_.emplace_back(idx);
            } else if (IsFolderType(type)) {
                const int children = model_->rowCount(idx);
                for (int c = 0; c < children; ++c) {
                    const QModelIndex child = model_->index(c, 0, idx);
                    if (IsGameType(TypeOf(child))) {
                        entries_.emplace_back(child);
                    }
                }
            }
        }
    }

    int new_row = -1;
    if (previous.isValid()) {
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i] == previous) {
                new_row = static_cast<int>(i);
                break;
            }
        }
    }
    if (new_row < 0 && !entries_.empty()) {
        new_row = std::clamp(current_row_, 0, static_cast<int>(entries_.size()) - 1);
    }
    const QModelIndex before = previous;
    current_row_ = new_row;
    current_ = new_row >= 0 ? entries_[static_cast<size_t>(new_row)] : QPersistentModelIndex{};
    UpdateScrollRange();
    if (IndexAt(new_row) != before) {
        emit CurrentChanged(IndexAt(new_row));
    }
    update();
}

void CartridgesStyle::SetCurrentIndex(const QModelIndex& index) {
    if (!model_ || !index.isValid() || index.model() != model_) {
        return;
    }
    if (rebuild_pending_) {
        Rebuild();
    }
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i] == index) {
            SetCurrentRow(static_cast<int>(i), false);
            return;
        }
    }
}

void CartridgesStyle::SetShowNames(bool show) {
    show_names_ = show;
    UpdateScrollRange();
    update();
}

void CartridgesStyle::SetShowInfo(bool show) {
    show_info_ = show;
    update();
}

QString CartridgesStyle::CycleOption() {
    scattered_ = !scattered_;
    update();
    return scattered_ ? tr("Layout: scattered") : tr("Layout: tidy");
}

int CartridgesStyle::RowCount() const {
    return static_cast<int>(entries_.size());
}

CartridgesStyle::Metrics CartridgesStyle::ComputeMetrics() const {
    Metrics m;
    const qreal avail = std::max<qreal>(1.0, width() - 2 * SideMargin);
    m.cols = std::max(1, static_cast<int>(avail / TargetCellWidth));
    m.cell_w = avail / m.cols;
    m.card_w = m.cell_w * 0.72;
    m.card_h = m.card_w * 1.3;
    m.cell_h = m.card_h + 30.0;
    m.margin_x = SideMargin;
    m.margin_top = TopMargin;
    const int n = RowCount();
    m.rows = (n + m.cols - 1) / m.cols;
    return m;
}

CartridgesStyle::Placement CartridgesStyle::Place(int row, const Metrics& m, bool selected) const {
    Placement place;
    const int col = row % m.cols;
    const int r = row / m.cols;
    place.center = QPointF(m.margin_x + (col + 0.5) * m.cell_w,
                           m.margin_top + r * m.cell_h + m.card_h / 2 + 6.0);
    if (scattered_) {
        const QModelIndex index = IndexAt(row);
        quint64 key = index.data(GameListItemPath::ProgramIdRole).toULongLong();
        if (key == 0) {
            key = qHash(index.data(Qt::DisplayRole).toString());
        }
        const quint64 h = Hash64(key);
        place.angle = static_cast<qreal>(h % 1201) / 100.0 - 6.0;
        const qreal ux = static_cast<qreal>((h >> 16) % 1001) / 1000.0 - 0.5;
        const qreal uy = static_cast<qreal>((h >> 32) % 1001) / 1000.0 - 0.5;
        place.center += QPointF(ux * 2.0 * m.cell_w * 0.05, uy * 2.0 * m.cell_h * 0.03);
    }
    if (selected) {
        place.center.ry() -= LiftAmount;
    }
    place.transform.translate(place.center.x(), place.center.y());
    place.transform.rotate(place.angle);
    place.transform.translate(-m.card_w / 2, -m.card_h / 2);
    return place;
}

int CartridgesStyle::RowAt(const QPoint& pos) const {
    const int n = RowCount();
    if (n == 0) {
        return -1;
    }
    const Metrics m = ComputeMetrics();
    const QPointF content(pos.x(), pos.y() + bar_->value());
    const auto hits = [&](int row, bool selected) {
        bool ok = false;
        const QTransform inverse = Place(row, m, selected).transform.inverted(&ok);
        return ok && QRectF(0, 0, m.card_w, m.card_h).contains(inverse.map(content));
    };
    if (current_row_ >= 0 && hits(current_row_, true)) {
        return current_row_;
    }
    const int col0 = static_cast<int>(std::floor((content.x() - m.margin_x) / m.cell_w));
    const int row0 = static_cast<int>(std::floor((content.y() - m.margin_top) / m.cell_h));
    int best = -1;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            const int c = col0 + dc;
            const int r = row0 + dr;
            if (c < 0 || c >= m.cols || r < 0) {
                continue;
            }
            const int row = r * m.cols + c;
            if (row < n && hits(row, false)) {
                // Later cards are painted on top, so prefer the last hit.
                best = row;
            }
        }
    }
    return best;
}

void CartridgesStyle::UpdateScrollRange() {
    if (!bar_) {
        return;
    }
    const Metrics m = ComputeMetrics();
    const qreal content_h = m.margin_top * 2 + m.rows * m.cell_h + (show_names_ ? 70.0 : 0.0);
    const int range = std::max(0, static_cast<int>(std::ceil(content_h)) - height());
    bar_->setRange(0, range);
    bar_->setPageStep(std::max(1, height()));
    bar_->setSingleStep(std::max(1, static_cast<int>(m.cell_h / 3)));
    bar_->setVisible(range > 0);
    const int bar_w = bar_->sizeHint().width();
    bar_->setGeometry(width() - bar_w, 0, bar_w, height());
}

void CartridgesStyle::EnsureVisible(int row) {
    if (row < 0 || row >= RowCount()) {
        return;
    }
    const Metrics m = ComputeMetrics();
    const int top = static_cast<int>(m.margin_top + (row / m.cols) * m.cell_h - 12);
    const int bottom = static_cast<int>(m.margin_top + (row / m.cols + 1) * m.cell_h +
                                        (show_names_ ? 60 : 12));
    if (top < bar_->value()) {
        bar_->setValue(top);
    } else if (bottom > bar_->value() + height()) {
        bar_->setValue(bottom - height());
    }
}

void CartridgesStyle::SetCurrentRow(int row, bool notify) {
    if (!model_ || row < 0 || row >= RowCount()) {
        return;
    }
    const QModelIndex index = IndexAt(row);
    if (!index.isValid()) {
        return;
    }
    const bool changed = current_row_ != row;
    current_ = QPersistentModelIndex(index);
    current_row_ = row;
    EnsureVisible(row);
    update();
    if (changed && notify) {
        emit CurrentChanged(index);
    }
}

void CartridgesStyle::Launch(const QModelIndex& index) {
    if (index.isValid()) {
        emit LaunchRequested(index);
    }
}

const QPixmap& CartridgesStyle::WoodTile(qreal dpr) const {
    // A fixed size tile, repeated across the view: generating a window wide texture pixel by
    // pixel on every width change stalled resizing.
    constexpr int tile_w = 512;
    const bool dark = IsDark(palette());
    const QString key = QStringLiteral("%1|%2")
                            .arg(static_cast<int>(dpr * 100))
                            .arg(static_cast<int>(dark));
    if (!wood_.isNull() && wood_key_ == key) {
        return wood_;
    }
    const int wd = static_cast<int>(std::ceil(tile_w * dpr));
    const int plank_h = std::max(8, static_cast<int>(std::lround(120.0 * dpr)));
    const int hd = plank_h * 4;
    QImage image(wd, hd, QImage::Format_RGB32);
    const float base_r = dark ? 98.0f : 214.0f;
    const float base_g = dark ? 68.0f : 170.0f;
    const float base_b = dark ? 46.0f : 120.0f;
    const int seam = std::max(1, static_cast<int>(dpr * 1.5));
    const int joint_width = std::max(1, static_cast<int>(dpr));
    constexpr float TwoPi = 6.2831853f;
    for (int y = 0; y < hd; ++y) {
        const quint32 plank = static_cast<quint32>(y / plank_h);
        const int ly = y % plank_h;
        const float tone =
            0.86f + 0.24f * static_cast<float>(Hash32(plank * 977u + 13u) & 255u) / 255.0f;
        const int joint_x = static_cast<int>(Hash32(plank * 131u + 7u) % static_cast<quint32>(wd));
        const float phase =
            static_cast<float>(Hash32(plank * 31u + 5u) & 255u) / 255.0f * TwoPi;
        const float ring =
            0.03f * std::sin(TwoPi * 2.0f * static_cast<float>(ly) / static_cast<float>(plank_h) +
                             phase);
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < wd; ++x) {
            const float streak =
                static_cast<float>(Hash32(static_cast<quint32>(x >> 5) * 7919u +
                                          static_cast<quint32>(y) * 104729u) &
                                   255u) /
                    255.0f -
                0.5f;
            const float noise =
                static_cast<float>(Hash32(static_cast<quint32>(x) * 73856093u ^
                                          static_cast<quint32>(y) * 19349663u) &
                                   255u) /
                    255.0f -
                0.5f;
            float t = tone + 0.05f * streak + 0.025f * noise + ring;
            if (ly < seam) {
                t *= 0.62f;
            } else if (ly < seam * 2) {
                t *= 0.9f;
            }
            if (std::abs(x - joint_x) < joint_width) {
                t *= 0.65f;
            }
            line[x] = qRgb(std::clamp(static_cast<int>(base_r * t), 0, 255),
                           std::clamp(static_cast<int>(base_g * t), 0, 255),
                           std::clamp(static_cast<int>(base_b * t), 0, 255));
        }
    }
    wood_ = QPixmap::fromImage(image);
    wood_.setDevicePixelRatio(dpr);
    wood_key_ = key;
    return wood_;
}

QPixmap CartridgesStyle::CardPixmap(const QModelIndex& index, const Metrics& m, qreal dpr) const {
    const auto type = TypeOf(index);
    const bool is_game = IsGameType(type);
    const QPalette pal = palette();

    QPixmap cover;
    QString tooltip;
    quint64 program_id = 0;
    QString path;
    if (is_game) {
        cover = index.data(GameListItemPath::CardIconRole).value<QPixmap>();
        if (cover.isNull() && index.data(GameListItemPath::HasIconRole).toBool()) {
            cover = index.data(Qt::DecorationRole).value<QPixmap>();
        }
        tooltip = LibraryStyleContext::InfoLine(index);
        program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
        path = index.data(GameListItemPath::FullPathRole).toString();
    }
    QString title = is_game ? index.data(GameListItemPath::TitleRole).toString() : QString{};
    if (title.isEmpty()) {
        title = index.data(Qt::DisplayRole).toString().section(QLatin1Char('\n'), 0, 0);
    }
    const QColor body = BodyColor(pal);
    const bool info = show_info_ && is_game;

    const QString key = QStringList{path,
                                    title,
                                    QString::number(program_id),
                                    QString::number(cover.cacheKey()),
                                    QString::number(static_cast<int>(type)),
                                    info ? tooltip : QString{},
                                    QString::number(static_cast<int>(m.card_w * 100)),
                                    QString::number(static_cast<int>(dpr * 100)),
                                    body.name()}
                            .join(QLatin1Char('|'));
    if (QPixmap* cached = card_cache_.object(key)) {
        return *cached;
    }

    CardData card_data;
    card_data.cover = cover;
    card_data.title = title;
    card_data.body = body;
    card_data.header_text = tr("GAME");
    if (is_game) {
        card_data.id_text = QStringLiteral("%1")
                           .arg(static_cast<qulonglong>(program_id), 16, 16, QLatin1Char('0'))
                           .toUpper();
        const QString color_key = QString::number(cover.cacheKey()) + QLatin1Char('|') + title;
        const auto found = strip_colors_.constFind(color_key);
        if (found != strip_colors_.constEnd()) {
            card_data.strip = *found;
        } else {
            if (strip_colors_.size() > 2048) {
                strip_colors_.clear();
            }
            card_data.strip = DominantColor(cover, static_cast<quint32>(qHash(title)));
            strip_colors_.insert(color_key, card_data.strip);
        }
        if (info) {
            const GameInfo game = LibraryStyleContext::Info(index);
            card_data.play_time = game.play_time_text;
            if (!game.update_version.isEmpty()) {
                card_data.version = QStringLiteral("v") + game.update_version;
            }
        }
    } else {
        card_data.strip = pal.color(QPalette::Highlight);
    }
    card_data.tag_seed = static_cast<int>(program_id % 7);

    auto* pixmap = new QPixmap(RenderCard(card_data, m.card_w, m.card_h, dpr));
    const int cost = std::max(
        1, static_cast<int>(static_cast<qint64>(pixmap->width()) * pixmap->height() * 4));
    const QPixmap result = *pixmap;
    card_cache_.insert(key, pixmap, cost);
    return result;
}

void CartridgesStyle::DrawNameBubble(QPainter& painter, const QModelIndex& index, const Metrics& m,
                                     const Placement& place) const {
    const auto type = index.data(GameListItem::TypeRole).value<GameListItemType>();
    QString title = type == GameListItemType::Game
                        ? index.data(GameListItemPath::TitleRole).toString()
                        : QString{};
    if (title.isEmpty()) {
        title = index.data(Qt::DisplayRole).toString().section(QLatin1Char('\n'), 0, 0);
    }
    if (title.isEmpty()) {
        return;
    }
    QFont bubble_font = font();
    bubble_font.setBold(true);
    const QFontMetrics fm(bubble_font);
    const int max_w = static_cast<int>(m.card_w * 1.25);
    const QRect text_box =
        fm.boundingRect(QRect(0, 0, max_w, 1000), Qt::AlignHCenter | Qt::TextWordWrap, title);
    const qreal bw = text_box.width() + 24.0;
    const qreal bh = text_box.height() + 16.0;
    const qreal arrow = 7.0;
    const qreal top = place.center.y() + m.card_h / 2 + 8.0;
    qreal left = place.center.x() - bw / 2;
    left = std::clamp(left, 6.0, std::max(6.0, width() - 6.0 - bw));
    const QRectF rect(left, top + arrow, bw, bh);
    const QPalette pal = palette();

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 55));
    painter.drawRoundedRect(rect.translated(0, 3), 10, 10);
    painter.setBrush(pal.color(QPalette::Base));
    painter.setPen(QPen(pal.color(QPalette::Highlight), 1.5));
    painter.drawRoundedRect(rect, 10, 10);
    const qreal cx = std::clamp(place.center.x(), rect.left() + 14.0, rect.right() - 14.0);
    QPolygonF tip;
    tip << QPointF(cx - arrow, rect.top() + 0.75) << QPointF(cx, rect.top() - arrow)
        << QPointF(cx + arrow, rect.top() + 0.75);
    painter.setPen(Qt::NoPen);
    painter.setBrush(pal.color(QPalette::Base));
    painter.drawPolygon(tip);
    painter.setPen(QPen(pal.color(QPalette::Highlight), 1.5));
    painter.drawLine(tip.at(0), tip.at(1));
    painter.drawLine(tip.at(1), tip.at(2));
    painter.setFont(bubble_font);
    painter.setPen(pal.color(QPalette::Text));
    painter.drawText(rect.adjusted(12, 8, -12, -8),
                     Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, title);
    painter.restore();
}

void CartridgesStyle::paintEvent([[maybe_unused]] QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const qreal dpr = devicePixelRatioF();
    const int scroll = bar_->value();

    const QPixmap& tile = WoodTile(dpr);
    const qreal tile_h = std::max<qreal>(1.0, tile.height() / dpr);
    painter.drawTiledPixmap(rect(), tile, QPointF(0, std::fmod(static_cast<qreal>(scroll), tile_h)));

    const int n = RowCount();
    if (n == 0) {
        return;
    }
    const Metrics m = ComputeMetrics();
    const qreal margin = CardMargin(m.card_w);
    const int first_row =
        std::max(0, static_cast<int>(std::floor((scroll - m.margin_top) / m.cell_h)) - 1);
    const int last_row = std::min(
        m.rows - 1, static_cast<int>(std::floor((scroll + height() - m.margin_top) / m.cell_h)) + 1);
    const int selected = current_row_;

    painter.translate(0, -scroll);
    const auto draw_card = [&](int row, bool is_selected) {
        const QModelIndex index = IndexAt(row);
        if (!index.isValid()) {
            return;
        }
        const QPixmap pixmap = CardPixmap(index, m, dpr);
        const Placement place = Place(row, m, is_selected);
        painter.save();
        painter.setTransform(place.transform, true);
        painter.setPen(Qt::NoPen);
        if (is_selected) {
            for (int i = 0; i < 6; ++i) {
                painter.setBrush(QColor(0, 0, 0, 20));
                painter.drawRoundedRect(QRectF(-i, -i + 12, m.card_w + 2 * i, m.card_h + 2 * i),
                                        m.card_w * 0.08 + i, m.card_w * 0.08 + i);
            }
        } else {
            painter.setBrush(QColor(0, 0, 0, 45));
            painter.drawRoundedRect(QRectF(0, 3, m.card_w, m.card_h), m.card_w * 0.08,
                                    m.card_w * 0.08);
        }
        painter.drawPixmap(QPointF(-margin, -margin), pixmap);
        if (is_selected) {
            QColor accent = palette().color(QPalette::Highlight);
            if (!hasFocus()) {
                accent.setAlpha(170);
            }
            painter.setPen(QPen(accent, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(BodyPath(m.card_w, m.card_h));
        }
        painter.restore();
    };

    const int end_row = std::min(n, (last_row + 1) * m.cols);
    for (int row = first_row * m.cols; row < end_row; ++row) {
        if (row != selected) {
            draw_card(row, false);
        }
    }
    if (selected >= 0 && selected < n) {
        draw_card(selected, true);
        if (show_names_) {
            DrawNameBubble(painter, IndexAt(selected), m, Place(selected, m, true));
        }
    }
}

void CartridgesStyle::resizeEvent(QResizeEvent* event) {
    LibraryStyleView::resizeEvent(event);
    UpdateScrollRange();
}

void CartridgesStyle::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton) {
        const int row = RowAt(event->position().toPoint());
        if (row >= 0) {
            SetCurrentRow(row, true);
        }
        event->accept();
        return;
    }
    LibraryStyleView::mousePressEvent(event);
}

void CartridgesStyle::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const int row = RowAt(event->position().toPoint());
        if (row >= 0) {
            SetCurrentRow(row, true);
            Launch(IndexAt(row));
        }
        event->accept();
        return;
    }
    LibraryStyleView::mouseDoubleClickEvent(event);
}

void CartridgesStyle::keyPressEvent(QKeyEvent* event) {
    const int n = RowCount();
    if (n == 0 ||
        (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        LibraryStyleView::keyPressEvent(event);
        return;
    }
    const Metrics m = ComputeMetrics();
    const int row = current_row_;
    int target = row;
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_A:
        target = row < 0 ? 0 : row - 1;
        break;
    case Qt::Key_Right:
    case Qt::Key_D:
        target = row < 0 ? 0 : row + 1;
        break;
    case Qt::Key_Up:
    case Qt::Key_W:
        target = row < 0 ? 0 : row - m.cols;
        break;
    case Qt::Key_Down:
    case Qt::Key_S:
        target = row < 0 ? 0 : row + m.cols;
        // Moving down from a full row into a short last row lands on its last game.
        if (row >= 0 && target >= n && row / m.cols < (n - 1) / m.cols) {
            target = n - 1;
        }
        break;
    case Qt::Key_Home:
        target = 0;
        break;
    case Qt::Key_End:
        target = n - 1;
        break;
    case Qt::Key_PageUp:
        target = std::max(0, std::max(row, 0) - m.cols * 2);
        break;
    case Qt::Key_PageDown:
        target = std::min(n - 1, std::max(row, 0) + m.cols * 2);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (row >= 0) {
            Launch(IndexAt(row));
            event->accept();
        } else {
            event->ignore();
        }
        return;
    default:
        LibraryStyleView::keyPressEvent(event);
        return;
    }
    if (target >= 0 && target < n) {
        SetCurrentRow(target, true);
    }
    event->accept();
}

void CartridgesStyle::wheelEvent(QWheelEvent* event) {
    int delta = 0;
    if (!event->pixelDelta().isNull()) {
        delta = event->pixelDelta().y();
    } else {
        // Accumulate so small touchpad / high resolution wheel steps are not truncated to zero.
        wheel_remainder_ += event->angleDelta().y() / 120.0 * ComputeMetrics().cell_h * 0.5;
        delta = static_cast<int>(wheel_remainder_);
        wheel_remainder_ -= delta;
    }
    if (delta != 0) {
        bar_->setValue(bar_->value() - delta);
    }
    event->accept();
}

void CartridgesStyle::contextMenuEvent(QContextMenuEvent* event) {
    int row = -1;
    QPoint global = event->globalPos();
    if (event->reason() == QContextMenuEvent::Keyboard) {
        row = current_row_;
        if (row >= 0) {
            const QPointF c = Place(row, ComputeMetrics(), true).center;
            global = mapToGlobal(
                QPoint(static_cast<int>(c.x()), static_cast<int>(c.y()) - bar_->value()));
        }
    } else {
        row = RowAt(event->pos());
    }
    event->accept();
    if (row < 0) {
        if (event->reason() != QContextMenuEvent::Keyboard) {
            emit ContextMenuRequested(QModelIndex{}, global);
        }
        return;
    }
    SetCurrentRow(row, true);
    emit ContextMenuRequested(IndexAt(row), global);
    event->accept();
}

void CartridgesStyle::focusInEvent(QFocusEvent* event) {
    LibraryStyleView::focusInEvent(event);
    update();
}

void CartridgesStyle::focusOutEvent(QFocusEvent* event) {
    LibraryStyleView::focusOutEvent(event);
    update();
}

namespace {

LibraryStyleView* Make(QWidget* parent) {
    return new CartridgesStyle(parent);
}

[[maybe_unused]] const bool registered =
    LibraryStyles::Register(
        {"cartridges", QT_TRANSLATE_NOOP("LibraryStyles", "Cartridges"), &Make});

} // namespace

} // namespace LibraryStyles
