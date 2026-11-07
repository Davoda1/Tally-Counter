// Copyright (c) 2026 davoda1
// SPDX-License-Identifier: Zlib
#pragma once
#include "paint.hpp"
#include "counter.hpp"
namespace tally {
struct Wheel {
    double from=0,to=0,started=-1;
    double position(double now) const;
    void set(int digit,double now,bool animated);
    bool moving(double now) const;
};
struct ResetSpin {
    double from=0,to=0,started=-1,seconds=0;
    double progress(double now) const;
    double angle(double now) const;
    void start(int previousCount,double now);
    bool moving(double now) const;
};
struct Layout {
    double scale=1,ox=0,oy=0;
    Rect reset{443,107,70,70};
    double x(double x) const { return ox+x*scale; }
    double y(double y) const { return oy+y*scale; }
    bool inReset(double x,double y) const {
        x=(x-ox)/scale; y=(y-oy)/scale;
        double dx=(x-478)/38,dy=(y-142)/38;
        return dx*dx+dy*dy<=1 || Rect{438,179,80,32}.contains(x,y);
    }
};
class View {
public:
    Counter counter;
    Wheel wheels[4];
    ResetSpin resetSpin;
    bool dark=false,hoverReset=false,pressedReset=false,focusReset=false,saveError=false;
    double pressedAt=-10;
    void restore(const std::string& text,double now);
    bool increment(double now);
    bool reset(double now);
    bool animating(double now) const;
    bool rewinding(double now) const;
    double remainingResetValue(double now) const;
    double displayPosition(int index,double now) const;
    Layout layout(double width,double height) const;
    void paint(Painter& painter,double width,double height,double now) const;
private:
    double rewindFrom=0;
    bool digitRewind=false;
    void update(double now,bool animated);
};
}
