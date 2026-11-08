// Copyright (c) 2026 davoda1
// SPDX-License-Identifier: Zlib
#include "reset_sound.hpp"
#include "ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
constexpr double tau = 6.283185307179586;

std::uint32_t read32(const std::vector<unsigned char>& bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at])
         | static_cast<std::uint32_t>(bytes[at+1]) << 8
         | static_cast<std::uint32_t>(bytes[at+2]) << 16
         | static_cast<std::uint32_t>(bytes[at+3]) << 24;
}

void invalidMotionIsBounded() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const std::vector<std::array<double,3>> invalid = {
        {nan,0,tau}, {infinity,0,tau}, {-infinity,0,tau},
        {0,0,tau}, {-.45,0,tau}, {.449,0,tau}, {2.201,0,3*tau}, {1e300,0,tau},
        {.45,nan,tau}, {.45,infinity,tau}, {.45,-.1,tau}, {.45,tau,2*tau},
        {.45,0,nan}, {.45,0,infinity}, {.45,0,-tau}, {.45,0,0}, {.45,0,.5*tau},
        {.45,0,3*tau+.001}, {.45,0,1e300}
    };
    for (const auto& state : invalid) {
        check(tally::reset_sound::render(state[0],state[1],state[2]).empty(),
              "invalid motion created audio");
        check(tally::reset_sound::contactTimes(state[0],state[1],state[2]).empty(),
              "invalid motion created contact events");
    }
    check(tally::reset_sound::makeWav({}).empty(), "empty PCM created a WAV");
    check(tally::reset_sound::makeWav(std::vector<std::int16_t>(
        tally::reset_sound::maximumSamples+1,0)).empty(), "oversized PCM created a WAV");
}

void nativeMotionAndAudioAgree() {
    const auto originalClick = tally::click_sound::pcm;
    for (int count : {0,1,9,25,100,399,400,1000,2500,4317,4318,9999}) {
        tally::ResetSpin spin;
        spin.start(count,10.);
        const auto events = tally::reset_sound::contactTimes(spin.seconds,spin.from,spin.to);
        const auto pcm = tally::reset_sound::render(spin.seconds,spin.from,spin.to);
        const auto repeated = tally::reset_sound::render(spin.seconds,spin.from,spin.to);
        check(!pcm.empty(), "valid native reset produced no audio");
        check(pcm == repeated, "reset PCM is not deterministic");
        check(pcm.size() <= tally::reset_sound::maximumSamples, "audio exceeded allocation bound");
        check(pcm.size() == static_cast<std::size_t>(std::ceil(
            (spin.seconds+tally::reset_sound::tailSeconds)*tally::reset_sound::sampleRate))+1,
            "audio length does not match reset plus soft stop tail");
        check(pcm.front()==0 && pcm.back()==0, "audio buffer endpoints are not zero");
        check(events.size() == static_cast<std::size_t>(std::llround(
            spin.to/tau*tally::reset_sound::teethPerTurn)), "wrong number of ratchet contacts");
        const auto stopSample = static_cast<std::size_t>(std::ceil(
            spin.seconds*tally::reset_sound::sampleRate));
        bool stopAudible = false, anyAudible = false;
        int peak = 0;
        for (std::size_t i=0; i<pcm.size(); ++i) {
            peak = std::max(peak,std::abs(static_cast<int>(pcm[i])));
            anyAudible = anyAudible || pcm[i]!=0;
            if (i>=stopSample) stopAudible = stopAudible || pcm[i]!=0;
        }
        check(anyAudible, "reset waveform is silent");
        check(stopAudible, "selected soft stop tick is missing");
        check(peak<=1331, "70 percent reset output exceeded its peak guard");
        for (std::size_t i=0; i<events.size(); ++i) {
            check(events[i]>0 && events[i]<spin.seconds, "tooth lies outside moving animation");
            check(i==0 || events[i]>events[i-1], "tooth times are not increasing");
            const double actualAngle = spin.angle(10.+events[i]);
            const double toothPhase = actualAngle/(tau/tally::reset_sound::teethPerTurn)-.5;
            check(std::abs(toothPhase-std::round(toothPhase))<1e-9,
                  "sound contact is not aligned with native knob angle");
        }
    }
    check(tally::click_sound::pcm==originalClick, "increment click PCM was changed");
}

void interruptedRotationKeepsPhase() {
    tally::ResetSpin spin;
    spin.start(9999,20.);
    const double restart=20.68;
    const double angleBefore=std::fmod(spin.angle(restart),tau);
    spin.start(37,restart);
    check(std::abs(spin.from-angleBefore)<1e-12, "native restart lost its current angle");
    const auto events=tally::reset_sound::contactTimes(spin.seconds,spin.from,spin.to);
    const auto pcm=tally::reset_sound::render(spin.seconds,spin.from,spin.to);
    check(!pcm.empty() && !events.empty(), "interrupted reset has no audio");
    double previousAngle=spin.from;
    for (double time : events) {
        const double angle=spin.angle(restart+time);
        const double phase=angle/(tau/tally::reset_sound::teethPerTurn)-.5;
        check(angle>previousAngle, "restarted ratchet travels backwards");
        check(std::abs(phase-std::round(phase))<1e-9, "restart tooth phase drifted");
        previousAngle=angle;
    }
    const auto tinySweep=tally::reset_sound::render(.45,tau-.001,tau);
    check(!tinySweep.empty(), "short remainder of a revolution was rejected");
    check(tally::reset_sound::contactTimes(.45,tau-.001,tau).empty(),
          "short remainder produced a tooth it never passed");
}

void wavEncodingIsPortable() {
    const std::vector<std::int16_t> examples={0,1,-1,32767,-32768,137,-214};
    const auto wav=tally::reset_sound::makeWav(examples);
    check(wav.size()==44+examples.size()*2, "WAV file size is wrong");
    check(std::memcmp(wav.data(),"RIFF",4)==0 && std::memcmp(wav.data()+8,"WAVE",4)==0,
          "WAV container signature is wrong");
    check(std::memcmp(wav.data()+12,"fmt ",4)==0 && std::memcmp(wav.data()+36,"data",4)==0,
          "WAV chunks are missing");
    check(read32(wav,4)==wav.size()-8 && read32(wav,40)==examples.size()*2,
          "WAV chunk lengths are wrong");
    check(read32(wav,16)==16 && read32(wav,24)==48000 && read32(wav,28)==96000,
          "WAV format/rate fields are wrong");
    check(wav[20]==1 && wav[21]==0 && wav[22]==1 && wav[23]==0
          && wav[32]==2 && wav[33]==0 && wav[34]==16 && wav[35]==0,
          "WAV is not mono signed 16-bit PCM");
    for (std::size_t i=0; i<examples.size(); ++i) {
        const auto sample=static_cast<std::uint16_t>(wav[44+2*i])
                         | static_cast<std::uint16_t>(wav[45+2*i])<<8;
        check(sample==static_cast<std::uint16_t>(examples[i]), "WAV PCM byte order is wrong");
    }
}
} // namespace

int main() {
    try {
        invalidMotionIsBounded();
        nativeMotionAndAudioAgree();
        interruptedRotationKeepsPhase();
        wavEncodingIsPortable();
        std::cout << "Reset sound tests passed (" << checks << " checks).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Reset sound test failed: " << error.what() << '\n';
        return 1;
    }
}
