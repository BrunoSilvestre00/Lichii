// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "input_common/helpers/steam_controller_haptics.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#include "common/logging.h"

#ifdef _WIN32
#include <SDL3/SDL_hidapi.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#endif

namespace InputCommon {

namespace {
#ifdef _WIN32
class SDLHidTransport final : public SteamControllerHaptics::Transport {
public:
    ~SDLHidTransport() override {
        Close();
    }

    bool Open(const std::string& path) override {
        write_error_logged = false;
        hid = SDL_hid_open_path(path.c_str());
        if (!hid) {
            LOG_WARNING(Input, "Steam haptics HID open: {}", SDL_GetError());
        }
        return hid != nullptr;
    }

    bool Configure(u8 format) override {
        SteamHaptics::Report zero_rumble{};
        zero_rumble[0] = 0x80;
        if (!Write(zero_rumble) || !Mode(1, format) || !Mode(1, 0) || !Mode(2, format)) {
            LOG_WARNING(Input, "Steam haptics configure format={}: {}", format, SDL_GetError());
            return false;
        }
        SDL_Delay(200);
        return true;
    }

    bool Write(const SteamHaptics::Report& report) override {
        if (!hid) {
            return false;
        }
        // Windows can pad output reports and return the padded size.
        const int written = SDL_hid_write(hid, report.data(), report.size());
        const bool complete = SteamHaptics::CompleteReportWrite(written, report.size());
        if (!complete && !write_error_logged) {
            LOG_WARNING(Input, "Steam haptics HID write returned {} of {} bytes: {}", written,
                        report.size(), SDL_GetError());
            write_error_logged = true;
        }
        return complete;
    }

    void Silence(u8 format) override {
        if (hid) {
            (void)Mode(1, format);
            (void)Mode(1, 0);
        }
    }

    void Close() override {
        if (hid) {
            SDL_hid_close(hid);
            hid = nullptr;
        }
    }

private:
    bool Mode(u8 operation, u8 format) {
        bool success = true;
        for (u8 channel : {u8{2}, u8{5}}) {
            SteamHaptics::Report report{};
            report[0] = 0x86;
            report[1] = operation;
            report[2] = channel;
            report[3] = format;
            success = Write(report) && success;
        }
        return success;
    }

    SDL_hid_device* hid{};
    bool write_error_logged{};
};
#endif

std::unique_ptr<SteamControllerHaptics::Transport> MakeTransport() {
#ifdef _WIN32
    return std::make_unique<SDLHidTransport>();
#else
    return {};
#endif
}
} // namespace

struct SteamControllerHaptics::Impl {
    std::string path;
    u16 product_id;
    std::unique_ptr<Transport> transport;
    mutable std::mutex state_mutex;
    std::array<SteamHaptics::Target, 2> targets{};
    std::mutex target_mutex;
    std::condition_variable wake;
    u64 generation{};
    std::thread sender;
    enum class State { Stopped, Starting, Streaming, Failed, Disconnected };
    std::atomic<State> state{State::Stopped};
    std::atomic<bool> running{};
    bool attempted{};
    bool fallback_logged{};
    bool disconnected{};

    Impl(std::string path_, u16 product_id_, std::unique_ptr<Transport> transport_)
        : path{std::move(path_)}, product_id{product_id_},
          transport{transport_ ? std::move(transport_) : MakeTransport()} {}

    u8 Format() const {
        return SteamHaptics::FormatForProduct(product_id);
    }

    bool HasActiveTargets() {
        std::scoped_lock lock{target_mutex};
        return SteamHaptics::Active(targets[0]) || SteamHaptics::Active(targets[1]);
    }

    bool Start() {
        attempted = true;
        running.store(true);
        state.store(State::Starting);
        try {
            sender = std::thread([this] { Run(); });
        } catch (const std::system_error& error) {
            running.store(false);
            state.store(State::Failed);
            LOG_WARNING(Input, "Steam Controller haptics thread failed: {}", error.what());
            return false;
        }
        return true;
    }

    void Stop() {
        {
            std::scoped_lock lock{target_mutex};
            running.store(false);
        }
        wake.notify_all();
        if (sender.joinable()) {
            sender.join();
        }
        state.store(disconnected ? State::Disconnected : State::Stopped);
    }

    void Run() {
        if (!transport || !transport->Open(path)) {
            LOG_WARNING(Input, "Steam Controller haptics open failed, product={:04x}", product_id);
            state.store(State::Failed);
            running.store(false);
            return;
        }
        if (!transport->Configure(Format()) || !running.load()) {
            state.store(State::Failed);
            running.store(false);
            transport->Silence(Format());
            transport->Close();
            return;
        }
        state.store(State::Streaming);
        LOG_INFO(Input, "Steam haptics streaming: product={:04x}, format={}, rate={} Hz",
                 product_id, Format(), SteamHaptics::SampleRateForProduct(product_id));
#ifdef _WIN32
        if (!SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH)) {
            LOG_DEBUG(Input, "Could not raise Steam haptics thread priority: {}", SDL_GetError());
        }
#endif
        const u32 rate = SteamHaptics::SampleRateForProduct(product_id);
        const auto period = std::chrono::nanoseconds{static_cast<std::chrono::nanoseconds::rep>(
            SteamHaptics::SamplesPerGrip * 1'000'000'000ULL / rate)};
        SteamHaptics::GripPcm pcm{rate};
        auto deadline = std::chrono::steady_clock::now();
        auto last_active = deadline;
        int failures = 0;
        bool recovered = false;

        while (running.load()) {
            std::array<SteamHaptics::Target, 2> snapshot;
            const auto now = std::chrono::steady_clock::now();
            {
                std::unique_lock lock{target_mutex};
                snapshot = targets;
                if (!SteamHaptics::Active(snapshot[0]) && !SteamHaptics::Active(snapshot[1])) {
                    if (now - last_active >= std::chrono::milliseconds{100}) {
                        const u64 seen = generation;
                        wake.wait(lock,
                                  [this, seen] { return !running.load() || generation != seen; });
                        deadline = std::chrono::steady_clock::now();
                        continue;
                    }
                } else {
                    last_active = now;
                }
            }

            failures = transport->Write(pcm.Build(snapshot)) ? 0 : failures + 1;
            if (failures >= 100) {
                if (recovered || !transport->Configure(Format())) {
                    LOG_WARNING(Input, "Steam Controller haptics transport failed; standard rumble "
                                       "will be used on the next vibration update");
                    break;
                }
                recovered = true;
                failures = 0;
                deadline = std::chrono::steady_clock::now();
                pcm = SteamHaptics::GripPcm{rate};
                LOG_INFO(Input, "Steam Controller haptics transport recovered once");
            }

            deadline += period;
            const auto after_write = std::chrono::steady_clock::now();
            if (deadline > after_write) {
#ifdef _WIN32
                const auto remaining =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - after_write)
                        .count();
                SDL_DelayPrecise(static_cast<u64>(remaining));
#else
                std::this_thread::sleep_until(deadline);
#endif
            } else if (after_write - deadline > period) {
                deadline = after_write;
            }
        }
        state.store(running.load() ? State::Failed : State::Stopped);
        running.store(false);
        transport->Silence(Format());
        transport->Close();
    }

    bool SetVibration(const Common::Input::VibrationStatus& vibration, bool enabled) {
        std::scoped_lock lock{state_mutex};
        if (disconnected) {
            return false;
        }
        const auto target =
            SteamHaptics::Sanitize({vibration.low_amplitude, vibration.low_frequency,
                                    vibration.high_amplitude, vibration.high_frequency});
        if (!enabled) {
            {
                std::scoped_lock target_lock{target_mutex};
                targets = {};
                ++generation;
            }
            wake.notify_all();
            Stop();
            attempted = false;
            fallback_logged = false;
            return false;
        }

        {
            std::scoped_lock target_lock{target_mutex};
            if (vibration.side != Common::Input::VibrationSide::Right) {
                targets[0] = target;
            }
            if (vibration.side != Common::Input::VibrationSide::Left) {
                targets[1] = target;
            }
            ++generation;
        }
        wake.notify_all();

        if (!running.load() && !HasActiveTargets()) {
            if (attempted) {
                Stop();
                return false;
            }
            return true;
        }
        if (!attempted) {
            (void)Start();
        }
        if (!running.load()) {
            if (sender.joinable()) {
                sender.join();
            }
            if (!fallback_logged) {
                LOG_WARNING(Input, "Steam Controller haptics: using standard rumble fallback "
                                   "(frequencies are lost)");
                fallback_logged = true;
            }
            return false;
        }
        return true;
    }

    ~Impl() {
        std::scoped_lock lock{state_mutex};
        Stop();
    }
};

SteamControllerHaptics::SteamControllerHaptics(std::string path, u16 product_id)
    : impl{std::make_unique<Impl>(std::move(path), product_id, nullptr)} {}

SteamControllerHaptics::SteamControllerHaptics(u16 product_id, std::unique_ptr<Transport> transport)
    : impl{std::make_unique<Impl>(std::string{}, product_id, std::move(transport))} {}

SteamControllerHaptics::~SteamControllerHaptics() = default;

bool SteamControllerHaptics::SetVibration(const Common::Input::VibrationStatus& vibration,
                                          bool enabled) {
    return impl->SetVibration(vibration, enabled);
}

bool SteamControllerHaptics::IsStreaming() const {
    const auto state = impl->state.load();
    return impl->running.load() &&
           (state == Impl::State::Starting || state == Impl::State::Streaming);
}

bool SteamControllerHaptics::RequiresUpdates() const {
    // Accepted requests can fail asynchronously, so identical updates must reach fallback too.
    std::scoped_lock lock{impl->state_mutex};
    return !impl->disconnected;
}

void SteamControllerHaptics::Disconnect() {
    std::scoped_lock lock{impl->state_mutex};
    impl->disconnected = true;
    {
        std::scoped_lock target_lock{impl->target_mutex};
        impl->targets = {};
        ++impl->generation;
    }
    impl->wake.notify_all();
    impl->Stop();
}

} // namespace InputCommon
