// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qt_common/util/open.h"

#include <QDesktopServices>
#include <QDir>

#include <optional>

#ifdef __unix__
#include <QProcess>
#include <QProcessEnvironment>
#endif

namespace QtCommon {

#ifdef __unix__

namespace {

/// Queries the systemd user manager for its session-wide XDG_CONFIG_HOME.
/// Returns nullopt when the manager is unreachable (non-systemd session,
/// sandbox, query failure). The result is cached: the manager environment
/// does not change for the lifetime of this process, so the subprocess
/// cost is paid at most once (a fast local D-Bus call, ~20ms).
std::optional<QString> SessionConfigHome() {
    static const std::optional<QString> cached = []() -> std::optional<QString> {
        QProcess proc;
        proc.setProgram(QStringLiteral("systemctl"));
        proc.setArguments({QStringLiteral("--user"), QStringLiteral("show-environment")});
        proc.start();
        if (!proc.waitForFinished(2000) || proc.exitCode() != 0) {
            return std::nullopt;
        }
        static const QString prefix = QStringLiteral("XDG_CONFIG_HOME=");
        const QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
        for (const QString& line : output.split(QLatin1Char('\n'))) {
            if (line.startsWith(prefix)) {
                return line.mid(prefix.size());
            }
        }
        return QString{};
    }();
    return cached;
}

/// Builds the environment for spawned external handlers (browsers, file
/// managers). Starts from the current process environment and drops
/// variables that must not leak into programs outside the emulator.
QProcessEnvironment CleanedEnvironmentForExternalProcess() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // External handlers are not part of the emulator, so never hand them
    // a config home that was overridden for this process (e.g. per-source
    // homes set by launchers). A file manager started with a foreign
    // config home reads the wrong dolphinrc without the user's kdeglobals
    // and falls back to a broken default theme. A value matching the
    // session manager's is the user's own and is preserved; with no
    // manager to compare against, anything but the default (~/.config)
    // is treated as an override. Session default applies when unset.
    const QString config_home = env.value(QStringLiteral("XDG_CONFIG_HOME"));
    if (!config_home.isEmpty()) {
        const std::optional<QString> session_home = SessionConfigHome();
        if (!session_home.has_value()) {
            if (config_home != QDir::homePath() + QStringLiteral("/.config")) {
                env.remove(QStringLiteral("XDG_CONFIG_HOME"));
            }
        } else if (config_home != session_home.value()) {
            env.remove(QStringLiteral("XDG_CONFIG_HOME"));
        }
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
