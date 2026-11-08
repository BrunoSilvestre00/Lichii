// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QGraphicsOpacityEffect>
#include <QIODevice>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
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

void SetLogoBackground(QWidget* widget, const QPixmap& logo) {
    const QImage image = logo.toImage();
    const QColor left = SampleLogoColor(image, 0, 50);
    const QColor right = SampleLogoColor(image, 50, 100);
    widget->setStyleSheet(
        QStringLiteral(
            "QWidget#LoadingScreen { background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, "
            "stop:0 #08090b, stop:0.34 rgba(%1, %2, %3, 68), "
            "stop:0.68 rgba(%4, %5, %6, 54), stop:1 #08090b); } "
            "QWidget#fade_parent { background-color: transparent; }")
            .arg(left.red())
            .arg(left.green())
            .arg(left.blue())
            .arg(right.red())
            .arg(right.green())
            .arg(right.blue()));
}

} // namespace

LoadingScreen::LoadingScreen(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::LoadingScreen>()),
      previous_stage(VideoCore::LoadCallbackStage::Complete) {
    ui->setupUi(this);
    setMinimumSize(Layout::MinimumSize::Width, Layout::MinimumSize::Height);
    ui->verticalLayout->insertWidget(0, ui->logo, 0, Qt::AlignHCenter);
    ui->logo->setMaximumSize(360, 100);
    ui->value->setMinimumHeight(20);

    // Create a fade out effect to hide this loading screen widget.
    // When fading opacity, it will fade to the parent widgets background color, which is why we
    // create an internal widget named fade_widget that we use the effect on, while keeping the
    // loading screen widget's background color black. This way we can create a fade to black effect
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
    closing_screen = false;
    ui->banner->clear();
    ui->logo->clear();
    ui->stage->clear();
    ui->value->clear();
    ui->logo->setVisible(false);
    SetLogoBackground(this, QPixmap{});
    Clear();
    previous_total = 0;
    previous_stage = VideoCore::LoadCallbackStage::Complete;

    std::vector<u8> buffer;
    if (loader.ReadBanner(buffer) == Loader::ResultStatus::Success) {
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
            SetLogoBackground(this, map);
        }
    }

    std::string title;
    if (loader.ReadTitle(title) != Loader::ResultStatus::Success) {
        title.clear();
    }
    game_title = QString::fromStdString(title);

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
