// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qt_common/util/open.h"

#include <QDesktopServices>
#include <QDir>

#ifdef __unix__
#include <QProcess>
#include <QProcessEnvironment>
#endif

namespace QtCommon {

#ifdef __unix__

namespace {

/// Builds the environment for spawned external handlers (browsers, file
/// managers). Starts from the current process environment and drops
/// variables that must not leak into programs outside the emulator.
QProcessEnvironment CleanedEnvironmentForExternalProcess() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // External handlers are not part of the emulator, so never hand them
    // an overridden config home (e.g. per-source homes set by launchers).
    // A file manager started with a foreign config home reads the wrong
    // dolphinrc without the user's kdeglobals and falls back to a broken
    // default theme. Session default (~/.config) applies when unset.
    const QString config_home = env.value(QStringLiteral("XDG_CONFIG_HOME"));
    if (!config_home.isEmpty() &&
        config_home != QDir::homePath() + QStringLiteral("/.config")) {
        env.remove(QStringLiteral("XDG_CONFIG_HOME"));
    }

    return env;
}

/// Opens a local path or URL string with the host default handler via
/// xdg-open. Returns true when the handler process was started.
bool OpenWithXdgOpen(const QString& target) {
    QProcess proc;
    proc.setProgram(QStringLiteral("xdg-open"));
    proc.setArguments({target});
    proc.setProcessEnvironment(CleanedEnvironmentForExternalProcess());
    proc.setWorkingDirectory(QDir::homePath());
    qint64 pid = 0;
    if (proc.startDetached(&pid)) {
        return true;
    }
    // Fallback if xdg-open cannot be started.
    return QDesktopServices::openUrl(QUrl::fromUserInput(target));
}

} // namespace

bool OpenUrl(const QUrl& url) {
    if (url.isLocalFile()) {
        return OpenWithXdgOpen(url.toLocalFile());
    }
    return OpenWithXdgOpen(url.toString(QUrl::FullyEncoded));
}

bool OpenLocalFolder(const QString& path) {
    return OpenWithXdgOpen(path);
}

bool OpenLocalFolder(const std::filesystem::path& path) {
    return OpenWithXdgOpen(
        QString::fromStdString(std::filesystem::path(path).string()));
}

#else

bool OpenUrl(const QUrl& url) {
    return QDesktopServices::openUrl(url);
}

bool OpenLocalFolder(const QString& path) {
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool OpenLocalFolder(const std::filesystem::path& path) {
    return QDesktopServices::openUrl(
        QUrl::fromLocalFile(QString::fromStdString(std::filesystem::path(path).string())));
}

#endif

} // namespace QtCommon
