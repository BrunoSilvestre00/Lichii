// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <vector>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLinearGradient>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QTextLayout>
#include <QVBoxLayout>

#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/game/styles/library_style_view.h"
#include "yuzu/game/styles/style_helpers.h"

namespace LibraryStyles {

namespace {

QString Tr(const char* text) {
    return QCoreApplication::translate("LibraryStyles", text);
}

QModelIndex Column(const QModelIndex& index, int column) {
    return index.sibling(index.row(), column);
}

QString PlayTimeText(u64 seconds) {
    if (seconds == 0) {
        return Tr("New");
    }
    if (seconds < 3600) {
        return Tr("%1 min").arg(std::max<u64>(1, seconds / 60));
    }
    return Tr("%1 h").arg(static_cast<double>(seconds) / 3600.0, 0, 'f', 1);
}

QString UpdateVersion(const QString& add_ons) {
    static const QRegularExpression regex{QStringLiteral(R"(^Update \(([0-9\.]+)\))"),
                                          QRegularExpression::MultilineOption};
    const auto match = regex.match(add_ons);
    return match.hasMatch() ? match.captured(1) : QString{};
}

void PaintAddFolder(QPainter* painter, const QRect& rect, const QPalette& palette) {
    const bool dark = LibraryStyleContext::IsDark(palette);
    painter->fillRect(rect, dark ? QColor(29, 30, 32) : QColor(243, 243, 244));
    const QColor accent = LibraryStyleContext::Accent(palette);
    const qreal side = std::min(rect.width(), rect.height());
    const QRectF folder(rect.center().x() - side * 0.24, rect.center().y() - side * 0.18,
                        side * 0.48, side * 0.36);
    QColor fill = accent;
    fill.setAlpha(dark ? 28 : 20);
    painter->setPen(QPen(accent, std::max(2.0, side * 0.014), Qt::SolidLine, Qt::RoundCap,
                         Qt::RoundJoin));
    painter->setBrush(fill);
    painter->drawRoundedRect(folder, side * 0.03, side * 0.03);
    const qreal plus = side * 0.08;
    const QPointF center = folder.center();
    painter->drawLine(QPointF(center.x() - plus, center.y()), QPointF(center.x() + plus, center.y()));
    painter->drawLine(QPointF(center.x(), center.y() - plus), QPointF(center.x(), center.y() + plus));
}

} // namespace

GameInfo LibraryStyleContext::Info(const QModelIndex& index) {
    GameInfo info;
    if (!index.isValid()) {
        return info;
    }
    const QModelIndex name = Column(index, GameListModel::COLUMN_NAME);
    const auto type = name.data(GameListItem::TypeRole).value<GameListItemType>();
    info.valid = true;
    if (type == GameListItemType::AddDir) {
        info.is_add_folder = true;
        info.title = Tr("Add games folder");
        return info;
    }
    if (type != GameListItemType::Game) {
        info.title = name.data(Qt::DisplayRole).toString();
        return info;
    }

    info.is_game = true;
    info.path = name.data(GameListItemPath::FullPathRole).toString();
    info.file_name = QFileInfo(info.path).completeBaseName();
    info.title = name.data(GameListItemPath::TitleRole).toString();
    if (info.title.isEmpty()) {
        info.title = info.file_name;
    }
    info.program_id = name.data(GameListItemPath::ProgramIdRole).toULongLong();
    info.program_id_text =
        QStringLiteral("%1").arg(info.program_id, 16, 16, QLatin1Char{'0'}).toUpper();
    info.file_type = name.data(GameListItemPath::FileTypeRole).toString();
    info.is_nx2 = info.file_type == QStringLiteral("NUP") || info.file_type == QStringLiteral("NX");
    info.has_icon = name.data(GameListItemPath::HasIconRole).toBool();
    info.play_time_seconds = name.data(GameListItemPath::PlayTimeSecondsRole).toULongLong();
    info.play_time_text = PlayTimeText(info.play_time_seconds);
    info.update_version = name.data(GameListItemPath::UpdateVersionRole).toString();
    if (info.update_version.isEmpty()) {
        info.update_version = UpdateVersion(
            Column(index, GameListModel::COLUMN_ADD_ONS).data(Qt::DisplayRole).toString());
    }
    info.size_text = Column(index, GameListModel::COLUMN_SIZE).data(Qt::DisplayRole).toString();

    QStringList parts;
    parts.append(info.play_time_seconds == 0 ? info.play_time_text
                                             : Tr("%1 played").arg(info.play_time_text));
    if (!info.update_version.isEmpty()) {
        parts.append(QStringLiteral("v%1").arg(info.update_version));
    }
    info.info_line = parts.join(QStringLiteral(" · "));
    return info;
}

QString LibraryStyleContext::Title(const QModelIndex& index) {
    return Info(index).title;
}

QString LibraryStyleContext::InfoLine(const QModelIndex& index) {
    return Info(index).info_line;
}

QPixmap LibraryStyleContext::Cover(const QModelIndex& index, const QSize& size,
                                   const QPalette& palette) {
    if (!index.isValid() || size.isEmpty()) {
        return {};
    }
    const QModelIndex name = Column(index, GameListModel::COLUMN_NAME);
    const auto type = name.data(GameListItem::TypeRole).value<GameListItemType>();

    QPixmap result(size);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (type == GameListItemType::AddDir) {
        PaintAddFolder(&painter, result.rect(), palette);
        return result;
    }

    QPixmap art;
    if (name.data(GameListItemPath::HasIconRole).toBool()) {
        art = name.data(GameListItemPath::CardIconRole).value<QPixmap>();
        if (art.isNull()) {
            art = name.data(Qt::DecorationRole).value<QPixmap>();
        }
    }
    if (art.isNull()) {
        QString title = name.data(GameListItemPath::TitleRole).toString();
        if (title.isEmpty()) {
            title = QFileInfo(name.data(GameListItemPath::FullPathRole).toString())
                        .completeBaseName();
        }
        PaintPlaceholderArt(&painter, result.rect(), title, palette);
        return result;
    }
    QSize scaled = art.size();
    scaled.scale(size, Qt::KeepAspectRatioByExpanding);
    const QRect target((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2,
                       scaled.width(), scaled.height());
    painter.drawPixmap(target, art);
    return result;
}

void LibraryStyleContext::PaintPlaceholderArt(QPainter* painter, const QRect& rect,
                                              const QString& title, const QPalette& palette) {
    const int hue = static_cast<int>(qHash(title) % 360);
    const bool dark = IsDark(palette);
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, QColor::fromHsl(hue, 120, dark ? 92 : 150));
    gradient.setColorAt(1.0, QColor::fromHsl((hue + 40) % 360, 130, dark ? 52 : 108));
    painter->fillRect(rect, gradient);

    QString initials;
    for (const QString& word : title.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (word.at(0).isLetterOrNumber()) {
            initials.append(word.at(0).toUpper());
        }
        if (initials.size() == 2) {
            break;
        }
    }
    if (initials.isEmpty()) {
        initials = QStringLiteral("?");
    }
    QFont font = painter->font();
    font.setBold(true);
    font.setPixelSize(std::max(10, std::min(rect.width(), rect.height()) * 2 / 5));
    painter->save();
    painter->setFont(font);
    painter->setPen(QColor(255, 255, 255, 225));
    painter->drawText(rect, Qt::AlignCenter, initials);
    painter->restore();
}

bool LibraryStyleContext::IsDark(const QPalette& palette) {
    return palette.window().color().lightness() < 128;
}

QColor LibraryStyleContext::Accent(const QPalette& palette) {
    QColor accent = palette.highlight().color();
    // Stylesheets set the highlight as a translucent selection fill (the Lichii theme uses its
    // pink at 35 %); a selection ring needs it solid.
    accent.setAlpha(255);
    // Dark themes may also use a deep highlight that disappears as a thin ring on dark art; lift
    // it to a bright tone of the same hue.
    if (IsDark(palette) && accent.lightnessF() < 0.55) {
        float h = 0, s = 0, l = 0, a = 1;
        accent.getHslF(&h, &s, &l, &a);
        accent = QColor::fromHslF(h, std::max(s, 0.8f), 0.58f, a);
    }
    return accent;
}

QColor LibraryStyleContext::Surface(const QPalette& palette) {
    return IsDark(palette) ? QColor(30, 31, 33) : QColor(255, 255, 255);
}

QColor LibraryStyleContext::MutedText(const QPalette& palette) {
    QColor color = palette.text().color();
    color.setAlpha(IsDark(palette) ? 200 : 150);
    return color;
}

int LibraryStyleContext::ArtSize() {
    return std::max(32, static_cast<int>(UISettings::values.game_cover_size.GetValue()));
}

int LibraryStyleContext::FeaturedArtSize() {
    return ArtSize() * 8 / 5;
}

QPixmap LibraryStyleContext::SoftShadow(const QSize& size, qreal radius, int blur, qreal alpha,
                                        qreal dpr) {
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1x%2|%3|%4|%5|%6")
                            .arg(size.width())
                            .arg(size.height())
                            .arg(qRound(radius))
                            .arg(blur)
                            .arg(qRound(alpha * 100))
                            .arg(qRound(dpr * 100));
    if (const auto it = cache.constFind(key); it != cache.constEnd()) {
        return *it;
    }
    if (cache.size() > 64) {
        cache.clear();
    }
    const int b = std::max(1, static_cast<int>(std::lround(blur * dpr)));
    const int w = static_cast<int>(std::lround(size.width() * dpr)) + 2 * b;
    const int h = static_cast<int>(std::lround(size.height() * dpr)) + 2 * b;
    // Alpha mask of the shape, then three box blurs (close to a Gaussian, like CSS blur).
    QImage mask(w, h, QImage::Format_Alpha8);
    mask.fill(0);
    {
        QPainter mp(&mask);
        mp.setRenderHint(QPainter::Antialiasing, true);
        mp.setPen(Qt::NoPen);
        mp.setBrush(QColor(0, 0, 0, 255));
        mp.drawRoundedRect(QRectF(b, b, w - 2 * b, h - 2 * b), radius * dpr, radius * dpr);
    }
    const int r = std::max(1, b / 3);
    std::vector<int> line(static_cast<size_t>(std::max(w, h)));
    const auto blur_pass = [&](bool horizontal) {
        const int outer = horizontal ? h : w;
        const int inner = horizontal ? w : h;
        for (int o = 0; o < outer; ++o) {
            const auto px = [&](int i) -> uchar& {
                return horizontal ? mask.scanLine(o)[i] : mask.scanLine(i)[o];
            };
            for (int i = 0; i < inner; ++i) {
                line[static_cast<size_t>(i)] = px(i);
            }
            int sum = 0;
            for (int i = -r; i <= r; ++i) {
                sum += line[static_cast<size_t>(std::clamp(i, 0, inner - 1))];
            }
            for (int i = 0; i < inner; ++i) {
                px(i) = static_cast<uchar>(sum / (2 * r + 1));
                sum += line[static_cast<size_t>(std::min(i + r + 1, inner - 1))] -
                       line[static_cast<size_t>(std::max(i - r, 0))];
            }
        }
    };
    for (int pass = 0; pass < 3; ++pass) {
        blur_pass(true);
        blur_pass(false);
    }
    QImage shadow(w, h, QImage::Format_ARGB32_Premultiplied);
    shadow.fill(Qt::transparent);
    {
        QPainter sp(&shadow);
        sp.setOpacity(std::clamp(alpha, 0.0, 1.0));
        sp.fillRect(shadow.rect(), Qt::black);
        sp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        sp.setOpacity(1.0);
        sp.drawImage(0, 0, mask);
    }
    QPixmap pixmap = QPixmap::fromImage(shadow);
    pixmap.setDevicePixelRatio(dpr);
    cache.insert(key, pixmap);
    return pixmap;
}

QPixmap LibraryStyleContext::WoodTable(const QSize& size, qreal dpr, bool dark) {
    // Warm tabletop: a radial glow from the upper left plus faint, nearly vertical grain lines.
    // It covers the whole viewport and stays put while the items scroll over it.
    static QPixmap wood_;
    static QString wood_key_;
    const QString key = QStringLiteral("%1x%2|%3|%4")
                            .arg(size.width())
                            .arg(size.height())
                            .arg(static_cast<int>(dpr * 100))
                            .arg(static_cast<int>(dark));
    if (!wood_.isNull() && wood_key_ == key) {
        return wood_;
    }
    const qreal w = std::max(1, size.width());
    const qreal h = std::max(1, size.height());
    QPixmap pixmap(static_cast<int>(std::ceil(w * dpr)), static_cast<int>(std::ceil(h * dpr)));
    pixmap.setDevicePixelRatio(dpr);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Ellipse centred at 30% / 20%, reaching the far corner (CSS "ellipse farthest-corner").
    const QPointF centre(w * 0.3, h * 0.2);
    const qreal rx = std::sqrt(2.0) * (w - centre.x());
    const qreal ry = std::sqrt(2.0) * (h - centre.y());
    QRadialGradient glow(QPointF(0, 0), 1.0);
    if (dark) {
        glow.setColorAt(0.0, QColor(0x6b, 0x4a, 0x33));
        glow.setColorAt(0.6, QColor(0x3d, 0x2a, 0x1d));
        glow.setColorAt(1.0, QColor(0x2a, 0x1c, 0x13));
    } else {
        glow.setColorAt(0.0, QColor(0xe6, 0xc9, 0xa0));
        glow.setColorAt(0.6, QColor(0xcd, 0xa5, 0x77));
        glow.setColorAt(1.0, QColor(0xb4, 0x8c, 0x5f));
    }
    QBrush brush(glow);
    QTransform to_ellipse;
    to_ellipse.translate(centre.x(), centre.y());
    to_ellipse.scale(rx, ry);
    brush.setTransform(to_ellipse);
    p.fillRect(QRectF(0, 0, w, h), brush);

    // Grain: a dark and a light hairline every 41 px, tilted 2 degrees.
    const qreal grain = dark ? 0.25 : 0.2;
    const QColor line_dark(0, 0, 0, static_cast<int>(255 * 0.35 * grain));
    const QColor line_light(255, 220, 180, static_cast<int>(255 * 0.12 * grain));
    p.translate(w / 2, h / 2);
    p.rotate(2.0);
    const qreal reach = std::hypot(w, h) / 2 + 42.0;
    for (qreal x = -reach; x < reach; x += 41.0) {
        p.fillRect(QRectF(x + 18.0, -reach, 1.0, 2 * reach), line_dark);
        p.fillRect(QRectF(x + 41.0, -reach, 1.0, 2 * reach), line_light);
    }
    p.end();

    wood_ = pixmap;
    wood_key_ = key;
    return wood_;
}

void LibraryStyleContext::PaintCover(QPainter* painter, const QRect& rect,
                                     const QModelIndex& index, const QPalette& palette,
                                     int radius) {
    if (rect.isEmpty()) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addRoundedRect(rect, radius, radius);
    painter->setClipPath(clip);
    painter->drawPixmap(rect, Cover(index, rect.size() * painter->device()->devicePixelRatioF(),
                                    palette));
    painter->restore();
}

void LibraryStyleContext::PaintSelection(QPainter* painter, const QRect& rect,
                                         const QPalette& palette, int radius, int width) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(Accent(palette), width));
    const qreal grow = width / 2.0 + 1.0;
    painter->drawRoundedRect(QRectF(rect).adjusted(-grow, -grow, grow, grow), radius + grow,
                             radius + grow);
    painter->restore();
}

void LibraryStyleContext::PaintName(QPainter* painter, const QRect& rect, const QString& text,
                                    const QColor& color, const QFont& font,
                                    Qt::Alignment alignment, int max_lines) {
    if (rect.isEmpty() || text.isEmpty()) {
        return;
    }
    painter->save();
    painter->setFont(font);
    painter->setPen(color);
    const QFontMetrics metrics(font);
    const int lines = std::max(1, std::min(max_lines, rect.height() / metrics.lineSpacing()));

    QTextLayout layout(text, font);
    layout.beginLayout();
    QStringList out;
    for (int i = 0; i < lines; ++i) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(rect.width());
        if (i == lines - 1) {
            out.append(metrics.elidedText(text.mid(line.textStart()), Qt::ElideRight,
                                          rect.width()));
        } else {
            out.append(text.mid(line.textStart(), line.textLength()).trimmed());
        }
    }
    layout.endLayout();

    const QString joined = out.join(QLatin1Char('\n'));
    painter->drawText(rect, alignment.toInt(), joined);
    painter->restore();
}

void LibraryStyleContext::PaintInfoPanel(QPainter* painter, const QRect& rect,
                                         const QModelIndex& index, const QPalette& palette,
                                         const QFont& font) {
    if (rect.isEmpty() || !index.isValid()) {
        return;
    }
    const GameInfo info = Info(index);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QColor border = Accent(palette);
    border.setAlpha(170);
    painter->setPen(QPen(border, 1.5));
    painter->setBrush(Surface(palette));
    painter->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

    const QRect inner = rect.adjusted(12, 8, -12, -8);
    QFont title_font = font;
    title_font.setBold(true);
    const int info_height = info.info_line.isEmpty() ? 0 : QFontMetrics(font).height() + 4;
    PaintName(painter, inner.adjusted(0, 0, 0, -info_height), info.title, palette.text().color(),
              title_font, Qt::AlignHCenter | Qt::AlignVCenter);
    if (info_height > 0) {
        PaintName(painter, QRect(inner.left(), inner.bottom() - info_height, inner.width(),
                                 info_height),
                  info.info_line, MutedText(palette), font, Qt::AlignHCenter | Qt::AlignVCenter,
                  1);
    }
    painter->restore();
}

void LibraryStyleContext::PaintCoverCaption(QPainter* painter, const QRect& cover_rect,
                                            const QModelIndex& index, const QFont& font,
                                            bool show_info) {
    if (cover_rect.isEmpty() || !index.isValid()) {
        return;
    }
    const GameInfo info = Info(index);
    const QFontMetrics metrics(font);
    const int lines = show_info && !info.info_line.isEmpty() ? 3 : 2;
    const int caption_height = std::min(cover_rect.height(), metrics.lineSpacing() * lines + 16);
    const QRect caption(cover_rect.left(), cover_rect.bottom() - caption_height + 1,
                        cover_rect.width(), caption_height);

    painter->save();
    QLinearGradient shade(caption.topLeft(), caption.bottomLeft());
    shade.setColorAt(0.0, QColor(0, 0, 0, 0));
    shade.setColorAt(0.45, QColor(0, 0, 0, 150));
    shade.setColorAt(1.0, QColor(0, 0, 0, 200));
    painter->fillRect(caption, shade);

    const QRect text = caption.adjusted(8, 6, -8, -6);
    QFont title_font = font;
    title_font.setBold(true);
    const int info_height = lines == 3 ? metrics.lineSpacing() : 0;
    PaintName(painter, text.adjusted(0, 0, 0, -info_height), info.title, Qt::white, title_font,
              Qt::AlignHCenter | Qt::AlignBottom);
    if (info_height > 0) {
        PaintName(painter,
                  QRect(text.left(), text.bottom() - info_height + 1, text.width(), info_height),
                  info.info_line, QColor(255, 255, 255, 210), font,
                  Qt::AlignHCenter | Qt::AlignVCenter, 1);
    }
    painter->restore();
}

namespace {

class PlaceholderView final : public LibraryStyleView {
public:
    PlaceholderView(QWidget* parent, const QString& style_name) : LibraryStyleView{parent} {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 8, 12, 8);
        auto* label = new QLabel(style_name, this);
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        list = new QListView(this);
        list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        layout->addWidget(label);
        layout->addWidget(list, 1);
        setFocusProxy(list);

        connect(list, &QListView::doubleClicked, this, &LibraryStyleView::LaunchRequested);
        connect(list, &QListView::customContextMenuRequested, this, [this](const QPoint& pos) {
            emit ContextMenuRequested(list->indexAt(pos), list->viewport()->mapToGlobal(pos));
        });
    }

    void SetModel(QAbstractItemModel* model) override {
        list->setModel(model);
        connect(list->selectionModel(), &QItemSelectionModel::currentChanged, this,
                [this](const QModelIndex& current) { emit CurrentChanged(current); });
    }
    QModelIndex CurrentIndex() const override {
        return list->currentIndex();
    }
    void SetCurrentIndex(const QModelIndex& index) override {
        list->setCurrentIndex(index);
        list->scrollTo(index);
    }
    void SetShowNames(bool) override {}
    void SetShowInfo(bool) override {}

private:
    QListView* list = nullptr;
};

} // namespace

LibraryStyleView* MakePlaceholderView(QWidget* parent, const QString& style_name) {
    return new PlaceholderView(parent, style_name);
}

} // namespace LibraryStyles
