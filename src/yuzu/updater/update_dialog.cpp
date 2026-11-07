// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QRadioButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <qdesktopservices.h>
#include "common/logging.h"
#include "qt_common/abstract/frontend.h"
#include "qt_common/abstract/progress.h"
#include "ui_update_dialog.h"
#include "update_dialog.h"

#include "common/httplib.h"

#ifdef YUZU_BUNDLED_OPENSSL
#include <openssl/cert.h>
#endif

#include <QDesktopServices>

#ifdef _WIN32
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringConverter>
#include <QTextStream>
#endif

#undef GetSaveFileName

UpdateDialog::UpdateDialog(const Common::Net::Release& release, QWidget* parent)
    : QDialog(parent), ui(new Ui::UpdateDialog) {
    ui->setupUi(this);

    ui->version->setText(
        tr("%1 is available for download.").arg(QString::fromStdString(release.title)));
    ui->url->setText(
        tr("<a href=\"%1\">View on GitHub</a>").arg(QString::fromStdString(release.html_url)));

    std::string text{release.body};
    if (auto pos = text.find("# Packages"); pos != std::string::npos) {
        text = text.substr(0, pos);
    }

    ui->body->setMarkdown(QString::fromStdString(text));

    // TODO(crueter): Find a way to set default
    const auto assets = release.GetPlatformAssets();

    if (assets.empty()) {
        ui->groupBox->setHidden(true);
        connect(this, &QDialog::accepted, this, [release]() {
            QDesktopServices::openUrl(QUrl{QString::fromStdString(release.html_url)});
        });
    } else if (assets.size() == 1) {
        ui->groupBox->setHidden(true);
        m_asset = assets[0];

        connect(this, &QDialog::accepted, this, &UpdateDialog::Download);
    } else {
        u32 i = 0;
        for (const Common::Net::Asset& a : assets) {
            QRadioButton* r = new QRadioButton(tr(a.name.c_str()), this);
            connect(r, &QRadioButton::toggled, this, [a, this](bool checked) {
                if (checked)
                    m_asset = a;
            });

            if (i == 0)
                r->setChecked(true);
            ++i;

            ui->radioButtons->addWidget(r);
        }

        connect(this, &QDialog::accepted, this, &UpdateDialog::Download);
    }
}

UpdateDialog::~UpdateDialog() {
    delete ui;
}

// TODO: migrate to a net.cpp wrapper
void UpdateDialog::Download() {
#ifdef _WIN32
    // The package is installed in place: download it to a scratch folder, no questions asked.
    const QString update_dir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("lichii-update"));
    QDir(update_dir).removeRecursively();
    QDir().mkpath(update_dir);
    const QString filename =
        QDir(update_dir).filePath(QString::fromStdString(m_asset.filename));
#else
    const auto filename = QtCommon::Frontend::GetSaveFileName(
        tr("New Version Location"),
        qApp->applicationDirPath() % QStringLiteral("/") % QString::fromStdString(m_asset.filename),
        tr("All Files (*.*)"));
#endif

    if (filename.isEmpty())
        return;

    QSaveFile file(filename);
    if (!file.open(QIODevice::Truncate | QIODevice::WriteOnly)) {
        LOG_WARNING(Frontend, "Could not open file {}", filename.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to save file"),
                                     tr("Could not open file %1 for writing.").arg(filename));
        return;
    }

    // TODO(crueter): Move to net.cpp
    constexpr std::size_t timeout_seconds = 15;

    std::unique_ptr<httplib::Client> client = std::make_unique<httplib::Client>(m_asset.url);
    client->set_connection_timeout(timeout_seconds);
    client->set_read_timeout(timeout_seconds);
    client->set_write_timeout(timeout_seconds);
    client->set_follow_location(true);

#ifdef YUZU_BUNDLED_OPENSSL
    client->load_ca_cert_store(kCert, sizeof(kCert));
#endif

    if (client == nullptr) {
        LOG_ERROR(Frontend, "Invalid URL {}{}", m_asset.url, m_asset.path);
        return;
    }

    auto progress =
        QtCommon::Frontend::newProgressDialog(tr("Downloading..."), tr("Cancel"), 0, 100);
    progress->show();

    QGuiApplication::processEvents();

    // Progress dialog.
    auto progress_callback = [&](size_t processed_size, size_t total_size) {
        QGuiApplication::processEvents();
        if (total_size != 0) {
            progress->setValue(static_cast<int>((processed_size * 100) / total_size));
        }
        return !progress->wasCanceled();
    };

    // Write file in chunks.
    auto content_receiver = [&file, filename](const char* t_data, size_t data_length) -> bool {
        if (file.write(t_data, data_length) == -1) {
            LOG_WARNING(Frontend, "Could not write {} bytes to file {}", data_length,
                        filename.toStdString());
            QtCommon::Frontend::Critical(tr("Failed to save file"),
                                         tr("Could not write to file %1.").arg(filename));
            return false;
        }

        return true;
    };

    // Now send off request
    auto result = client->Get(m_asset.path, content_receiver, progress_callback);
    progress->close();

    if (!result) {
        LOG_ERROR(Frontend, "GET to {}{} returned null", m_asset.url, m_asset.path);
        return;
    }

    const auto& response = result.value();
    if (response.status >= 400) {
        LOG_ERROR(Frontend, "GET to {}{} returned error status code: {}", m_asset.url, m_asset.path,
                  response.status);
        QtCommon::Frontend::Critical(tr("Failed to download file"),
                                     tr("Could not download from %1%2\nError code: %3")
                                         .arg(QString::fromStdString(m_asset.url),
                                              QString::fromStdString(m_asset.path),
                                              QString::number(response.status)));
        return;
    }
    if (!response.has_header("content-type")) {
        LOG_ERROR(Frontend, "GET to {}{} returned no content", m_asset.url, m_asset.path);
        return;
    }

    if (!file.commit()) {
        LOG_WARNING(Frontend, "Could not commit to file {}", filename.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to save file"),
                                     tr("Could not commit to file %1.").arg(filename));
        return;
    }

#ifdef _WIN32
    const auto install = QtCommon::Frontend::Question(
        tr("Download Complete"),
        tr("The update was downloaded. Install it now?\n\nLichii will close, replace its files "
           "(your user folder, saves and settings are kept) and open again."),
        QtCommon::Frontend::Yes | QtCommon::Frontend::No);
    if (install == QtCommon::Frontend::Yes && InstallAndRestart(filename)) {
        return;
    }
#endif

    // Download is complete. User may choose to open in the file manager.
    auto button =
        QtCommon::Frontend::Question(tr("Download Complete"),
                                     tr("Successfully downloaded %1. Would you like to open it?")
                                         .arg(QString::fromStdString(m_asset.filename)),
                                     QtCommon::Frontend::Yes | QtCommon::Frontend::No);

    if (button == QtCommon::Frontend::Yes) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(filename));
    }
}

#ifdef _WIN32
bool UpdateDialog::InstallAndRestart(const QString& package_path) {
    const QString app_dir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const QString app_exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QDir update_dir = QFileInfo(package_path).absoluteDir();
    const QString staging = QDir::toNativeSeparators(update_dir.filePath(QStringLiteral("staging")));
    QDir(staging).removeRecursively();
    QDir().mkpath(staging);

    const auto fail = [](const QString& reason) {
        LOG_ERROR(Frontend, "Update install failed: {}", reason.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to install update"), reason);
        return false;
    };

    // Windows 10 1803 and later ship bsdtar, which extracts zip archives.
    const QString tar = QDir::toNativeSeparators(
        QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:\Windows")))
            .filePath(QStringLiteral("System32/tar.exe")));
    if (!QFileInfo::exists(tar)) {
        return fail(tr("%1 was not found, so the package can't be extracted.").arg(tar));
    }
    auto progress = QtCommon::Frontend::newProgressDialog(tr("Extracting update..."), QString(), 0, 0);
    progress->show();
    QProcess extract;
    extract.start(tar, {QStringLiteral("-xf"), QDir::toNativeSeparators(package_path),
                        QStringLiteral("-C"), staging});
    if (!extract.waitForStarted()) {
        progress->close();
        return fail(tr("Could not run %1.").arg(tar));
    }
    while (!extract.waitForFinished(100)) {
        QGuiApplication::processEvents();
    }
    progress->close();
    const QString new_exe = QDir(staging).filePath(QFileInfo(app_exe).fileName());
    if (extract.exitStatus() != QProcess::NormalExit || extract.exitCode() != 0 ||
        !QFileInfo::exists(new_exe)) {
        return fail(tr("The downloaded package could not be extracted:\n%1")
                        .arg(QString::fromLocal8Bit(extract.readAllStandardError())));
    }

    // A running executable and its DLLs can't be overwritten, so a helper waits for this process
    // to exit, copies the new files over the install (robocopy retries locked files) and starts
    // the emulator again. It inherits this process's environment, launcher variables included.
    const auto quote = [](QString path) {
        return QStringLiteral("'") + path.replace(QStringLiteral("'"), QStringLiteral("''")) +
               QStringLiteral("'");
    };
    const QString script_path = update_dir.filePath(QStringLiteral("install_update.ps1"));
    const QString log_path = QDir::toNativeSeparators(update_dir.filePath(QStringLiteral("install_update.log")));
    QFile script(script_path);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return fail(tr("Could not write %1.").arg(script_path));
    }
    QTextStream out(&script);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true); // Windows PowerShell reads BOM-less scripts as ANSI
    out << "$staging = " << quote(staging) << "\n"
        << "$app = " << quote(app_dir) << "\n"
        << "$exe = " << quote(app_exe) << "\n"
        << "$log = " << quote(log_path) << "\n"
        << "Wait-Process -Id " << QCoreApplication::applicationPid()
        << " -ErrorAction SilentlyContinue\n"
        << "robocopy $staging $app /E /R:30 /W:1 /NP /NJH /NJS > $log\n"
        << "$code = $LASTEXITCODE\n"
        << "if ($code -lt 8) {\n"
        << "    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue\n"
        << "    Remove-Item -LiteralPath " << quote(QDir::toNativeSeparators(package_path))
        << " -Force -ErrorAction SilentlyContinue\n"
        << "    Start-Process -FilePath $exe -WorkingDirectory $app\n"
        << "} else {\n"
        << "    Add-Content -Path $log -Value \"robocopy failed with exit code $code\"\n"
        << "    Start-Process -FilePath $exe -WorkingDirectory $app\n"
        << "    Start-Process -FilePath notepad.exe -ArgumentList $log\n"
        << "}\n";
    out.flush();
    script.close();

    if (!QProcess::startDetached(QStringLiteral("powershell.exe"),
                                 {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                                  QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"),
                                  QStringLiteral("Hidden"), QStringLiteral("-File"),
                                  QDir::toNativeSeparators(script_path)})) {
        return fail(tr("Could not start the update helper."));
    }
    LOG_INFO(Frontend, "Update extracted to {}, installing after exit", staging.toStdString());

    // Close through the main window so settings are saved; the helper installs once we exit.
    if (QWidget* const main = parentWidget() ? parentWidget()->window() : nullptr) {
        QMetaObject::invokeMethod(main, "close", Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
    }
    return true;
}
#endif
