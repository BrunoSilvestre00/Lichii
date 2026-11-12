// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Placeholder until the Dual pane style lands: the style's own files replace this one.

#include <QCoreApplication>

#include "yuzu/game/styles/dual_pane_style.h"
#include "yuzu/game/styles/library_style_view.h"
#include "yuzu/game/styles/style_helpers.h"

namespace {

LibraryStyles::LibraryStyleView* Make(QWidget* parent) {
    return LibraryStyles::MakePlaceholderView(
        parent, QCoreApplication::translate("LibraryStyles", "Dual pane"));
}

[[maybe_unused]] const bool registered =
    LibraryStyles::Register({"dual_pane", QT_TRANSLATE_NOOP("LibraryStyles", "Dual pane"), &Make});

} // namespace
