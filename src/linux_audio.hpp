// SPDX-License-Identifier: Zlib
// Copyright (c) 2026 davoda1
#pragma once
#include "click_sound.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace tally {
// ALSA stays on this worker. The most recent sound replaces any unfinished
// playback instead of building a queue. A tally always uses the original click.
class ClickAudio final {
    struct Pcm;
    using Frames = long;
    using UnsignedFrames = unsigned long;
    using ErrorHandler = void (*)(const char*, int, const char*, int, const char*, ...);
    struct Alsa {
        void* library = nullptr;
        int (*open)(Pcm**, const char*, int, int) = nullptr;
        int (*setParams)(Pcm*, int, int, unsigned, unsigned, int, unsigned) = nullptr;
        Frames (*write)(Pcm*, const void*, UnsignedFrames) = nullptr;
        int (*prepare)(Pcm*) = nullptr;
        int (*drop)(Pcm*) = nullptr;
        int (*drain)(Pcm*) = nullptr;
        int (*close)(Pcm*) = nullptr;
        int (*errorHandler)(ErrorHandler) = nullptr;

        template<class T> bool symbol(T& function, const char* name) {
            void* address = dlsym(library, name);
            static_assert(sizeof(function) == sizeof(address), "POSIX function pointer size");
            std::memcpy(&function, &address, sizeof(function));
            return address != nullptr;
        }
        bool load() {
            library = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
            return library && symbol(open, "snd_pcm_open") &&
                symbol(setParams, "snd_pcm_set_params") && symbol(write, "snd_pcm_writei") &&
                symbol(prepare, "snd_pcm_prepare") && symbol(drop, "snd_pcm_drop") &&
                symbol(drain, "snd_pcm_drain") && symbol(close, "snd_pcm_close") &&
                symbol(errorHandler, "snd_lib_error_set_handler");
        }
        ~Alsa() { if (library) dlclose(library); }
    };

    struct State {
        std::mutex mutex;
        std::condition_variable wake;
        bool stopping = false;
        bool pending = false;
        bool finished = false;
        // Null denotes the static original click; otherwise the worker owns
        // this reset buffer until playback finishes or a new request arrives.
        std::shared_ptr<const std::vector<std::int16_t>> pendingReset;
    };
    std::shared_ptr<State> state = std::make_shared<State>();
    std::thread worker;

    static void ignoreAlsaError(const char*, int, const char*, int, const char*, ...) {}

    static void run(const std::shared_ptr<State>& state) {
        using Clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;
        Alsa alsa;
        if (!alsa.load()) return;
        // Device absence is normal on some desktops. It must not produce a
        // dialog, write a stream of diagnostics, or affect counting.
        alsa.errorHandler(ignoreAlsaError);
        Pcm* pcm = nullptr;
        auto retryAfter = Clock::time_point::min();
        bool quit = false;
        while (!quit) {
            std::shared_ptr<const std::vector<std::int16_t>> reset;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                state->wake.wait(lock, [&state] { return state->stopping || state->pending; });
                if (state->stopping) break;
                reset = std::move(state->pendingReset);
                state->pending = false;
            }
            if (Clock::now() < retryAfter) continue;
            if (!pcm) {
                // Stable ALSA ABI: playback=0, nonblock=1, S16_LE=2,
                // RW_INTERLEAVED=3. The packaged Linux executable is x86-64.
                const int result = alsa.open(&pcm, "default", 0, 1);
                if (result < 0 || !pcm ||
                    alsa.setParams(pcm, 2, 3, 1, click_sound::sampleRate, 1, 20000) < 0) {
                    if (pcm) alsa.close(pcm);
                    pcm = nullptr;
                    retryAfter = Clock::now() + 5s;
                    continue;
                }
            }
            alsa.drop(pcm);
            bool failed = alsa.prepare(pcm) < 0;
            std::size_t offset = 0;
            unsigned recoveries = 0;
            const auto playbackBudget = [&reset] {
                // Keep the original click's short timeout, while allowing the
                // complete count-dependent reset and its small seating tick.
                return 250ms + std::chrono::milliseconds(reset
                    ? (reset->size() * 1000 + click_sound::sampleRate - 1)
                        / click_sound::sampleRate : 0);
            };
            auto deadline = Clock::now() + playbackBudget();
            while (!failed && !quit) {
                bool restart = false;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    quit = state->stopping;
                    restart = state->pending;
                    if (restart) reset = std::move(state->pendingReset);
                    state->pending = false;
                }
                if (quit) break;
                if (restart) {
                    alsa.drop(pcm);
                    if (alsa.prepare(pcm) < 0) { failed = true; break; }
                    offset = 0;
                    recoveries = 0;
                    deadline = Clock::now() + playbackBudget();
                }
                if (Clock::now() >= deadline) { failed = true; break; }
                Frames result;
                const auto count = reset ? reset->size() : click_sound::pcm.size();
                const auto* samples = reset ? reset->data() : click_sound::pcm.data();
                if (offset < count) {
                    const auto frames = std::min<std::size_t>(240, count - offset);
                    result = alsa.write(pcm, samples + offset,
                                        static_cast<UnsignedFrames>(frames));
                    if (result > 0) {
                        offset += static_cast<std::size_t>(result);
                        continue;
                    }
                } else {
                    result = alsa.drain(pcm); // Nonblocking; interruptible below.
                    if (result == 0) break;
                }
                if ((result == -EPIPE || result == -ESTRPIPE) && recoveries++ == 0) {
                    if (alsa.prepare(pcm) >= 0) { offset = 0; continue; }
                }
                if (result != -EAGAIN && result != 0) { failed = true; break; }
                std::unique_lock<std::mutex> lock(state->mutex);
                state->wake.wait_for(lock, 1ms, [&state] { return state->stopping || state->pending; });
            }
            if (failed) {
                alsa.drop(pcm);
                alsa.close(pcm);
                pcm = nullptr;
                retryAfter = Clock::now() + 5s;
            }
        }
        if (pcm) { alsa.drop(pcm); alsa.close(pcm); }
        alsa.errorHandler(nullptr);
    }

public:
    ClickAudio() {
        // Counting still works if the system cannot start a sound worker.
        try {
            worker = std::thread([shared = state] {
                try { run(shared); } catch (...) {}
                {
                    std::lock_guard<std::mutex> lock(shared->mutex);
                    shared->finished = true;
                }
                shared->wake.notify_one();
            });
        } catch (const std::system_error&) {}
    }
    ClickAudio(const ClickAudio&) = delete;
    ClickAudio& operator=(const ClickAudio&) = delete;
    ~ClickAudio() {
        if (!worker.joinable()) return;
        std::unique_lock<std::mutex> lock(state->mutex);
        state->stopping = true;
        state->pending = false;
        state->pendingReset.reset();
        state->wake.notify_one();
        // Nonblocking PCM operations normally stop immediately. A desktop
        // plugin may nevertheless stall while connecting to its audio server;
        // it must not hold app shutdown hostage. Shared state owns all worker
        // data until that call returns (or the process exits).
        const bool finished = state->wake.wait_for(lock, std::chrono::milliseconds(150),
                                                   [this] { return state->finished; });
        lock.unlock();
        if (finished) worker.join();
        else worker.detach();
    }
    void trigger() {
        if (!worker.joinable()) return;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->pendingReset.reset();
            state->pending = true;
        }
        state->wake.notify_one();
    }
    void triggerReset(std::vector<std::int16_t> samples) {
        if (!worker.joinable() || samples.empty()) return;
        auto buffer = std::make_shared<const std::vector<std::int16_t>>(std::move(samples));
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->pendingReset = std::move(buffer);
            state->pending = true;
        }
        state->wake.notify_one();
    }
};
} // namespace tally
