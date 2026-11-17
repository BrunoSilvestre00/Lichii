// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "input_common/helpers/steam_controller_haptics.h"

namespace {
class FakeTransport final : public InputCommon::SteamControllerHaptics::Transport {
public:
    bool Open(const std::string&) override {
        std::scoped_lock lock{mutex};
        ++opens;
        last_report.reset();
        return open_success;
    }

    bool Configure(u8 format) override {
        std::unique_lock lock{mutex};
        ++configures;
        wake.notify_all();
        wake.wait(lock, [this] { return !block_configure; });
        last_format = format;
        if (configures > 1 && recover_writes) {
            fail_writes = false;
        }
        return configure_success;
    }

    bool Write(const InputCommon::SteamHaptics::Report& report) override {
        std::scoped_lock lock{mutex};
        const bool success = !fail_writes;
        if (success && report[0] == 0x88) {
            ++pcm_writes;
            last_report = report;
        }
        wake.notify_all();
        return success;
    }

    void Silence(u8) override {
        std::scoped_lock lock{mutex};
        ++silences;
        wake.notify_all();
    }

    void Close() override {
        std::scoped_lock lock{mutex};
        ++closes;
        wake.notify_all();
    }

    bool WaitForPcmWrites(int count) {
        std::unique_lock lock{mutex};
        return wake.wait_for(lock, std::chrono::seconds{2},
                             [this, count] { return pcm_writes >= count; });
    }

    template <typename Predicate>
    std::optional<InputCommon::SteamHaptics::Report> WaitForReport(Predicate predicate) {
        std::unique_lock lock{mutex};
        if (!wake.wait_for(lock, std::chrono::seconds{2},
                           [&] { return last_report && predicate(*last_report); })) {
            return std::nullopt;
        }
        return last_report;
    }

    bool WaitForSilence() {
        std::unique_lock lock{mutex};
        return wake.wait_for(lock, std::chrono::seconds{3}, [this] { return silences > 0; });
    }

    void BlockConfigure(bool block) {
        std::scoped_lock lock{mutex};
        block_configure = block;
        wake.notify_all();
    }

    bool WaitForConfigure() {
        std::unique_lock lock{mutex};
        return wake.wait_for(lock, std::chrono::seconds{2}, [this] { return configures > 0; });
    }

    void SetOpenSuccess(bool success) {
        std::scoped_lock lock{mutex};
        open_success = success;
    }

    void SetConfigureSuccess(bool success) {
        std::scoped_lock lock{mutex};
        configure_success = success;
    }

    void SetWriteFailures(bool fail) {
        std::scoped_lock lock{mutex};
        fail_writes = fail;
    }

    void RecoverWrites() {
        std::scoped_lock lock{mutex};
        recover_writes = true;
    }

    int ConfigureCount() {
        std::scoped_lock lock{mutex};
        return configures;
    }

    int CloseCount() {
        std::scoped_lock lock{mutex};
        return closes;
    }

    int OpenCount() {
        std::scoped_lock lock{mutex};
        return opens;
    }

    u8 LastFormat() {
        std::scoped_lock lock{mutex};
        return last_format;
    }

private:
    std::mutex mutex;
    std::condition_variable wake;
    int opens{};
    int configures{};
    int pcm_writes{};
    int silences{};
    int closes{};
    u8 last_format{};
    std::optional<InputCommon::SteamHaptics::Report> last_report;
    bool configure_success{true};
    bool open_success{true};
    bool block_configure{};
    bool fail_writes{};
    bool recover_writes{};
};

bool HasGripState(const InputCommon::SteamHaptics::Report& report, bool left, bool right) {
    const auto active = [](auto begin, auto end) {
        return std::any_of(begin, end, [](u8 sample) { return sample != 0xff && sample != 0x7f; });
    };
    return active(report.begin() + 2, report.begin() + 33) == left &&
           active(report.begin() + 33, report.end()) == right;
}

Common::Input::VibrationStatus Vibration(Common::Input::VibrationSide side) {
    return {
        .low_amplitude = 0.4f,
        .low_frequency = 220.0f,
        .high_amplitude = 0.3f,
        .high_frequency = 660.0f,
        .type = Common::Input::VibrationAmplificationType::Linear,
        .side = side,
    };
}
} // namespace

TEST_CASE("Steam Controller haptics close and reopen cleanly", "[input][rumble]") {
    auto transport = std::make_unique<FakeTransport>();
    auto* const fake = transport.get();
    InputCommon::SteamControllerHaptics haptics{0x1302, std::move(transport)};

    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Left), true));
    const auto left =
        fake->WaitForReport([](const auto& report) { return HasGripState(report, true, false); });
    REQUIRE(left);
    CHECK(haptics.IsStreaming());
    REQUIRE(fake->LastFormat() == 8);
    const auto& left_report = *left;
    REQUIRE(left_report[1] == 31);
    REQUIRE(left_report[33] == 0xff);
    REQUIRE(std::any_of(left_report.begin() + 2, left_report.begin() + 33,
                        [](u8 sample) { return sample != 0xff && sample != 0x7f; }));
    // Let old reports accumulate before requesting a different grip state.
    REQUIRE(fake->WaitForPcmWrites(3));
    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Right), true));
    const auto both =
        fake->WaitForReport([](const auto& report) { return HasGripState(report, true, true); });
    REQUIRE(both);
    const auto& both_report = *both;
    REQUIRE(std::any_of(both_report.begin() + 2, both_report.begin() + 33,
                        [](u8 sample) { return sample != 0xff && sample != 0x7f; }));
    REQUIRE(std::any_of(both_report.begin() + 33, both_report.end(),
                        [](u8 sample) { return sample != 0xff && sample != 0x7f; }));
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Left), false));
    REQUIRE(fake->CloseCount() == 1);

    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Left), true));
    const auto left_only =
        fake->WaitForReport([](const auto& report) { return HasGripState(report, true, false); });
    REQUIRE(left_only);
    REQUIRE(fake->LastFormat() == 8);
    const auto& left_only_report = *left_only;
    REQUIRE(std::any_of(left_only_report.begin() + 2, left_only_report.begin() + 33,
                        [](u8 sample) { return sample != 0xff && sample != 0x7f; }));
    REQUIRE(std::all_of(left_only_report.begin() + 33, left_only_report.end(),
                        [](u8 sample) { return sample == 0xff; }));
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), false));
    REQUIRE(fake->CloseCount() == 2);

    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Right), true));
    const auto reopened =
        fake->WaitForReport([](const auto& report) { return HasGripState(report, false, true); });
    REQUIRE(reopened);
    const auto& reopened_report = *reopened;
    REQUIRE(reopened_report[2] == 0xff);
    REQUIRE(std::any_of(reopened_report.begin() + 33, reopened_report.end(),
                        [](u8 sample) { return sample != 0xff && sample != 0x7f; }));
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), false));
    REQUIRE(fake->CloseCount() == 3);
    CHECK_FALSE(haptics.IsStreaming());
    haptics.Disconnect();
    CHECK_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true));
    CHECK(fake->OpenCount() == 3);
}

TEST_CASE("Steam Controller haptics recover once before falling back", "[input][rumble]") {
    auto transport = std::make_unique<FakeTransport>();
    auto* const fake = transport.get();
    fake->SetWriteFailures(true);
    InputCommon::SteamControllerHaptics haptics{0x1303, std::move(transport)};

    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true));
    REQUIRE(fake->WaitForSilence());
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true));
    REQUIRE(fake->ConfigureCount() == 2);
    REQUIRE(fake->LastFormat() == 9);
    REQUIRE(fake->CloseCount() == 1);
}

TEST_CASE("Steam Controller haptics fall back after setup failure", "[input][rumble]") {
    auto transport = std::make_unique<FakeTransport>();
    auto* const fake = transport.get();
    fake->SetConfigureSuccess(false);
    InputCommon::SteamControllerHaptics haptics{0x1304, std::move(transport)};

    haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true);
    REQUIRE(fake->WaitForSilence());
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true));
    REQUIRE(fake->CloseCount() == 1);
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), false));
    fake->SetConfigureSuccess(true);
    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true));
    REQUIRE(fake->WaitForPcmWrites(1));
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), false));
    REQUIRE(fake->CloseCount() == 2);
}

TEST_CASE("Steam haptics setup does not block updates and uses the latest targets",
          "[input][rumble]") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    fake->BlockConfigure(true);
    InputCommon::SteamControllerHaptics haptics{0x1302, std::move(transport)};
    const bool accepted = haptics.SetVibration(Vibration(Common::Input::VibrationSide::Left), true);
    const bool configuring = fake->WaitForConfigure();
    auto stop_left = Vibration(Common::Input::VibrationSide::Left);
    stop_left.low_amplitude = stop_left.high_amplitude = 0;
    haptics.SetVibration(stop_left, true);
    haptics.SetVibration(Vibration(Common::Input::VibrationSide::Right), true);
    fake->BlockConfigure(false);
    REQUIRE(accepted);
    REQUIRE(configuring);
    REQUIRE(
        fake->WaitForReport([](const auto& report) { return HasGripState(report, false, true); }));
}

TEST_CASE("Steam haptics disconnect during setup closes without streaming", "[input][rumble]") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    fake->BlockConfigure(true);
    InputCommon::SteamControllerHaptics haptics{0x1302, std::move(transport)};
    haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true);
    const bool configuring = fake->WaitForConfigure();
    auto disconnected = std::async(std::launch::async, [&] { haptics.Disconnect(); });
    fake->BlockConfigure(false);
    disconnected.get();
    REQUIRE(configuring);
    REQUIRE(fake->CloseCount() == 1);
    REQUIRE_FALSE(haptics.IsStreaming());
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true));
}

TEST_CASE("Steam haptics open failure permits fallback", "[input][rumble]") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    fake->SetOpenSuccess(false);
    InputCommon::SteamControllerHaptics haptics{0x1302, std::move(transport)};
    haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true);
    haptics.SetVibration({}, false);
    REQUIRE(fake->OpenCount() == 1);
    REQUIRE(fake->CloseCount() == 0);
}

TEST_CASE("Steam haptics resume streaming after one recovery", "[input][rumble]") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    fake->SetWriteFailures(true);
    fake->RecoverWrites();
    InputCommon::SteamControllerHaptics haptics{0x1302, std::move(transport)};
    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true));
    REQUIRE(
        fake->WaitForReport([](const auto& report) { return HasGripState(report, true, true); }));
    REQUIRE(fake->ConfigureCount() == 2);
    REQUIRE(haptics.IsStreaming());
}

TEST_CASE("Steam haptics require repeated updates after asynchronous setup failure",
          "[input][rumble]") {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    fake->BlockConfigure(true);
    fake->SetConfigureSuccess(false);
    InputCommon::SteamControllerHaptics haptics{0x1302, std::move(transport)};
    const auto request = Vibration(Common::Input::VibrationSide::Both);
    const bool requires_before = haptics.RequiresUpdates();
    const bool accepted = haptics.SetVibration(request, true);
    const bool configuring = fake->WaitForConfigure();
    fake->BlockConfigure(false);
    const bool silenced = fake->WaitForSilence();
    REQUIRE(requires_before);
    REQUIRE(accepted);
    REQUIRE(configuring);
    REQUIRE(silenced);
    REQUIRE_FALSE(haptics.IsStreaming());
    REQUIRE(haptics.RequiresUpdates());
    REQUIRE_FALSE(haptics.SetVibration(request, true));
    haptics.Disconnect();
    REQUIRE_FALSE(haptics.RequiresUpdates());
}
