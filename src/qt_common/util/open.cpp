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

/// Unescapes one systemd shell-escaped environment value, i.e. the $'...'
/// ANSI-C quoting used by `systemctl show-environment`. Plain values pass
/// through unchanged so the comparison below also works where no shell is
/// involved.
QString UnescapeSystemdValue(const QString& value) {
    static const QString prefix = QStringLiteral("$'");
    if (value.size() < 4 || !value.startsWith(prefix) ||
        !value.endsWith(QLatin1Char('\''))) {
        return value;
    }
    const QString inner = value.mid(2, value.size() - 3);
    auto hex_value = [](QChar c) -> int {
        const uint u = c.unicode();
        if (u >= '0' && u <= '9') {
            return int(u - '0');
        }
        if (u >= 'a' && u <= 'f') {
            return int(u - 'a' + 10);
        }
        if (u >= 'A' && u <= 'F') {
            return int(u - 'A' + 10);
        }
        return -1;
    };
    QString out;
    out.reserve(inner.size());
    for (qsizetype i = 0; i < inner.size();) {
        const QChar c = inner[i];
        if (c != QLatin1Char('\\') || i + 1 >= inner.size()) {
            out.append(c);
            ++i;
            continue;
        }
        const QChar e = inner[i + 1];
        const auto take = [&](QChar ch) {
            out.append(ch);
            i += 2;
        };
        switch (e.unicode()) {
        case 'a':
            take(QLatin1Char('\a'));
            break;
        case 'b':
            take(QLatin1Char('\b'));
            break;
        case 'f':
            take(QLatin1Char('\f'));
            break;
        case 'n':
            take(QLatin1Char('\n'));
            break;
        case 'r':
            take(QLatin1Char('\r'));
            break;
        case 't':
            take(QLatin1Char('\t'));
            break;
        case 'v':
            take(QLatin1Char('\v'));
            break;
        case 's':
            take(QLatin1Char(' '));
            break;
        case '\\':
            take(QLatin1Char('\\'));
            break;
        case '\'':
            take(QLatin1Char('\''));
            break;
        case '"':
            take(QLatin1Char('"'));
            break;
        case 'x': {
            const int hi = (i + 2 < inner.size()) ? hex_value(inner[i + 2]) : -1;
            const int lo = (i + 3 < inner.size()) ? hex_value(inner[i + 3]) : -1;
            if (hi >= 0 && lo >= 0) {
                out.append(QChar(char16_t(hi * 16 + lo)));
                i += 4;
            } else {
                out.append(c);
                ++i;
            }
            break;
        }
        case 'u':
        case 'U': {
            const int digits = (e.unicode() == 'u') ? 4 : 8;
            uint v = 0;
            bool ok = (i + 2 + digits <= inner.size());
            for (int k = 0; ok && k < digits; ++k) {
                const int h = hex_value(inner[i + 2 + k]);
                if (h < 0) {
                    ok = false;
                } else {
                    v = v * 16 + uint(h);
                }
            }
            if (ok) {
                out.append(QString::fromUcs4(reinterpret_cast<const char32_t*>(&v), 1));
                i += 2 + digits;
            } else {
                out.append(c);
                ++i;
            }
            break;
        }
        default: {
            // Octal \ooo (up to 3 digits); anything else passes through.
            uint v = 0;
            int k = 0;
            while (k < 3 && i + 1 + k < inner.size()) {
                const uint d = inner[i + 1 + k].unicode();
                if (d < '0' || d > '7') {
                    break;
                }
                v = v * 8 + (d - '0');
                ++k;
            }
            if (k > 0) {
                out.append(QChar(char16_t(v)));
                i += 1 + k;
            } else {
                out.append(c);
                ++i;
            }
            break;
        }
        }
    }
    return out;
}

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
                return UnescapeSystemdValue(line.mid(prefix.size()));
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
