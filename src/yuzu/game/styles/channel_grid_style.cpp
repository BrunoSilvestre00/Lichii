// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Placeholder until the Channel grid style lands: the style's own files replace this one.

#include <QCoreApplication>

#include "yuzu/game/styles/channel_grid_style.h"
#include "yuzu/game/styles/library_style_view.h"
#include "yuzu/game/styles/style_helpers.h"

namespace {

LibraryStyles::LibraryStyleView* Make(QWidget* parent) {
    return LibraryStyles::MakePlaceholderView(
        parent, QCoreApplication::translate("LibraryStyles", "Channel grid"));
}

[[maybe_unused]] const bool registered =
    LibraryStyles::Register({"channel_grid", QT_TRANSLATE_NOOP("LibraryStyles", "Channel grid"), &Make});

} // namespace
