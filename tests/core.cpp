#include "counter.hpp"
#include "ui.hpp"
#include "click_sound.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
unsigned long checks = 0;

void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

std::string fourDigits(int value) {
    const std::string text = std::to_string(value);
    return std::string(4 - text.size(), '0') + text;
}

void settledAt(const tally::View& view, int expected, double now) {
    check(view.counter.value == expected, "counter does not match expected value");
    const std::string digits = fourDigits(expected);
    check(view.counter.digits() == digits, "four-digit formatting is incorrect");
    check(!view.animating(now), "animation has not finished after ample settling time");
    for (int i = 0; i < 4; ++i) {
        const double position = view.displayPosition(i, now);
        check(std::isfinite(position), "wheel position is not finite");
        double digit = std::fmod(position, 10.0);
        if (digit < 0) digit += 10.0;
        check(std::abs(digit - (digits[i] - '0')) < 1e-6,
              "settled wheel does not show its corresponding digit");
    }
}

void validPersistence() {
    tally::Counter counter;
    for (int value = 0; value <= tally::maximum; ++value) {
        const std::string plain = std::to_string(value);
        const std::string padded = fourDigits(value);
        check(tally::parseCount(plain) == value, "valid decimal persistence rejected");
        check(tally::parseCount(padded) == value, "zero-padded persistence rejected");
        check(tally::parseCount(" \t\r\n" + padded + "\n\t ") == value,
              "surrounding whitespace is not handled correctly");
        counter.restore(plain);
        check(counter.value == value, "restore changed a valid count");
        check(counter.digits() == padded, "valid count does not have four digits");
    }
    check(tally::parseCount(std::string(1024, '0') + "17") == 17,
          "leading zeros changed a valid count");
}

void invalidPersistence() {
    const std::vector<std::string> invalid = {
        "", " \t\r\n", "-1", "+1", "10000", "99999", "1000000000000000000000000000",
        "18446744073709551615", "-9999", "1.0", "1e3", "0x10", "NaN", "Infinity",
        "1 2", "1\n2", " 123 trailing", "count=12", "１２", std::string("12\0junk", 7),
        std::string(1, static_cast<char>(0xff)) + "12"
    };
    tally::Counter counter;
    for (const auto& text : invalid) {
        counter.restore("4321");
        counter.restore(text);
        check(counter.value == 0, "malformed or out-of-range persistence was accepted");
        check(counter.digits() == "0000", "invalid persistence did not recover to 0000");
    }
}

void saturationAndReset() {
    tally::Counter counter;
    check(counter.value == 0 && counter.digits() == "0000", "incorrect initial count");
    check(!counter.reset(), "resetting zero incorrectly reports a change");
    for (int value = 1; value <= tally::maximum; ++value) {
        check(counter.increment(), "increment below maximum was rejected");
        check(counter.value == value, "increment skipped or repeated a value");
    }
    for (int attempt = 0; attempt < 1000; ++attempt) {
        check(!counter.increment(), "increment at 9999 reported a change");
        check(counter.value == tally::maximum, "count overflowed or wrapped at 9999");
    }
    check(counter.digits() == "9999", "maximum was formatted incorrectly");
    check(counter.reset(), "reset from maximum did not report a change");
    check(counter.value == 0 && counter.digits() == "0000", "reset did not clear count");
    check(counter.increment() && counter.value == 1, "cannot increment after reset");
}

void carries() {
    const std::vector<std::pair<int, int>> transitions = {
        {0, 1}, {8, 9}, {9, 10}, {99, 100}, {999, 1000}, {9998, 9999}
    };
    double now = 10.0;
    for (const auto& transition : transitions) {
        tally::View view;
        view.restore(std::to_string(transition.first), now);
        settledAt(view, transition.first, now);
        check(view.increment(now + 0.01), "view rejected increment below maximum");
        check(view.counter.value == transition.second, "view deferred count until animation");
        settledAt(view, transition.second, now + 2.0);
        now += 3.0;
    }
}

void rapidInput() {
    tally::View view;
    double now = 100.0;
    view.restore("0", now);
    for (int value = 1; value <= 1000; ++value) {
        now += 0.0003;
        check(view.increment(now), "rapid increment was dropped");
        check(view.counter.value == value, "rapid input did not update the count immediately");
        for (const auto& wheel : view.wheels)
            check(std::isfinite(wheel.position(now)), "rapid input produced an invalid wheel position");
    }
    settledAt(view, 1000, now + 2.0);
    for (int value = 1001; value <= tally::maximum; ++value) {
        now += 0.0003;
        check(view.increment(now), "increment was dropped approaching the maximum");
    }
    for (int attempt = 0; attempt < 100; ++attempt) {
        now += 0.0003;
        check(!view.increment(now), "view incremented beyond 9999");
        check(view.counter.value == tally::maximum, "view overflowed beyond 9999");
    }
    settledAt(view, tally::maximum, now + 2.0);
}

void interruptedAnimation() {
    tally::View view;
    const double now = 500.0;
    view.restore("999", now);
    check(view.increment(now + 0.01), "carry increment failed");
    check(view.increment(now + 0.02), "second increment during roll failed");
    check(view.reset(now + 0.04), "reset during roll did not report a change");
    check(view.counter.value == 0, "reset did not clear the count immediately");
    settledAt(view, 0, now + 2.0);
    check(!view.reset(now + 2.1), "resetting zero incorrectly reports a change");
    settledAt(view, 0, now + 4.0);
    check(view.increment(now + 4.01), "cannot increment following reset during animation");
    settledAt(view, 1, now + 6.0);

    view.restore("9998", now + 7.0);
    check(view.increment(now + 7.01), "increment to maximum failed");
    check(!view.increment(now + 7.02), "maximum was not respected during animation");
    check(view.reset(now + 7.03), "reset from maximum during animation failed");
    // The maximum-count knob rotation lasts longer than the old digit roll.
    settledAt(view, 0, now + 10.0);
}

void restoreSynchronizesDisplay() {
    tally::View view;
    view.restore("9", 1000.0);
    check(view.increment(1000.01), "increment before restore failed");
    view.restore("2468", 1000.02);
    settledAt(view, 2468, 1002.02);
    view.restore("corrupt", 1003.0);
    settledAt(view, 0, 1005.0);
}

void resetRotationTiming() {
    constexpr double revolution = 6.283185307179586;
    constexpr double now = 1500.0;
    double previousDuration = 0;
    for (int previousCount : {0, 1, 100, 1000, tally::maximum}) {
        tally::View view;
        view.restore(std::to_string(previousCount), now - 1);
        check(view.reset(now) == (previousCount != 0), "reset reports the wrong count change");
        check(view.counter.value == 0 && view.counter.digits() == "0000",
              "rotation delayed clearing the count");
        const std::string previousDigits = fourDigits(previousCount);
        for (int i = 0; i < 4; ++i)
            check(std::abs(std::fmod(view.displayPosition(i, now), 10.0)
                           - (previousDigits[i] - '0')) < 1e-9,
                  "rewind did not begin at the previously displayed count");
        check(view.rewinding(now) == (previousCount != 0),
              "rewind activity does not match the previous count");

        const double duration = view.resetSpin.seconds;
        check(std::isfinite(duration) && duration > 0 && duration <= 2.2 + 1e-9,
              "reset rotation duration is invalid or unbounded");
        check(duration > previousDuration, "a larger count did not rotate longer");
        previousDuration = duration;
        check(view.resetSpin.moving(now) && view.animating(now),
              "reset did not keep the animation timer active");
        double lastAngle = view.resetSpin.angle(now);
        for (double fraction : {0.25, 0.5, 0.75, 1.0}) {
            const double angle = view.resetSpin.angle(now + duration * fraction);
            check(std::isfinite(angle) && angle > lastAngle,
                  "reset rotation did not progress clockwise");
            lastAngle = angle;
        }
        const double finished = now + duration + 0.01;
        check(!view.resetSpin.moving(finished), "reset rotation did not finish");
        check(std::abs(std::remainder(view.resetSpin.angle(finished), revolution)) < 1e-9,
              "reset knob did not return to its resting orientation");
        settledAt(view, 0, finished);
    }
}

void inputDuringResetRotation() {
    constexpr double revolution = 6.283185307179586;
    tally::View view;
    view.restore("9999", 2000.0);
    check(view.reset(2000.01), "initial rotating reset failed");
    const double originalFinish = view.resetSpin.started + view.resetSpin.seconds;
    const double beforeRepeatedReset = view.resetSpin.angle(2000.4);
    const double beforeRepeatedValue = view.remainingResetValue(2000.4);
    std::array<double, 4> beforeRepeatedDigits{};
    for (int i = 0; i < 4; ++i) beforeRepeatedDigits[i] = view.displayPosition(i, 2000.4);
    check(!view.reset(2000.4), "repeated reset at zero changed the count");
    check(view.resetSpin.started + view.resetSpin.seconds == originalFinish,
          "repeated reset at zero extended the rotation");
    check(view.resetSpin.angle(2000.4) == beforeRepeatedReset,
          "repeated reset at zero changed the current angle");
    check(view.remainingResetValue(2000.4) == beforeRepeatedValue && beforeRepeatedValue > 0,
          "repeated reset at zero snapped or restarted the digit rewind");
    for (int i = 0; i < 4; ++i)
        check(view.displayPosition(i, 2000.4) == beforeRepeatedDigits[i],
              "repeated reset at zero changed a displayed digit");
    check(view.increment(2000.5), "counting was blocked during reset rotation");
    check(!view.rewinding(2000.5) && view.remainingResetValue(2000.5) == 0,
          "new input did not finish the digit rewind");
    for (int i = 0; i < 4; ++i)
        check(std::abs(view.displayPosition(i, 2000.68) - (i == 3 ? 1 : 0)) < 1e-9,
              "new tally did not become visible while the knob was still rotating");
    check(view.resetSpin.moving(2000.68), "rotation ended before the concurrent-input check");
    check(view.increment(2000.7), "second increment during reset rotation failed");
    settledAt(view, 2, originalFinish + 0.1);

    view.restore("9999", 2010.0);
    check(view.reset(2010.01), "second rotating reset failed");
    check(view.increment(2010.5), "increment before an interrupted reset failed");
    const double angleBefore = view.resetSpin.angle(2010.8);
    check(view.reset(2010.8), "reset of a new count during rotation failed");
    const double angleAfter = view.resetSpin.angle(2010.8);
    check(std::abs(std::remainder(angleAfter - angleBefore, revolution)) < 1e-9,
          "reset during rotation snapped the knob to another angle");
    check(view.resetSpin.angle(2010.9) > angleAfter,
          "retargeted reset did not continue clockwise");
    settledAt(view, 0, 2013.0);

    view.restore("9999", 2020.0);
    check(view.reset(2020.01), "reset before restore failed");
    check(view.resetSpin.moving(2020.2), "rotation was not active before restore");
    view.restore("2468", 2020.2);
    check(!view.resetSpin.moving(2020.2) && view.resetSpin.angle(2020.2) == 0,
          "restore left a stale reset rotation");
    check(!view.rewinding(2020.2) && view.remainingResetValue(2020.2) == 0,
          "restore left a stale digit rewind");
    settledAt(view, 2468, 2020.2);
    settledAt(view, 2468, 2023.0);
}

void rollbackDisplayProgress() {
    constexpr double now = 3000.0;
    for (int previousCount : {1, 1234, tally::maximum}) {
        tally::View view;
        view.restore(std::to_string(previousCount), now - 1);
        check(view.reset(now), "reset failed before the display-progress check");
        check(view.counter.value == 0, "visual rollback delayed the authoritative reset");
        check(view.remainingResetValue(now) == previousCount,
              "visual rollback lost its starting count");
        const double finish = view.resetSpin.started + view.resetSpin.seconds;
        double lastValue = previousCount;
        std::array<double, 4> lastPositions{};
        for (int i = 0; i < 4; ++i) lastPositions[i] = view.displayPosition(i, now);
        for (double fraction : {0.1, 0.25, 0.5, 0.75, 0.9}) {
            const double time = now + view.resetSpin.seconds * fraction;
            const double remaining = view.remainingResetValue(time);
            check(std::isfinite(remaining) && remaining >= 0 && remaining < lastValue,
                  "visual rollback failed to decrease continuously");
            check(view.rewinding(time) && view.resetSpin.moving(time),
                  "digits and knob did not remain active together");
            const double knobProgress = (view.resetSpin.angle(time) - view.resetSpin.from)
                                       / (view.resetSpin.to - view.resetSpin.from);
            check(std::abs(remaining / previousCount - (1 - knobProgress)) < 1e-9,
                  "digit rollback and knob rotation use different progress");
            for (int i = 0; i < 4; ++i) {
                const double position = view.displayPosition(i, time);
                check(std::isfinite(position) && position >= 0
                      && position <= lastPositions[i] + 1e-9,
                      "a rewinding digit advanced or wrapped forward");
                lastPositions[i] = position;
            }
            lastValue = remaining;
        }
        check(!view.rewinding(finish) && !view.resetSpin.moving(finish),
              "digits and knob did not stop at the same instant");
        check(std::abs(view.remainingResetValue(finish)) < 1e-9,
              "digits were not zero when the knob stopped");
        settledAt(view, 0, finish + 0.01);
    }
}

double timeForRemaining(const tally::View& view, double remaining) {
    double earlier = view.resetSpin.started;
    double later = earlier + view.resetSpin.seconds;
    for (int i = 0; i < 48; ++i) {
        const double middle = (earlier + later) / 2;
        if (view.remainingResetValue(middle) > remaining) earlier = middle;
        else later = middle;
    }
    return (earlier + later) / 2;
}

void rollbackBorrowsAndForwardInterruption() {
    struct Borrow {
        int start;
        double remaining;
        std::array<double, 4> positions;
    };
    const Borrow examples[] = {
        {10, 9.5, {0, 0, 0.5, 9.5}},
        {100, 99.5, {0, 0.5, 9.5, 99.5}},
        {1000, 999.5, {0.5, 9.5, 99.5, 999.5}},
        {1234, 1233.5, {1, 12, 123, 1233.5}},
    };
    for (const auto& example : examples) {
        tally::View view;
        view.restore(std::to_string(example.start), 4000.0);
        view.reset(4000.01);
        const double time = timeForRemaining(view, example.remaining);
        check(std::abs(view.remainingResetValue(time) - example.remaining) < 1e-6,
              "could not inspect the requested reverse-borrow transition");
        for (int i = 0; i < 4; ++i)
            check(std::abs(view.displayPosition(i, time) - example.positions[i]) < 1e-6,
                  "reverse borrowing moved the wrong digit or used the wrong direction");
    }

    tally::View view;
    view.restore("999", 4100.0);
    check(view.increment(4100.01), "forward carry failed before the reverse test");
    const double reversedAt = 4100.06;
    std::array<double, 4> before{};
    for (int i = 0; i < 4; ++i) before[i] = view.displayPosition(i, reversedAt);
    check(view.reset(reversedAt), "reset during the unfinished forward carry failed");
    check(view.remainingResetValue(reversedAt) > 999
          && view.remainingResetValue(reversedAt) < 1000,
          "reset during forward motion jumped ahead to an unseen count");
    for (int i = 0; i < 4; ++i)
        check(std::abs(std::remainder(view.displayPosition(i, reversedAt) - before[i], 10.0)) < 1e-7,
              "reset during a forward carry jumped a displayed wheel");
    check(view.remainingResetValue(reversedAt + 0.1) < view.remainingResetValue(reversedAt),
          "interrupted forward carry did not begin rolling backward");
    settledAt(view, 0, reversedAt + view.resetSpin.seconds + 0.01);
}

void quietEmbeddedClick() {
    const auto& pcm = tally::click_sound::pcm;
    const auto& wav = tally::click_sound::wav;
    check(tally::click_sound::sampleRate == 48000
          && pcm.size() * 1000 == tally::click_sound::sampleRate * 35,
          "embedded tally click is not 35 ms of 48 kHz audio");
    check(pcm.front() == 0 && pcm.back() == 0,
          "embedded tally click has a discontinuous endpoint");
    int peak = 0;
    for (auto sample : pcm) peak = std::max(peak, std::abs(static_cast<int>(sample)));
    const double level = peak / 32768.0;
    check(level > 0.01 && level < 0.1,
          "embedded tally click is silent, too loud, or clipped");
    check(wav.size() == 44 + pcm.size() * 2,
          "embedded WAV length does not match the PCM payload");
    check(std::memcmp(wav.data(), "RIFF", 4) == 0
          && std::memcmp(wav.data() + 8, "WAVEfmt ", 8) == 0
          && std::memcmp(wav.data() + 36, "data", 4) == 0,
          "embedded WAV has invalid chunk markers");
    const auto little16 = [&](std::size_t offset) {
        return std::uint32_t(wav[offset]) | (std::uint32_t(wav[offset + 1]) << 8);
    };
    const auto little32 = [&](std::size_t offset) {
        return little16(offset) | (little16(offset + 2) << 16);
    };
    check(little32(4) + 8 == wav.size() && little32(16) == 16
          && little32(40) == pcm.size() * 2,
          "embedded WAV chunk lengths are inconsistent");
    check(little16(20) == 1 && little16(22) == 1 && little16(34) == 16,
          "embedded WAV is not mono 16-bit PCM");
    check(little32(24) == tally::click_sound::sampleRate
          && little32(28) == tally::click_sound::sampleRate * 2
          && little16(32) == 2,
          "embedded WAV sample rate or frame size is inconsistent");
    bool matches = true;
    for (std::size_t i = 0; i < pcm.size(); ++i)
        if (little16(44 + i * 2) != static_cast<std::uint16_t>(pcm[i])) matches = false;
    check(matches, "Windows WAV and Linux PCM clicks do not contain the same audio");
}
} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"every valid persisted count", validPersistence},
        {"invalid persistence recovery", invalidPersistence},
        {"saturation and reset", saturationAndReset},
        {"four-digit carries", carries},
        {"rapid input without lost counts", rapidInput},
        {"reset interrupts animation safely", interruptedAnimation},
        {"restore synchronizes displayed digits", restoreSynchronizesDisplay},
        {"reset rotation duration and direction", resetRotationTiming},
        {"input remains correct during reset rotation", inputDuringResetRotation},
        {"digit rewind stays synchronized with the knob", rollbackDisplayProgress},
        {"reverse borrows and interruption of forward motion", rollbackBorrowsAndForwardInterruption},
        {"quiet embedded click PCM and WAV", quietEmbeddedClick}
    };
    unsigned failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << checks << " checks, " << failures << " failed test groups\n";
    return failures ? 1 : 0;
}
