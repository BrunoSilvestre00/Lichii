// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QKeyEvent>
#include <QObject>

#include "common/input.h"
#include "common/settings_input.h"

namespace Core::HID {
using ButtonValues = std::array<Common::Input::ButtonStatus, Settings::NativeButton::NumButtons>;
using SticksValues = std::array<Common::Input::StickStatus, Settings::NativeAnalog::NumAnalogs>;
enum class ControllerTriggerType;
class EmulatedController;
class HIDCore;
} // namespace Core::HID

class ControllerNavigation : public QObject {
    Q_OBJECT

public:
    explicit ControllerNavigation(Core::HID::HIDCore& hid_core, QWidget* parent = nullptr);
    ~ControllerNavigation();

    /// Disables events from the emulated controller
    void UnloadController();

    /// Also report X, Y, L and R (as the X, Y, [ and ] keys), for the game library styles.
    void SetLibraryButtonsEnabled(bool enabled) {
        library_buttons = enabled;
    }

signals:
    void TriggerKeyboardEvent(Qt::Key key);

private:
    void TriggerButton(Settings::NativeButton::Values native_button, Qt::Key key);
    void ControllerUpdateEvent(Core::HID::ControllerTriggerType type);

    void ControllerUpdateButton();

    void ControllerUpdateStick();

    Core::HID::ButtonValues button_values{};
    Core::HID::SticksValues stick_values{};

    int player1_callback_key{};
    int handheld_callback_key{};
    bool is_controller_set{};
    bool library_buttons{};
    mutable std::mutex mutex;
    Core::HID::EmulatedController* player1_controller;
    Core::HID::EmulatedController* handheld_controller;
};
