// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFontMetrics>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLinearGradient>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTextLayout>
#include <QVBoxLayout>

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
        return Tr("Never played");
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

/// Default art for a game without an icon: a gradient picked from the title and its initials. No
/// console art, so it fits every style.
void PaintNeutralArt(QPainter* painter, const QRect& rect, const QString& title,
                     const QPalette& palette) {
    const int hue = static_cast<int>(qHash(title) % 360);
    const bool dark = LibraryStyleContext::IsDark(palette);
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
    parts.append(info.play_time_text);
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
        PaintNeutralArt(&painter, result.rect(), name.data(GameListItemPath::TitleRole).toString(),
                        palette);
        return result;
    }
    QSize scaled = art.size();
    scaled.scale(size, Qt::KeepAspectRatioByExpanding);
    const QRect target((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2,
                       scaled.width(), scaled.height());
    painter.drawPixmap(target, art);
    return result;
}

bool LibraryStyleContext::IsDark(const QPalette& palette) {
    return palette.window().color().lightness() < 128;
}

QColor LibraryStyleContext::Accent(const QPalette& palette) {
    return palette.highlight().color();
}

QColor LibraryStyleContext::Surface(const QPalette& palette) {
    return IsDark(palette) ? QColor(30, 31, 33) : QColor(255, 255, 255);
}

QColor LibraryStyleContext::MutedText(const QPalette& palette) {
    QColor color = palette.text().color();
    color.setAlpha(IsDark(palette) ? 200 : 150);
    return color;
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
