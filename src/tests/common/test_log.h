// SPDX-FileCopyrightText: Copyright 2026 Lichii Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <mutex>
#include <system_error>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "tests/common/restore_eden_path.h"

namespace Tests {

/// Starts the log for tests that read it through the in-memory ring (SetRingCapacity/ReadRing).
/// The first Log::Initialize of a process opens eden_log.txt in the Eden log folder and keeps it
/// open, and opening it moves an earlier eden_log.txt over eden_log.txt.old.txt: a test run with
/// a normal HOME rotated the user's own log away. The file goes to one folder under the
/// temporary folder instead, the same every run (an open file can't be removed on Windows, and a
/// new folder per run was left behind each time); nothing reads it, so two tests processes may
/// share it.
inline void InitializeTestLog() {
    static std::once_flag once;
    std::call_once(once, [] {
        const auto previous = Common::FS::GetEdenPath(Common::FS::EdenPath::LogDir);
        const auto directory = std::filesystem::temp_directory_path() / "lichii-tests-log";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        Common::FS::SetEdenPath(Common::FS::EdenPath::LogDir, directory);
        Common::Log::Initialize();
        RestoreEdenPath(Common::FS::EdenPath::LogDir, previous);
    });
}

} // namespace Tests
