// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHideEvent>
#include <QIODevice>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QStyleOption>
#include <QTime>
#include "common/container/unordered_map.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/loader/loader.h"
#include "ui_loading_screen.h"
#include "video_core/rasterizer_interface.h"
#include "yuzu/loading_screen.h"

// Mingw seems to not have QMovie at all. If QMovie is missing then use a single frame instead of an
// showing the full animation
#if !YUZU_QT_MOVIE_MISSING
#include <QMovie>
#endif

constexpr char PROGRESSBAR_STYLE_PREPARE[] = R"(
QProgressBar {
  background-color: rgba(255, 255, 255, 0.08);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 6px;
  min-height: 14px;
  max-height: 14px;
}
QProgressBar::chunk {
  background-color: #a6a8ab;
  border-radius: 5px;
})";

constexpr char PROGRESSBAR_STYLE_BUILD[] = R"(
QProgressBar {
  background-color: rgba(255, 255, 255, 0.08);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 6px;
  min-height: 14px;
  max-height: 14px;
}
QProgressBar::chunk {
  background-color: #a6a8ab;
  border-radius: 5px;
})";

constexpr char PROGRESSBAR_STYLE_COMPLETE[] = R"(
QProgressBar {
  background-color: rgba(88, 160, 112, 0.12);
  border: 1px solid rgba(88, 160, 112, 0.28);
  border-radius: 6px;
  min-height: 14px;
  max-height: 14px;
}
QProgressBar::chunk {
  background-color: #58a070;
  border-radius: 5px;
})";

namespace {

QColor SampleLogoColor(const QImage& image, int begin, int end) {
    if (image.isNull()) {
        return QColor(64, 68, 76);
    }

    const QImage sample = image.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_ARGB32);
    qreal red = 0;
    qreal green = 0;
    qreal blue = 0;
    qreal weight_total = 0;
    const int start_x = sample.width() * begin / 100;
    const int end_x = sample.width() * end / 100;

    for (int y = 0; y < sample.height(); ++y) {
        for (int x = start_x; x < end_x; ++x) {
            const QColor color = sample.pixelColor(x, y);
            if (color.alpha() < 64 || color.saturationF() < 0.18 || color.lightnessF() < 0.08 ||
                color.lightnessF() > 0.9) {
                continue;
            }
            const qreal weight = color.saturationF();
            red += color.redF() * weight;
            green += color.greenF() * weight;
            blue += color.blueF() * weight;
            weight_total += weight;
        }
    }

    if (weight_total == 0) {
        return QColor(64, 68, 76);
    }
    return QColor::fromRgbF(red / weight_total, green / weight_total, blue / weight_total);
}

QColor LiftLogoAccent(QColor color) {
    return color.lightness() < 112 ? color.lighter(165) : color;
}

void SetLogoBackground(QWidget* widget, const QColor& left, const QColor& right) {
    widget->setStyleSheet(
        QStringLiteral(
            "QWidget#LoadingScreen { background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, "
            "stop:0 #08090b, stop:0.34 rgba(%1, %2, %3, 86), "
            "stop:0.68 rgba(%4, %5, %6, 68), stop:1 #08090b); } "
            "QWidget#fade_parent { background-color: transparent; }")
            .arg(left.red())
            .arg(left.green())
            .arg(left.blue())
            .arg(right.red())
            .arg(right.green())
            .arg(right.blue()));
}

void SetLogoBackground(QWidget* widget, const QPixmap& logo) {
    const QImage image = logo.toImage();
    SetLogoBackground(widget, LiftLogoAccent(SampleLogoColor(image, 0, 50)),
                      LiftLogoAccent(SampleLogoColor(image, 50, 100)));
}

} // namespace

LoadingScreen::LoadingScreen(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::LoadingScreen>()),
      previous_stage(VideoCore::LoadCallbackStage::Complete) {
    ui->setupUi(this);
    setMinimumSize(Layout::MinimumSize::Width, Layout::MinimumSize::Height);
    ui->logo->setMaximumSize(360, 100);
    ui->value->setMinimumHeight(20);

    logo_glow = new QGraphicsDropShadowEffect(ui->logo);
    logo_glow->setBlurRadius(18);
    logo_glow->setColor(QColor(166, 168, 171, 96));
    logo_glow->setOffset(0, 0);
    ui->logo->setGraphicsEffect(logo_glow);
    logo_float_animation = std::make_unique<QPropertyAnimation>(ui->logo, "pos");
    logo_float_animation->setDuration(4200);
    logo_float_animation->setLoopCount(-1);
    logo_float_animation->setEasingCurve(QEasingCurve::InOutSine);

    // Fade the content layer to reveal the logo-tinted background behind it.
    opacity_effect = new QGraphicsOpacityEffect(this);
    opacity_effect->setOpacity(1);
    ui->fade_parent->setGraphicsEffect(opacity_effect);
    fadeout_animation = std::make_unique<QPropertyAnimation>(opacity_effect, "opacity");
    fadeout_animation->setDuration(500);
    fadeout_animation->setStartValue(1);
    fadeout_animation->setEndValue(0);
    fadeout_animation->setEasingCurve(QEasingCurve::OutBack);

    // After the fade completes, hide the widget and reset the opacity
    connect(fadeout_animation.get(), &QPropertyAnimation::finished, [this] {
        hide();
        opacity_effect->setOpacity(1);
        emit Hidden();
    });
    connect(this, &LoadingScreen::LoadProgress, this, &LoadingScreen::OnLoadProgress,
            Qt::QueuedConnection);
    qRegisterMetaType<VideoCore::LoadCallbackStage>();

    stage_translations = {
        {VideoCore::LoadCallbackStage::Prepare, tr("Preparing")},
        {VideoCore::LoadCallbackStage::Build, tr("Loading Shaders")},
        {VideoCore::LoadCallbackStage::Complete, tr("Starting Game")},
    };
    progressbar_style = {
        {VideoCore::LoadCallbackStage::Prepare, PROGRESSBAR_STYLE_PREPARE},
        {VideoCore::LoadCallbackStage::Build, PROGRESSBAR_STYLE_BUILD},
        {VideoCore::LoadCallbackStage::Complete, PROGRESSBAR_STYLE_COMPLETE},
    };
}

LoadingScreen::~LoadingScreen() = default;

void LoadingScreen::Prepare(Loader::AppLoader& loader) {
    logo_float_animation->stop();
    logo_rest_position_valid = false;
    closing_screen = false;
    ui->banner->clear();
    ui->logo->clear();
    ui->logo->setWordWrap(false);
    ui->logo->setStyleSheet(QStringLiteral("QLabel#logo { background-color: transparent; }"));
    ui->stage->clear();
    ui->value->clear();
    ui->logo->setVisible(false);
    SetLogoBackground(this, QPixmap{});
    Clear();
    previous_total = 0;
    previous_stage = VideoCore::LoadCallbackStage::Complete;

    QColor logo_accent{166, 168, 171};
    QPixmap banner_fallback;
    bool logo_loaded = false;
    std::vector<u8> buffer;
    if (loader.ReadBanner(buffer) == Loader::ResultStatus::Success) {
        const QImage banner_image =
            QImage::fromData(buffer.data(), static_cast<int>(buffer.size()));
        if (!banner_image.isNull()) {
            banner_fallback = QPixmap::fromImage(banner_image);
            logo_accent = LiftLogoAccent(SampleLogoColor(banner_image, 0, 100));
        }
#ifdef YUZU_QT_MOVIE_MISSING
        QPixmap map;
        map.loadFromData(buffer.data(), buffer.size());
        ui->banner->setPixmap(map.scaled(360, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation));
#else
        backing_mem = std::make_unique<QByteArray>(reinterpret_cast<char*>(buffer.data()),
                                                   static_cast<int>(buffer.size()));
        backing_buf = std::make_unique<QBuffer>(backing_mem.get());
        backing_buf->open(QIODevice::ReadOnly);
        animation = std::make_unique<QMovie>(backing_buf.get(), QByteArray());
        animation->start();
        ui->banner->setMovie(animation.get());
#endif
        buffer.clear();
    }
    if (loader.ReadLogo(buffer) == Loader::ResultStatus::Success) {
        QPixmap map;
        map.loadFromData(buffer.data(), static_cast<uint>(buffer.size()));
        if (!map.isNull()) {
            ui->logo->setPixmap(map.scaled(360, 100, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
            ui->logo->setVisible(true);
            logo_accent = LiftLogoAccent(SampleLogoColor(map.toImage(), 0, 100));
            QColor glow_color = logo_accent;
            glow_color.setAlpha(112);
            logo_glow->setColor(glow_color);
            SetLogoBackground(this, map);
            logo_loaded = true;
        }
    }

    std::string title;
    if (loader.ReadTitle(title) != Loader::ResultStatus::Success) {
        title.clear();
    }
    game_title = QString::fromStdString(title);

    if (!logo_loaded) {
        if (!banner_fallback.isNull()) {
            ui->logo->setPixmap(banner_fallback.scaled(360, 100, Qt::KeepAspectRatio,
                                                       Qt::SmoothTransformation));
        } else {
            const QString wordmark = game_title.isEmpty() ? tr("Lichii") : game_title;
            ui->logo->setText(wordmark);
            ui->logo->setWordWrap(true);
            ui->logo->setStyleSheet(
                QStringLiteral("QLabel#logo { background-color: transparent; color: %1; "
                               "font-size: 25px; font-weight: 600; letter-spacing: 1px; }")
                    .arg(logo_accent.name()));
        }
        ui->logo->setVisible(true);
        QColor glow_color = logo_accent;
        glow_color.setAlpha(112);
        logo_glow->setColor(glow_color);
        SetLogoBackground(this, logo_accent, logo_accent);
    }

    slow_shader_compile_start = false;
    OnLoadProgress(VideoCore::LoadCallbackStage::Prepare, 0, 0);
}

void LoadingScreen::PrepareClosing(Loader::AppLoader& loader) {
    fadeout_animation->stop();
    opacity_effect->setOpacity(1);
    Prepare(loader);
    closing_screen = true;
    ui->stage->setText(game_title.isEmpty() ? tr("Closing software…")
                                            : tr("Closing %1…").arg(game_title));
    ui->value->clear();
    ui->value->hide();
    ui->progress_bar->setRange(0, 0);
}

void LoadingScreen::OnLoadComplete() {
    if (closing_screen) {
        return;
    }
    fadeout_animation->start(QPropertyAnimation::KeepWhenStopped);
}

void LoadingScreen::OnLoadProgress(VideoCore::LoadCallbackStage stage, std::size_t value,
                                   std::size_t total) {
    if (closing_screen) {
        return;
    }
    using namespace std::chrono;
    const auto now = steady_clock::now();
    if (stage != previous_stage) {
        ui->progress_bar->setStyleSheet(QString::fromUtf8(progressbar_style[stage]));
        if (stage == VideoCore::LoadCallbackStage::Prepare) {
            ui->progress_bar->setRange(0, 0);
            ui->progress_bar->show();
        } else if (stage == VideoCore::LoadCallbackStage::Build) {
            ui->progress_bar->setRange(0, std::max<int>(1, static_cast<int>(total)));
            ui->progress_bar->show();
        } else {
            ui->progress_bar->setRange(0, 0);
            ui->progress_bar->show();
        }
        previous_stage = stage;
        slow_shader_compile_start = false;
    }
    if (stage == VideoCore::LoadCallbackStage::Build && total != previous_total) {
        ui->progress_bar->setMaximum(static_cast<int>(total));
        previous_total = total;
    }

    QString estimate;
    if (stage == VideoCore::LoadCallbackStage::Build) {
        if (now - previous_time > milliseconds{50} || slow_shader_compile_start) {
            if (!slow_shader_compile_start) {
                slow_shader_start = now;
                slow_shader_compile_start = true;
                slow_shader_first_value = value;
            }
            const auto diff = duration_cast<milliseconds>(now - slow_shader_start);
            if (diff > seconds{1} && value > slow_shader_first_value && total > value) {
                const auto eta_mseconds =
                    static_cast<long>(static_cast<double>(total - slow_shader_first_value) /
                                      (value - slow_shader_first_value) * diff.count());
                estimate = tr("%1 remaining")
                               .arg(QTime(0, 0, 0, 0)
                                        .addMSecs(std::max<long>(eta_mseconds - diff.count() + 1000,
                                                                 1000))
                                        .toString(QStringLiteral("mm:ss")));
            }
        }
    } else if (stage == VideoCore::LoadCallbackStage::Prepare) {
        estimate = tr("Setting up emulation session");
    } else if (stage == VideoCore::LoadCallbackStage::Complete) {
        estimate = tr("Finalizing launch");
    }

    QString stage_text = stage_translations[stage];
    if (stage == VideoCore::LoadCallbackStage::Build) {
        stage_text = tr("Loading Shaders (%1 / %2)")
                         .arg(static_cast<int>(value))
                         .arg(static_cast<int>(total));
    } else if (stage == VideoCore::LoadCallbackStage::Complete && !game_title.isEmpty()) {
        stage_text = tr("Starting Game (%1)").arg(game_title);
    }
    ui->stage->setText(stage_text);
    ui->value->setText(estimate);
    ui->value->setVisible(!estimate.isEmpty());
    if (stage == VideoCore::LoadCallbackStage::Build) {
        ui->progress_bar->setValue(static_cast<int>(value));
    }
    previous_time = now;
}

void LoadingScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (ui->logo->isHidden()) {
        return;
    }

    logo_rest_position = ui->logo->pos();
    logo_rest_position_valid = true;
    logo_float_animation->setStartValue(logo_rest_position);
    logo_float_animation->setKeyValueAt(0.5, logo_rest_position + QPoint(0, -7));
    logo_float_animation->setEndValue(logo_rest_position);
    logo_float_animation->start();
}

void LoadingScreen::hideEvent(QHideEvent* event) {
    logo_float_animation->stop();
    if (logo_rest_position_valid) {
        ui->logo->move(logo_rest_position);
        logo_rest_position_valid = false;
    }
    QWidget::hideEvent(event);
}

void LoadingScreen::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    QWidget::paintEvent(event);
}

void LoadingScreen::Clear() {
    closing_screen = false;
#ifndef YUZU_QT_MOVIE_MISSING
    animation.reset();
    backing_buf.reset();
    backing_mem.reset();
#endif
}
