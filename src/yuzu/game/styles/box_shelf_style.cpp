// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Box Shelf library style. Concept by inxomnyaa.

#include <algorithm>
#include <cmath>
#include <QAbstractItemModel>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygonF>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QStringList>
#include <QTimer>
#include <QTransform>
#include <QWheelEvent>
#include <QtMath>

#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "yuzu/game/styles/box_shelf_style.h"

namespace LibraryStyles {

namespace {

constexpr double kCaseAspect = 0.62;
constexpr double kTopMargin = 14.0;
constexpr double kBottomMargin = 24.0;

struct Rng {
    quint32 state = 0x9E3779B9u;
    quint32 Next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    double F() {
        return static_cast<double>(Next() & 0xFFFFFFu) / 16777216.0;
    }
};

int ItemType(const QModelIndex& index) {
    return index.data(GameListItem::TypeRole).toInt();
}

bool IsGame(const QModelIndex& index) {
    return ItemType(index) == static_cast<int>(GameListItemType::Game);
}

bool IsFolderType(int type) {
    return type == static_cast<int>(GameListItemType::CustomDir) ||
           type == static_cast<int>(GameListItemType::SdmcDir) ||
           type == static_cast<int>(GameListItemType::UserNandDir) ||
           type == static_cast<int>(GameListItemType::SysNandDir);
}

QString TitleOf(const QModelIndex& index) {
    const QString full = index.data(GameListItemPath::TitleRole).toString();
    return full.isEmpty() ? index.data(Qt::DisplayRole).toString().section(QLatin1Char('
'), 0, 0)
                          : full;
}

/// Stable identity of a game, used as the cache key base.
QString ItemKey(const QModelIndex& index) {
    return index.data(GameListItemPath::FullPathRole).toString() + QLatin1Char('|') +
           QString::number(index.data(GameListItemPath::ProgramIdRole).toULongLong());
}

/// Reads "label: value" style tooltip line `line` and returns the value, or empty when the line has
/// no label (for example "Never Played").
QString TooltipValue(const QModelIndex& index, int line) {
    const QStringList lines = index.data(Qt::ToolTipRole).toString().split(QLatin1Char('
'));
    if (line < 0 || line >= lines.size()) {
        return {};
    }
    const qsizetype colon = lines.at(line).indexOf(QLatin1Char(':'));
    return colon < 0 ? QString() : lines.at(line).mid(colon + 1).trimmed();
}

QPixmap CoverOf(const QModelIndex& index) {
    if (!index.data(GameListItemPath::HasIconRole).toBool()) {
        return {};
    }
    QPixmap pixmap = index.data(GameListItemPath::CardIconRole).value<QPixmap>();
    if (pixmap.isNull()) {
        pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
    }
    return pixmap;
}

QColor PlaceholderColor(const QString& title) {
    const uint hue = static_cast<uint>(qHash(title) % 360u);
    return QColor::fromHsl(static_cast<int>(hue), 150, 104);
}

QString Initials(const QString& title) {
    QString result;
    const QStringList words = title.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& word : words) {
        for (const QChar ch : word) {
            if (ch.isLetterOrNumber()) {
                result.append(ch.toUpper());
                break;
            }
        }
        if (result.size() >= 2) {
            break;
        }
    }
    return result;
}

/// Fills `r` with the cover art (cropped to fill) or a generic placeholder.
void DrawCover(QPainter* p, const QRectF& r, const QModelIndex& index, const QString& title) {
    p->save();
    p->setClipRect(r, Qt::IntersectClip);
    const QPixmap cover = CoverOf(index);
    if (!cover.isNull()) {
        const QSizeF scaled = QSizeF(cover.size()).scaled(r.size(), Qt::KeepAspectRatioByExpanding);
        const QRectF target(r.center().x() - scaled.width() / 2,
                            r.center().y() - scaled.height() / 2, scaled.width(),
                            scaled.height());
        p->drawPixmap(target, cover, QRectF(cover.rect()));
        p->restore();
        return;
    }

    {
        const QColor base = PlaceholderColor(title);
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0.0, base.lighter(130));
        g.setColorAt(1.0, base.darker(170));
        p->fillRect(r, g);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255, 28));
        p->drawEllipse(QPointF(r.left() + r.width() * 0.78, r.top() + r.height() * 0.2),
                       r.width() * 0.42, r.width() * 0.42);
        QFont f = p->font();
        f.setBold(true);
        f.setPixelSize(std::max(8, static_cast<int>(r.width() * 0.4)));
        p->setFont(f);
        p->setPen(QColor(255, 255, 255, 215));
        p->drawText(r, Qt::AlignCenter, Initials(title));
    }
    p->restore();
}

void DrawTag(QPainter* p, const QPointF& right_top, const QString& text, const QFont& font) {
    const QFontMetrics fm(font);
    const double w = fm.horizontalAdvance(text) + 14.0;
    const double h = fm.height() + 6.0;
    p->save();
    p->translate(right_top.x() - w / 2.0 + w * 0.12, right_top.y() + h / 2.0);
    p->rotate(-5.0);
    const QRectF rect(-w / 2.0, -h / 2.0, w, h);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0, 0, 0, 60));
    p->drawRoundedRect(rect.translated(1.5, 2.0), h / 2.2, h / 2.2);
    p->setBrush(QColor(255, 238, 170));
    p->setPen(QPen(QColor(120, 90, 20, 140), 1.0));
    p->drawRoundedRect(rect, h / 2.2, h / 2.2);
    p->setFont(font);
    p->setPen(QColor(64, 44, 8));
    p->drawText(rect, Qt::AlignCenter, text);
    p->restore();
}

} // namespace

BoxShelfStyle::BoxShelfStyle(QWidget* parent) : LibraryStyleView(parent), m_pixmaps(128 * 1024) {
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

BoxShelfStyle::~BoxShelfStyle() = default;

void BoxShelfStyle::SetModel(QAbstractItemModel* model) {
    if (m_model) {
        QObject::disconnect(m_model.data(), nullptr, this, nullptr);
    }
    m_model = model;
    m_current = QPersistentModelIndex();
    m_current_key.clear();
    m_scroll = 0.0;
    m_gen.clear();
    if (model) {
        const auto schedule = [this] { ScheduleRebuild(); };
        connect(model, &QAbstractItemModel::modelReset, this, schedule);
        connect(model, &QAbstractItemModel::rowsInserted, this, schedule);
        connect(model, &QAbstractItemModel::rowsRemoved, this, schedule);
        connect(model, &QAbstractItemModel::rowsMoved, this, schedule);
        connect(model, &QAbstractItemModel::layoutChanged, this, schedule);
        connect(model, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex& top_left, const QModelIndex& bottom_right) {
                    InvalidateRange(top_left, bottom_right);
                    update();
                });
        connect(model, &QObject::destroyed, this, [this] {
            m_entries.clear();
            m_current = QPersistentModelIndex();
            m_current_key.clear();
            m_pixmaps.clear();
            m_colors.clear();
            m_gen.clear();
            update();
        });
    }
    RebuildRows();
    update();
}

QModelIndex BoxShelfStyle::CurrentIndex() const {
    return QModelIndex(m_current);
}

void BoxShelfStyle::SetCurrentIndex(const QModelIndex& index) {
    EnsureRows();
    if (!m_model) {
        return;
    }
    if (!index.isValid()) {
        if (m_current.isValid()) {
            m_current = QPersistentModelIndex();
            m_current_key.clear();
            update();
            NotifyCurrent();
        }
        return;
    }
    if (index.model() != m_model.data()) {
        return;
    }
    const QPersistentModelIndex wanted(index.siblingAtColumn(0));
    for (int i = 0; i < ItemCount(); ++i) {
        if (m_entries[static_cast<size_t>(i)] == wanted) {
            SetCurrentPos(i);
            return;
        }
    }
}

void BoxShelfStyle::SetShowNames(bool show) {
    if (m_show_names == show) {
        return;
    }
    m_show_names = show;
    ClampScroll();
    update();
}

void BoxShelfStyle::SetShowInfo(bool show) {
    if (m_show_info == show) {
        return;
    }
    m_show_info = show;
    update();
}

QString BoxShelfStyle::CycleOption() {
    m_spines = !m_spines;
    ClampScroll();
    const int pos = CurrentPos();
    if (pos >= 0) {
        EnsureVisible(pos);
    }
    update();
    return m_spines ? tr("Cases: spines") : tr("Cases: front");
}

// ---------------------------------------------------------------------------------------------
// Model tracking
// ---------------------------------------------------------------------------------------------

void BoxShelfStyle::ScheduleRebuild() {
    if (m_rebuild_pending) {
        return;
    }
    m_rebuild_pending = true;
    QTimer::singleShot(0, this, [this] {
        if (m_rebuild_pending) {
            RebuildRows();
            update();
        }
    });
}

void BoxShelfStyle::EnsureRows() {
    if (m_rebuild_pending) {
        RebuildRows();
    }
}

void BoxShelfStyle::InvalidateRange(const QModelIndex& top_left, const QModelIndex& bottom_right) {
    if (!m_model || !top_left.isValid() || !bottom_right.isValid() || top_left.column() > 0 ||
        bottom_right.column() < 0) {
        return;
    }
    const QModelIndex parent = top_left.parent();
    for (int row = top_left.row(); row <= bottom_right.row(); ++row) {
        const QModelIndex idx = m_model->index(row, 0, parent);
        if (!idx.isValid() || !IsGame(idx)) {
            continue;
        }
        const QString key = ItemKey(idx);
        ++m_gen[key];
        m_colors.remove(key);
    }
}

void BoxShelfStyle::NotifyCurrent() {
    if (m_notifying) {
        return;
    }
    m_notifying = true;
    emit CurrentChanged(QModelIndex(m_current));
    m_notifying = false;
}

void BoxShelfStyle::RebuildRows() {
    m_rebuild_pending = false;
    m_entries.clear();
    // Cached pixmaps stay valid: they are keyed by game identity, size and a per game generation.

    if (!m_model) {
        m_current = QPersistentModelIndex();
        m_current_key.clear();
        return;
    }

    // Games are shown flat: top level games plus the games inside each folder row. Favorites and
    // the "add folder" row are not shown.
    const int top_rows = m_model->rowCount();
    for (int r = 0; r < top_rows; ++r) {
        const QModelIndex idx = m_model->index(r, 0);
        const int type = ItemType(idx);
        if (type == static_cast<int>(GameListItemType::Game)) {
            m_entries.emplace_back(idx);
        } else if (IsFolderType(type)) {
            const int children = m_model->rowCount(idx);
            for (int c = 0; c < children; ++c) {
                const QModelIndex child = m_model->index(c, 0, idx);
                if (IsGame(child)) {
                    m_entries.emplace_back(child);
                }
            }
        }
    }
    ClampScroll();

    if (CurrentPos() >= 0) {
        return;
    }
    // The current game may have come back as a new index (model reset): find it by identity.
    if (!m_current_key.isEmpty()) {
        for (int i = 0; i < ItemCount(); ++i) {
            if (ItemKey(IndexAtPos(i)) == m_current_key) {
                m_current = m_entries[static_cast<size_t>(i)];
                EnsureVisible(i);
                return;
            }
        }
    }
    if (m_entries.empty()) {
        const bool had = m_current.isValid();
        m_current = QPersistentModelIndex();
        m_current_key.clear();
        if (had) {
            NotifyCurrent();
        }
    } else {
        m_current = m_entries.front();
        m_current_key = ItemKey(QModelIndex(m_current));
        EnsureVisible(0);
        NotifyCurrent();
    }
}

// ---------------------------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------------------------

BoxShelfStyle::Metrics BoxShelfStyle::ComputeMetrics() const {
    Metrics m;
    const double w = std::max(1, width());
    const double usable = std::max(60.0, w - 48.0);
    if (!m_spines) {
        m.cols = std::max(1, static_cast<int>(usable / 260.0));
        m.cell_w = usable / m.cols;
        m.case_w = std::min(m.cell_w * 0.72, 300.0);
        m.case_h = m.case_w / kCaseAspect;
        m.bubble_h = m_show_names ? 24.0 : 0.0;
    } else {
        const double spine_w = std::clamp(w / 56.0, 26.0, 44.0);
        m.case_w = spine_w;
        m.case_h = spine_w * 7.0;
        m.cell_w = spine_w + 5.0;
        m.cols = std::max(1, static_cast<int>(usable / m.cell_w));
        m.bubble_h = 0.0;
    }
    m.left = (w - m.cols * m.cell_w) / 2.0;
    m.top_pad = 20.0;
    m.top_face = std::max(8.0, m.case_w * 0.05);
    m.plank_h = m.top_face + std::max(14.0, m.case_w * 0.09);
    const double plank_visible = m.plank_h - m.top_face * 0.55;
    m.row_h = m.top_pad + m.case_h + std::max(plank_visible, m.bubble_h + 4.0) + 16.0;
    return m;
}

QModelIndex BoxShelfStyle::IndexAtPos(int pos) const {
    if (pos < 0 || pos >= ItemCount()) {
        return {};
    }
    return QModelIndex(m_entries[static_cast<size_t>(pos)]);
}

int BoxShelfStyle::CurrentPos() const {
    if (!m_current.isValid()) {
        return -1;
    }
    for (int i = 0; i < ItemCount(); ++i) {
        if (m_entries[static_cast<size_t>(i)] == m_current) {
            return i;
        }
    }
    return -1;
}

QRectF BoxShelfStyle::ItemRect(const Metrics& m, int pos) const {
    const int row = pos / m.cols;
    const int col = pos % m.cols;
    const double x = m.left + col * m.cell_w + (m.cell_w - m.case_w) / 2.0;
    const double y = kTopMargin + row * m.row_h + m.top_pad - m_scroll;
    return QRectF(x, y, m.case_w, m.case_h);
}

int BoxShelfStyle::HitTest(const QPointF& point) const {
    const int count = ItemCount();
    if (count == 0) {
        return -1;
    }
    const Metrics m = ComputeMetrics();
    const double fy = point.y() + m_scroll - kTopMargin;
    const double fx = point.x() - m.left;
    if (fy < 0.0 || fx < 0.0) {
        return -1;
    }
    const int row = static_cast<int>(fy / m.row_h);
    const int col = static_cast<int>(fx / m.cell_w);
    if (col >= m.cols) {
        return -1;
    }
    const int pos = row * m.cols + col;
    if (pos < 0 || pos >= count) {
        return -1;
    }
    const QRectF rect = ItemRect(m, pos).adjusted(-4, -12, 4, 6);
    return rect.contains(point) ? pos : -1;
}

double BoxShelfStyle::MaxScroll(const Metrics& m) const {
    const int count = ItemCount();
    const int rows = (count + m.cols - 1) / m.cols;
    const double total = kTopMargin + rows * m.row_h + kBottomMargin;
    return std::max(0.0, total - height());
}

void BoxShelfStyle::ClampScroll() {
    m_scroll = std::clamp(m_scroll, 0.0, MaxScroll(ComputeMetrics()));
}

void BoxShelfStyle::EnsureVisible(int pos) {
    if (pos < 0 || pos >= ItemCount()) {
        return;
    }
    const Metrics m = ComputeMetrics();
    const int row = pos / m.cols;
    const double top = kTopMargin + row * m.row_h;
    const double bottom = top + m.row_h;
    if (top < m_scroll) {
        m_scroll = top;
    } else if (bottom > m_scroll + height()) {
        m_scroll = bottom - height();
    }
    ClampScroll();
}

void BoxShelfStyle::SetCurrentPos(int pos) {
    const int count = ItemCount();
    if (count == 0) {
        return;
    }
    pos = std::clamp(pos, 0, count - 1);
    if (pos == CurrentPos()) {
        EnsureVisible(pos);
        update();
        return;
    }
    m_current = m_entries[static_cast<size_t>(pos)];
    m_current_key = ItemKey(QModelIndex(m_current));
    EnsureVisible(pos);
    update();
    NotifyCurrent();
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

void BoxShelfStyle::keyPressEvent(QKeyEvent* event) {
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        QWidget::keyPressEvent(event);
        return;
    }
    EnsureRows();
    const int count = ItemCount();
    if (count == 0) {
        QWidget::keyPressEvent(event);
        return;
    }
    const Metrics m = ComputeMetrics();
    int pos = CurrentPos();
    if (pos < 0) {
        pos = 0;
    }
    const int last_row = (count - 1) / m.cols;
    const int visible_rows = std::max(1, static_cast<int>(height() / m.row_h));

    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_A:
        SetCurrentPos(pos - 1);
        break;
    case Qt::Key_Right:
    case Qt::Key_D:
        SetCurrentPos(pos + 1);
        break;
    case Qt::Key_Up:
    case Qt::Key_W:
        if (pos - m.cols >= 0) {
            SetCurrentPos(pos - m.cols);
        }
        break;
    case Qt::Key_Down:
    case Qt::Key_S:
        if (pos + m.cols < count) {
            SetCurrentPos(pos + m.cols);
        } else if (pos / m.cols < last_row) {
            SetCurrentPos(count - 1);
        }
        break;
    case Qt::Key_PageUp:
        SetCurrentPos(std::max(0, pos - m.cols * visible_rows));
        break;
    case Qt::Key_PageDown:
        SetCurrentPos(std::min(count - 1, pos + m.cols * visible_rows));
        break;
    case Qt::Key_Home:
        SetCurrentPos(0);
        break;
    case Qt::Key_End:
        SetCurrentPos(count - 1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_current.isValid()) {
            emit LaunchRequested(QModelIndex(m_current));
        }
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void BoxShelfStyle::mousePressEvent(QMouseEvent* event) {
    EnsureRows();
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        const int pos = HitTest(event->position());
        if (pos >= 0) {
            SetCurrentPos(pos);
            if (event->button() == Qt::RightButton && m_current.isValid()) {
                emit ContextMenuRequested(QModelIndex(m_current),
                                          event->globalPosition().toPoint());
            }
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void BoxShelfStyle::mouseDoubleClickEvent(QMouseEvent* event) {
    EnsureRows();
    if (event->button() == Qt::LeftButton) {
        const int pos = HitTest(event->position());
        if (pos >= 0) {
            SetCurrentPos(pos);
            if (m_current.isValid()) {
                emit LaunchRequested(QModelIndex(m_current));
            }
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void BoxShelfStyle::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        event->ignore();
        return;
    }
    // Touchpads report exact pixels; a notched wheel reports 120 units per step.
    double dy = 0.0;
    if (!event->pixelDelta().isNull()) {
        dy = event->pixelDelta().y();
    } else {
        dy = event->angleDelta().y() * 0.6;
    }
    if (dy != 0.0) {
        m_scroll -= dy;
        ClampScroll();
        update();
    }
    event->accept();
}

void BoxShelfStyle::resizeEvent(QResizeEvent* event) {
    LibraryStyleView::resizeEvent(event);
    ClampScroll();
    update();
}

// ---------------------------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------------------------

bool BoxShelfStyle::IsDark() const {
    return palette().window().color().lightness() < 128;
}

void BoxShelfStyle::paintEvent([[maybe_unused]] QPaintEvent* event) {
    EnsureRows();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const bool dark = IsDark();
    DrawWall(p, dark);

    const int count = ItemCount();
    if (count == 0) {
        p.setPen(palette().text().color());
        p.drawText(rect(), Qt::AlignCenter, tr("No games to show"));
        return;
    }

    ClampScroll();
    const Metrics m = ComputeMetrics();
    const int rows = (count + m.cols - 1) / m.cols;
    const int first_row =
        std::clamp(static_cast<int>((m_scroll - kTopMargin) / m.row_h), 0, rows - 1);
    const int last_row =
        std::clamp(static_cast<int>((m_scroll + height() - kTopMargin) / m.row_h), 0, rows - 1);
    const int selected_pos = CurrentPos();

    for (int row = first_row; row <= last_row; ++row) {
        DrawPlank(p, m, row, dark);
        const int begin = row * m.cols;
        const int end = std::min(count, begin + m.cols);
        for (int pos = begin; pos < end; ++pos) {
            if (pos != selected_pos) {
                DrawItem(p, m, pos, false, dark);
            }
        }
    }
    if (selected_pos >= 0 && selected_pos / m.cols >= first_row - 1 &&
        selected_pos / m.cols <= last_row + 1) {
        DrawItem(p, m, selected_pos, true, dark);
    }

    // Slim scroll indicator.
    const double max_scroll = MaxScroll(m);
    if (max_scroll > 0.0) {
        const double h = height();
        const double total = h + max_scroll;
        const double handle = std::max(28.0, h * h / total);
        const double y = (h - handle) * (m_scroll / max_scroll);
        QColor c = palette().text().color();
        c.setAlpha(80);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(width() - 7.0, y + 2.0, 4.0, handle - 4.0), 2, 2);
    }
}

void BoxShelfStyle::DrawWall(QPainter& p, bool dark) {
    const QColor base = dark ? QColor(56, 40, 31) : QColor(214, 184, 146);
    const QColor win = palette().window().color();
    const QColor mixed((base.red() * 4 + win.red()) / 5, (base.green() * 4 + win.green()) / 5,
                       (base.blue() * 4 + win.blue()) / 5);
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0.0, mixed.lighter(108));
    g.setColorAt(1.0, mixed.darker(116));
    p.fillRect(rect(), g);
}

QPixmap BoxShelfStyle::WoodPixmap(int w, int h, int top_face, bool dark) {
    const double dpr = devicePixelRatioF();
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                            .arg(w)
                            .arg(h)
                            .arg(top_face)
                            .arg(dark ? 1 : 0)
                            .arg(dpr);
    const auto found = m_wood.constFind(key);
    if (found != m_wood.constEnd()) {
        return found.value();
    }
    if (m_wood.size() > 6) {
        m_wood.clear();
    }

    QPixmap pm(QSize(std::max(1, qCeil(w * dpr)), std::max(1, qCeil(h * dpr))));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor base = dark ? QColor(104, 72, 44) : QColor(178, 130, 80);
    QLinearGradient top(0, 0, 0, top_face);
    top.setColorAt(0.0, base.lighter(128));
    top.setColorAt(1.0, base.lighter(110));
    p.fillRect(QRectF(0, 0, w, top_face), top);
    QLinearGradient front(0, top_face, 0, h);
    front.setColorAt(0.0, base);
    front.setColorAt(1.0, base.darker(138));
    p.fillRect(QRectF(0, top_face, w, h - top_face), front);

    Rng rng;
    const int lines = std::max(12, w / 14);
    for (int i = 0; i < lines; ++i) {
        const bool on_top = rng.F() < 0.3;
        const double y0 = on_top ? 1.0 : top_face + 1.0;
        const double y1 = on_top ? top_face - 1.0 : h - 1.0;
        const double y = y0 + rng.F() * std::max(1.0, y1 - y0);
        const double x0 = rng.F() * w;
        const double len = 80.0 + rng.F() * 320.0;
        const double wob = (rng.F() - 0.5) * 3.0;
        QPainterPath path;
        path.moveTo(x0, y);
        path.cubicTo(x0 + len * 0.3, y + wob, x0 + len * 0.6, y - wob, x0 + len, y + wob * 0.5);
        const bool light = rng.F() < 0.25;
        const QColor c = light ? QColor(255, 232, 196, 22)
                               : QColor(40, 22, 8, 12 + static_cast<int>(rng.F() * 34));
        p.setPen(QPen(c, 0.7 + rng.F() * 0.9));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    for (int i = 0; i < 3; ++i) {
        const double x = rng.F() * w;
        p.setPen(QPen(QColor(30, 16, 6, 70), 1.0));
        p.drawLine(QPointF(x, top_face), QPointF(x, h));
    }
    p.setPen(QPen(QColor(255, 240, 210, 90), 1.0));
    p.drawLine(QPointF(0, 0.5), QPointF(w, 0.5));
    p.setPen(QPen(QColor(255, 235, 200, 70), 1.0));
    p.drawLine(QPointF(0, top_face + 0.5), QPointF(w, top_face + 0.5));
    p.setPen(QPen(QColor(20, 10, 2, 120), 1.0));
    p.drawLine(QPointF(0, h - 0.5), QPointF(w, h - 0.5));
    p.end();

    m_wood.insert(key, pm);
    return pm;
}

void BoxShelfStyle::DrawPlank(QPainter& p, const Metrics& m, int row, bool dark) {
    const double y =
        kTopMargin + row * m.row_h + m.top_pad + m.case_h - m.top_face * 0.55 - m_scroll;
    const QPixmap wood = WoodPixmap(std::max(1, width()), std::max(1, qCeil(m.plank_h)),
                                    std::max(1, qCeil(m.top_face)), dark);
    p.drawPixmap(QRectF(0, y, width(), m.plank_h), wood, QRectF(wood.rect()));

    QLinearGradient shade(0, y + m.plank_h, 0, y + m.plank_h + 12.0);
    shade.setColorAt(0.0, QColor(0, 0, 0, 80));
    shade.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.fillRect(QRectF(0, y + m.plank_h, width(), 12.0), shade);
}

QColor BoxShelfStyle::SpineColor(const QModelIndex& index) {
    const QString key = ItemKey(index);
    const auto found = m_colors.constFind(key);
    if (found != m_colors.constEnd()) {
        return found.value();
    }
    QColor result;
    const QPixmap cover = CoverOf(index);
    if (cover.isNull()) {
        result = PlaceholderColor(TitleOf(index)).darker(190);
    } else {
        const QImage img = cover.toImage()
                               .scaled(8, 8, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                               .convertToFormat(QImage::Format_RGB32);
        qint64 r = 0, g = 0, b = 0;
        for (int y = 0; y < img.height(); ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                r += qRed(line[x]);
                g += qGreen(line[x]);
                b += qBlue(line[x]);
            }
        }
        const qint64 n = std::max(1, img.width() * img.height());
        result = QColor(static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(b / n))
                     .darker(190);
    }
    m_colors.insert(key, result);
    return result;
}

QString BoxShelfStyle::PixmapKey(QChar kind, const QModelIndex& index, const Metrics& m, bool dark,
                                 double dpr) const {
    // Built by concatenation: a game path may contain "%1", which QString::arg would substitute.
    const QString base = ItemKey(index);
    return QString(kind) + QLatin1Char('|') + base + QLatin1Char('|') +
           QString::number(m_gen.value(base)) + QLatin1Char('|') +
           QString::number(qRound(m.case_w * 10)) + QLatin1Char('|') +
           QString::number(dark ? 1 : 0) + QLatin1Char('|') + QString::number(dpr);
}

QPixmap BoxShelfStyle::CasePixmap(const QModelIndex& index, const Metrics& m, bool dark) {
    const double dpr = devicePixelRatioF();
    const QString key = PixmapKey(QLatin1Char('C'), index, m, dark, dpr);
    if (const QPixmap* cached = m_pixmaps.object(key)) {
        return *cached;
    }

    const double w = m.case_w;
    const double h = m.case_h;
    QPixmap pm(QSize(std::max(1, qCeil(w * dpr)), std::max(1, qCeil(h * dpr))));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const double radius = w * 0.045;
    const double spine_w = w * 0.07;
    const QColor spine = SpineColor(index);
    const QRectF full(0, 0, w, h);
    QPainterPath shape;
    shape.addRoundedRect(full.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    p.setClipPath(shape);
    QLinearGradient spine_g(0, 0, spine_w, 0);
    spine_g.setColorAt(0.0, spine.darker(130));
    spine_g.setColorAt(1.0, spine.lighter(112));
    p.fillRect(QRectF(0, 0, spine_w, h), spine_g);

    const QRectF art(spine_w, 0, w - spine_w, h);
    DrawCover(&p, art, index, TitleOf(index));

    // Soft sheen over the art, and the crease where the spine meets the cover.
    QLinearGradient sheen(0, 0, w, h);
    sheen.setColorAt(0.0, QColor(255, 255, 255, 46));
    sheen.setColorAt(0.35, QColor(255, 255, 255, 0));
    sheen.setColorAt(1.0, QColor(0, 0, 0, 36));
    p.fillRect(art, sheen);
    p.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
    p.drawLine(QPointF(spine_w, 0), QPointF(spine_w, h));
    p.setPen(QPen(QColor(255, 255, 255, 50), 1.0));
    p.drawLine(QPointF(spine_w + 1.0, 0), QPointF(spine_w + 1.0, h));
    p.setClipping(false);

    // Plastic edge highlight and thin outline.
    QLinearGradient edge(0, 0, w, h);
    edge.setColorAt(0.0, QColor(255, 255, 255, 190));
    edge.setColorAt(0.5, QColor(255, 255, 255, 40));
    edge.setColorAt(1.0, QColor(255, 255, 255, 110));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 110), 1.0));
    p.drawPath(shape);
    p.setPen(QPen(QBrush(edge), 1.3));
    QPainterPath inner;
    inner.addRoundedRect(full.adjusted(1.4, 1.4, -1.4, -1.4), radius, radius);
    p.drawPath(inner);
    p.end();

    const int cost = std::max(1, pm.width() * pm.height() * 4 / 1024);
    m_pixmaps.insert(key, new QPixmap(pm), cost);
    return pm;
}

QPixmap BoxShelfStyle::SpinePixmap(const QModelIndex& index, const Metrics& m, bool dark) {
    const double dpr = devicePixelRatioF();
    const QString key = PixmapKey(QLatin1Char('S'), index, m, dark, dpr);
    if (const QPixmap* cached = m_pixmaps.object(key)) {
        return *cached;
    }

    const double w = m.case_w;
    const double h = m.case_h;
    QPixmap pm(QSize(std::max(1, qCeil(w * dpr)), std::max(1, qCeil(h * dpr))));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QColor base = SpineColor(index);
    const QString title = TitleOf(index);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0), 3.0, 3.0);
    p.setClipPath(shape);

    QLinearGradient g(0, 0, w, 0);
    g.setColorAt(0.0, base.lighter(128));
    g.setColorAt(0.35, base.lighter(106));
    g.setColorAt(0.7, base);
    g.setColorAt(1.0, base.darker(140));
    p.fillRect(QRectF(0, 0, w, h), g);

    // Small cover thumbnail at the top, title running down the spine.
    const QRectF thumb(3.0, 3.0, w - 6.0, w - 6.0);
    DrawCover(&p, thumb, index, title);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 70), 1.0));
    p.drawRect(thumb);

    const bool light_bg = qGray(base.rgb()) > 140;
    QFont f = font();
    f.setBold(true);
    f.setPixelSize(std::clamp(static_cast<int>(w * 0.4), 10, 16));
    p.setFont(f);
    p.setPen(light_bg ? QColor(24, 20, 16) : QColor(250, 246, 238));
    const double top = w + 2.0;
    const double avail = std::max(10.0, h - top - 6.0);
    p.save();
    p.translate(w / 2.0, top);
    p.rotate(90.0);
    const QString elided =
        QFontMetrics(f).elidedText(title, Qt::ElideRight, static_cast<int>(avail));
    p.drawText(QRectF(0, -w / 2.0, avail, w), Qt::AlignVCenter | Qt::AlignLeft, elided);
    p.restore();

    p.setClipping(false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 120), 1.0));
    p.drawPath(shape);
    p.setPen(QPen(QColor(255, 255, 255, 80), 1.0));
    p.drawLine(QPointF(1.5, 3.0), QPointF(1.5, h - 3.0));
    p.end();

    const int cost = std::max(1, pm.width() * pm.height() * 4 / 1024);
    m_pixmaps.insert(key, new QPixmap(pm), cost);
    return pm;
}

/// Name bubble under the selected case: wraps to two lines, accent outline.
void BoxShelfStyle::DrawBubble(QPainter& p, const Metrics& m, const QRectF& base,
                               const QString& title, bool dark) {
    if (title.isEmpty()) {
        return;
    }
    QFont f = font();
    f.setBold(true);
    f.setPixelSize(std::clamp(static_cast<int>(m.case_w * 0.075), 11, 15));
    const QFontMetrics fm(f);
    const QColor accent = palette().highlight().color();
    const double y = base.bottom() + 4.0;
    const double max_w = std::max(120.0, std::min(m.cell_w * 1.4, width() - 16.0));
    const QRect text_bounds = fm.boundingRect(QRect(0, 0, static_cast<int>(max_w - 24.0), 1000),
                                              Qt::AlignCenter | Qt::TextWordWrap, title);
    const double text_h = std::min<double>(text_bounds.height(), fm.lineSpacing() * 2);
    const double bw = std::max(80.0, text_bounds.width() + 24.0);
    double x = base.center().x() - bw / 2.0;
    x = std::clamp(x, 4.0, std::max(4.0, width() - bw - 4.0));
    const QRectF box(x, y, bw, text_h + 14.0);

    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 55));
    p.drawRoundedRect(box.translated(0, 2), 11, 11);
    p.setBrush(dark ? QColor(34, 32, 34, 236) : QColor(255, 253, 248, 240));
    p.setPen(QPen(accent, 2.0));
    p.drawRoundedRect(box, 11, 11);
    p.setFont(f);
    p.setPen(dark ? QColor(240, 238, 234) : QColor(40, 34, 28));
    p.setClipRect(box.adjusted(6, 4, -6, -4));
    p.drawText(box.adjusted(12, 7, -12, -7), Qt::AlignCenter | Qt::TextWordWrap, title);
    p.restore();
}

/// Small plain title on the shelf front under an unselected case.
void BoxShelfStyle::DrawPlainTitle(QPainter& p, const Metrics& m, const QRectF& base,
                                   const QString& title) {
    if (title.isEmpty()) {
        return;
    }
    QFont f = font();
    f.setPixelSize(std::clamp(static_cast<int>(m.case_w * 0.065), 10, 13));
    const QFontMetrics fm(f);
    const double w = std::min(m.cell_w * 0.98, m.case_w * 1.12);
    const QRectF box(base.center().x() - w / 2.0, base.bottom() + 3.0, w, m.bubble_h - 4.0);
    const QString text = fm.elidedText(title, Qt::ElideRight, static_cast<int>(w));
    p.save();
    p.setFont(f);
    p.setPen(QColor(20, 10, 2, 170));
    p.drawText(box.translated(0, 1), Qt::AlignHCenter | Qt::AlignTop, text);
    p.setPen(QColor(255, 244, 222));
    p.drawText(box, Qt::AlignHCenter | Qt::AlignTop, text);
    p.restore();
}

void BoxShelfStyle::DrawItem(QPainter& p, const Metrics& m, int pos, bool selected, bool dark) {
    const QModelIndex index = IndexAtPos(pos);
    if (!index.isValid()) {
        return;
    }
    const QRectF base = ItemRect(m, pos);
    if (base.bottom() < -60.0 || base.top() > height() + 60.0) {
        return;
    }
    const double lift = selected ? std::max(6.0, m.case_w * 0.04) : 0.0;
    const QRectF r = base.translated(0, -lift);
    const double w = m.case_w;
    const double h = m.case_h;
    const QColor accent = palette().highlight().color();

    // Contact shadow on the plank.
    p.save();
    p.translate(base.center().x(), base.bottom() - 1.0);
    p.scale(1.0, selected ? 0.22 : 0.16);
    const double shadow_r = w * (selected ? 0.72 : 0.62);
    QRadialGradient shadow(QPointF(0, 0), shadow_r);
    shadow.setColorAt(0.0, QColor(0, 0, 0, selected ? 175 : 125));
    shadow.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(shadow);
    p.drawEllipse(QPointF(0, 0), shadow_r, shadow_r);
    p.restore();

    // Slight perspective: the far (right) edge is a little shorter.
    QTransform t = QTransform::fromTranslate(r.x(), r.y());
    if (!m_spines) {
        const double k = h * 0.014;
        QPolygonF dst;
        dst << r.topLeft() << QPointF(r.right(), r.top() + k) << QPointF(r.right(), r.bottom() - k)
            << r.bottomLeft();
        QPolygonF src;
        src << QPointF(0, 0) << QPointF(w, 0) << QPointF(w, h) << QPointF(0, h);
        QTransform quad;
        if (QTransform::quadToQuad(src, dst, quad)) {
            t = quad;
        }
    }
    QPainterPath outline;
    outline.addRoundedRect(QRectF(0, 0, w, h), w * 0.045, w * 0.045);
    const QPainterPath mapped = t.map(outline);

    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, selected ? 70 : 42));
    p.drawPath(mapped.translated(selected ? 6.0 : 3.0, selected ? lift + 3.0 : 3.0));
    p.restore();

    const QPixmap pix = m_spines ? SpinePixmap(index, m, dark) : CasePixmap(index, m, dark);
    p.save();
    p.setTransform(t, true);
    p.drawPixmap(QRectF(0, 0, w, h), pix, QRectF(pix.rect()));
    p.restore();

    if (selected) {
        p.save();
        p.setBrush(Qt::NoBrush);
        QColor glow = accent;
        glow.setAlpha(80);
        p.setPen(QPen(glow, 7.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(mapped);
        p.setPen(QPen(accent, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(mapped);
        p.restore();
    }

    // Names: a bubble for the selected case, plain small titles for the others (front mode).
    if (m_show_names) {
        if (selected) {
            DrawBubble(p, m, base, TitleOf(index), dark);
        } else if (!m_spines) {
            DrawPlainTitle(p, m, base, TitleOf(index));
        }
    }

    // Play time and version tags, top right of the case.
    if (m_show_info && IsGame(index) && (!m_spines || selected)) {
        // Play time and version are only exposed through the tooltip: "Play Time: X" (or "Never
        // Played"), then "Version: Y".
        QStringList tags;
        const QString play = TooltipValue(index, 0);
        if (!play.isEmpty()) {
            tags << play;
        }
        const QString version = TooltipValue(index, 1);
        if (!version.isEmpty()) {
            tags << QStringLiteral("v") + version;
        }
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(std::clamp(static_cast<int>(m.case_w * 0.065), 10, 13));
        const QFontMetrics fm(f);
        double ty = r.top() + 10.0;
        const double tag_x = m_spines ? r.right() + 14.0 : r.right() - 2.0;
        for (const QString& tag : tags) {
            DrawTag(&p, QPointF(tag_x, ty), tag, f);
            ty += fm.height() + 10.0;
        }
    }
}

namespace {

LibraryStyleView* Make(QWidget* parent) {
    return new BoxShelfStyle(parent);
}

[[maybe_unused]] const bool registered =
    LibraryStyles::Register({"box_shelf", QT_TR_NOOP("Box Shelf"), &Make});

} // namespace

} // namespace LibraryStyles
