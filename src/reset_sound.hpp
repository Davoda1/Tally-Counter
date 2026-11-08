// Copyright (c) 2026 davoda1
// SPDX-License-Identifier: Zlib
#pragma once

#include "click_sound.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tally::reset_sound {

// Fine gears: a quiet contact transient with two very short material modes.
// Audio follows the actual ResetSpin duration and angles supplied by the view.
inline constexpr unsigned sampleRate = 48000;
inline constexpr unsigned teethPerTurn = 24;
inline constexpr double level = .70;
inline constexpr bool finishTick = true;
inline constexpr double tailSeconds = .020;
inline constexpr double maximumDuration = 2.20;
// Includes endpoint padding and the ceiling of floating-point sample time.
inline constexpr std::size_t maximumSamples = 106562;
static_assert(sampleRate == click_sound::sampleRate, "reset/click sample rates must agree");

namespace detail {
inline constexpr double tau = 6.283185307179586;
inline constexpr std::size_t hitSamples = 960;

inline bool validMotion(double duration, double fromAngle, double toAngle) {
    // Reject invalid state before computing sizes or allocating buffers.
    // The native counter resets in .45..2.20 seconds and ends in 1..3 turns.
    return std::isfinite(duration) && std::isfinite(fromAngle) && std::isfinite(toAngle)
        && duration >= .45 && duration <= maximumDuration
        && fromAngle >= 0 && fromAngle < tau
        && toAngle >= tau && toAngle <= 3*tau && toAngle > fromAngle;
}

inline double smoothstep(double x) {
    x = std::clamp(x, 0., 1.);
    return x*x*(3-2*x);
}

inline double inverseSmoothstep(double x) {
    x = std::clamp(x, 0., 1.);
    return .5 - std::sin(std::asin(1-2*x)/3);
}

inline double normalizeAngle(double angle) {
    return std::fmod(std::fmod(angle, tau)+tau, tau);
}

class Noise {
    std::uint32_t state;
public:
    explicit Noise(std::uint32_t seed) : state(seed ? seed : 1) {}
    double next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state) / 4294967296.;
    }
};

inline double referenceEnergy() {
    static const double value = [] {
        double sum = 0;
        for (std::int16_t sample : click_sound::pcm) {
            // Signed 16-bit PCM maps exactly onto these binary fractions.
            const double x = static_cast<double>(sample)/32768.;
            sum += x*x;
        }
        return sum/sampleRate*.36;
    }();
    return value;
}

inline std::array<float, hitSamples> makeHit(unsigned tooth) {
    // Stable per-tooth variation; all arithmetic wraps modulo 2^32.
    Noise noise(0x54414c4cU ^ (68U*0x045d9f3bU) ^ ((tooth+1U)*0x119de1f3U));
    const double variation = .97 + noise.next()*.06;
    const double pitch = .990 + noise.next()*.020;
    const double high = 1-std::exp(-tau*(5700*pitch)/sampleRate);
    const double low = 1-std::exp(-tau*(1100*pitch)/sampleRate);
    double fast = 0, slow = 0;
    std::array<float, hitSamples> output{};
    for (std::size_t n = 0; n < output.size(); ++n) {
        const double t = static_cast<double>(n)/sampleRate;
        const double white = noise.next()*2-1;
        fast += high*(white-fast);
        slow += low*(white-slow);
        const double band = fast-slow;
        const double attack = smoothstep(t/.00030);
        double signal = band*attack*std::exp(-t/.00145);
        signal += .14*std::sin(tau*(2970*pitch)*t)*std::exp(-t/.0016)*attack;
        signal += .055*std::sin(tau*(4380*pitch)*t)*std::exp(-t/.0010)*attack;
        // A float store after each stage preserves the chosen PCM design.
        output[n] = static_cast<float>(signal*smoothstep((.020-t)/.005));
    }
    double sum = 0, peak = 0;
    for (float sample : output) {
        const double x = sample;
        sum += x*x;
        peak = std::max(peak, std::abs(x));
    }
    const double energy = sum/sampleRate;
    const double gain = std::min(std::sqrt(referenceEnergy()*.72/std::max(energy,1e-15)),
                                 .048/std::max(peak,1e-15))*variation;
    for (float& sample : output) sample = static_cast<float>(static_cast<double>(sample)*gain);
    output.front() = output.back() = 0;
    return output;
}

struct Contact {
    unsigned tooth;
    double seconds;
};

inline std::vector<Contact> contacts(double duration, double fromAngle, double toAngle) {
    if (!validMotion(duration, fromAngle, toAngle)) return {};
    const double start = normalizeAngle(fromAngle);
    const double sweep = toAngle-start;
    const double step = tau/teethPerTurn;
    unsigned tooth = static_cast<unsigned>(std::max(0., std::floor(start/step-.5)+1));
    std::vector<Contact> result;
    result.reserve(3*teethPerTurn);
    for (; (tooth+.5)*step < toAngle-1e-10; ++tooth) {
        const double angle = (tooth+.5)*step;
        if (angle <= start+1e-10) continue;
        result.push_back({tooth, inverseSmoothstep((angle-start)/sweep)*duration});
    }
    return result;
}

inline void addAt(std::vector<float>& output, const float* hit, std::size_t length,
                  double seconds) {
    const double exact = seconds*sampleRate;
    const auto offset = static_cast<std::size_t>(std::floor(exact));
    const double fraction = exact-static_cast<double>(offset);
    for (std::size_t n = 0; n < length; ++n) {
        const std::size_t i = offset+n;
        if (i < output.size())
            output[i] = static_cast<float>(static_cast<double>(output[i])
                                           + static_cast<double>(hit[n])*(1-fraction));
        if (i+1 < output.size())
            output[i+1] = static_cast<float>(static_cast<double>(output[i+1])
                                             + static_cast<double>(hit[n])*fraction);
    }
}

inline std::vector<float> renderSamples(double duration, double fromAngle, double toAngle) {
    if (!validMotion(duration, fromAngle, toAngle)) return {};
    const auto length = static_cast<std::size_t>(std::ceil((duration+tailSeconds)*sampleRate))+1;
    if (length > maximumSamples) return {};
    std::vector<float> output(length, 0);
    for (const Contact& contact : contacts(duration, fromAngle, toAngle)) {
        const auto hit = makeHit(contact.tooth);
        addAt(output, hit.data(), hit.size(), contact.seconds);
    }
    // The selected soft stop contact begins at the animation endpoint. Its
    // 18 ms release fits inside the reserved 20 ms tail, with no extra spin.
    const auto stopHit = makeHit(1009);
    std::array<float, 864> seat{};
    for (std::size_t n = 0; n < seat.size(); ++n) {
        const double fade = smoothstep(static_cast<double>(seat.size()-1-n)/(sampleRate*.005));
        seat[n] = static_cast<float>(static_cast<double>(stopHit[n])*.85*fade);
    }
    addAt(output, seat.data(), seat.size(), duration);
    constexpr std::size_t taper = 144;
    for (std::size_t n = 0; n <= taper; ++n) {
        float& sample = output[output.size()-1-n];
        sample = static_cast<float>(static_cast<double>(sample)
                                   *smoothstep(static_cast<double>(n)/taper));
    }
    output.front() = output.back() = 0;
    double peak = 0;
    for (float sample : output) peak = std::max(peak, std::abs(static_cast<double>(sample)));
    const double gain = std::min(1., .058/std::max(peak,1e-15))*level;
    for (float& sample : output) sample = static_cast<float>(static_cast<double>(sample)*gain);
    return output;
}
} // namespace detail

// Tooth contacts only; the separate soft stop contact is at duration seconds.
// Invalid motion returns an empty vector and never expands allocation limits.
inline std::vector<double> contactTimes(double duration, double fromAngle, double toAngle) {
    const auto contacts = detail::contacts(duration, fromAngle, toAngle);
    std::vector<double> times;
    times.reserve(contacts.size());
    for (const auto& contact : contacts) times.push_back(contact.seconds);
    return times;
}

inline std::vector<std::int16_t> render(double duration, double fromAngle, double toAngle) {
    const auto samples = detail::renderSamples(duration, fromAngle, toAngle);
    std::vector<std::int16_t> pcm;
    pcm.reserve(samples.size());
    for (float sample : samples) {
        // Round to nearest signed PCM value (halfway values round upward).
        const double value = std::floor(static_cast<double>(sample)*32768.+.5);
        pcm.push_back(static_cast<std::int16_t>(std::clamp(value,-32768.,32767.)));
    }
    return pcm;
}

// Self-contained mono PCM WAV for playback APIs requiring a memory WAV.
// Input must be a nonempty bounded reset buffer; invalid input returns {}.
inline std::vector<unsigned char> makeWav(const std::vector<std::int16_t>& pcm) {
    if (pcm.empty() || pcm.size() > maximumSamples) return {};
    const auto dataBytes = static_cast<std::uint32_t>(pcm.size()*2);
    std::vector<unsigned char> wav(44+dataBytes, 0);
    const auto text = [&wav](std::size_t at, const char* value) {
        for (std::size_t i = 0; i < 4; ++i) wav[at+i] = static_cast<unsigned char>(value[i]);
    };
    const auto u16 = [&wav](std::size_t at, std::uint16_t value) {
        wav[at] = static_cast<unsigned char>(value);
        wav[at+1] = static_cast<unsigned char>(value >> 8);
    };
    const auto u32 = [&wav](std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) wav[at+i] = static_cast<unsigned char>(value >> (i*8));
    };
    text(0,"RIFF"); u32(4,36+dataBytes); text(8,"WAVE"); text(12,"fmt ");
    u32(16,16); u16(20,1); u16(22,1); u32(24,sampleRate);
    u32(28,sampleRate*2); u16(32,2); u16(34,16); text(36,"data"); u32(40,dataBytes);
    for (std::size_t i = 0; i < pcm.size(); ++i) u16(44+i*2,static_cast<std::uint16_t>(pcm[i]));
    return wav;
}

} // namespace tally::reset_sound
