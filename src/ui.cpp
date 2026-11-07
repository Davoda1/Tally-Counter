// SPDX-License-Identifier: Zlib
// Copyright (c) 2026 davoda1.
#include "ui.hpp"
#include "reset_svg.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace tally {
namespace {
constexpr double duration=.16;
struct ScaledPainter : Painter {
    Painter& p; Layout t;
    ScaledPainter(Painter& p, Layout t):p(p),t(t){}
    Rect r(Rect a) const {return {t.x(a.x),t.y(a.y),a.w*t.scale,a.h*t.scale};}
    void rect(Rect a,Color c) override {p.rect(r(a),c);}
    void rounded(Rect a,double d,Color c,Color e,Color b,double w) override {p.rounded(r(a),d*t.scale,c,e,b,w*t.scale);}
    void ellipse(Rect a,Color c,Color e,Color b,double w) override {p.ellipse(r(a),c,e,b,w*t.scale);}
    void line(double a,double b,double c,double d,Color e,double w) override {p.line(t.x(a),t.y(b),t.x(c),t.y(d),e,w*t.scale);}
    void text(const std::string& a,Rect b,double c,Color d,bool e,bool f,Align g) override {p.text(a,r(b),c*t.scale,d,e,f,g);}
    void fillPath(const PathCommand* commands,std::size_t count,Color color) override {
        std::vector<PathCommand> transformed(commands,commands+count);
        for(auto& c:transformed) {
            if(c.verb==PathVerb::Close) continue;
            c.x1=t.x(c.x1);c.y1=t.y(c.y1);
            if(c.verb==PathVerb::Curve) {
                c.x2=t.x(c.x2);c.y2=t.y(c.y2);
                c.x3=t.x(c.x3);c.y3=t.y(c.y3);
            }
        }
        p.fillPath(transformed.data(),transformed.size(),color);
    }
    void clip(Rect a) override {p.clip(r(a));}
    void unclip() override {p.unclip();}
};
int digitAt(int value,int index) {
    constexpr int divisors[]={1000,100,10,1};
    return value/divisors[index]%10;
}
void resetSymbol(Painter& p,double cx,double cy,double size,double angle,Color color) {
    auto path=reset_svg::commands;
    const double cosine=std::cos(angle),sine=std::sin(angle);
    auto transform=[&](double& x,double& y) {
        const double px=(x/reset_svg::width-.5)*size;
        const double py=(y/reset_svg::height-.5)*size;
        x=cx+px*cosine-py*sine;
        y=cy+px*sine+py*cosine;
    };
    for(auto& c:path) {
        if(c.verb==PathVerb::Close) continue;
        transform(c.x1,c.y1);
        if(c.verb==PathVerb::Curve) {transform(c.x2,c.y2);transform(c.x3,c.y3);}
    }
    p.fillPath(path.data(),path.size(),color);
}
}

double Wheel::position(double now) const {
    if(started<0 || now>=started+duration) return to;
    double t=std::clamp((now-started)/duration,0.0,1.0);
    double easing=1-std::pow(1-t,3);
    return from+(to-from)*easing;
}
bool Wheel::moving(double now) const {return from!=to && started>=0 && now<started+duration;}
void Wheel::set(int digit,double now,bool animated) {
    if(!animated) {from=to=digit;started=-1;return;}
    // Unchanged higher wheels continue their existing roll.
    if(static_cast<int>(std::lround(to))%10==digit) return;
    double current=position(now);
    double target=std::floor(to/10)*10+digit;
    if(target<=to) target+=10;
    // Input remains exact; a burst does not leave a queue of visual rolls.
    if(target-current>2) current=target-1;
    from=current;to=target;started=now;
}

double ResetSpin::progress(double now) const {
    if(started<0 || seconds<=0) return 0;
    const double t=std::clamp((now-started)/seconds,0.,1.);
    // Smooth acceleration and deceleration, ending on a complete revolution.
    return t*t*(3-2*t);
}
double ResetSpin::angle(double now) const {
    return from+(to-from)*progress(now);
}
bool ResetSpin::moving(double now) const {
    return started>=0 && seconds>0 && now<started+seconds;
}
void ResetSpin::start(int previousCount,double now) {
    constexpr double tau=6.283185307179586;
    from=std::fmod(angle(now),tau);
    if(from<0) from+=tau;
    const double fraction=std::clamp(previousCount,0,maximum)/double(maximum);
    seconds=.45+1.75*std::sqrt(fraction);
    to=std::ceil(seconds*1.25)*tau;
    started=now;
}

void View::update(double now,bool animated) {for(int i=0;i<4;++i) wheels[i].set(digitAt(counter.value,i),now,animated);}
void View::restore(const std::string& text,double now) {
    counter.restore(text);update(now,false);resetSpin={};
    digitRewind=false;rewindFrom=0;
}
bool View::increment(double now) {
    bool changed=counter.increment();
    if(changed) {
        // New input finishes the visual rewind and immediately shows the new tally.
        // The authoritative count has already been zero since Reset was pressed.
        digitRewind=false;rewindFrom=0;
        update(now,true);pressedAt=now;
    }
    return changed;
}
bool View::reset(double now) {
    const int previousCount=counter.value;
    bool changed=counter.reset();pressedAt=-10;
    // Repeated clicks at zero do not queue spins or prolong an active reset.
    if(changed || !resetSpin.moving(now)) {
        // Include an unfinished forward step so reversing does not jump ahead.
        const double unfinished=std::clamp(wheels[3].to-wheels[3].position(now),0.,double(previousCount));
        rewindFrom=std::max(0.,previousCount-unfinished);
        digitRewind=rewindFrom>0;
        resetSpin.start(previousCount,now);
    }
    update(now,false);
    return changed;
}
bool View::rewinding(double now) const {return digitRewind && resetSpin.moving(now);}
double View::remainingResetValue(double now) const {
    return digitRewind?rewindFrom*(1-resetSpin.progress(now)):0;
}
double View::displayPosition(int index,double now) const {
    if(index<0 || index>3) return 0;
    if(!digitRewind) return wheels[index].position(now);
    const double value=remainingResetValue(now);
    const int whole=static_cast<int>(std::floor(value));
    const double fraction=value-whole;
    constexpr int divisors[]={1000,100,10,1};
    const int place=divisors[index];
    // Higher drums turn only during a borrow, exactly as an odometer does.
    return whole/place+(whole%place==place-1?fraction:0);
}
bool View::animating(double now) const {
    for(auto& w:wheels) if(w.moving(now)) return true;
    return now<pressedAt+.16 || resetSpin.moving(now);
}
Layout View::layout(double width,double height) const {
    Layout l;l.scale=std::min(width/560.,height/280.);
    l.ox=(width-560*l.scale)*.5;l.oy=(height-280*l.scale)*.5;return l;
}

void View::paint(Painter& painter,double width,double height,double now) const {
    const Color bg=rgb(dark?0x191c20:0xeff1f2);
    painter.rect({0,0,width,height},bg);
    ScaledPainter scaled(painter,layout(width,height)); Painter& p=scaled;
    const Color ink=rgb(dark?0xd4d8dd:0x50565d);
    const Color muted=rgb(dark?0x969da6:0x6d747b);
    p.text("T A L L Y",{37,23,150,20},12,ink,true,false,Align::Left);
    p.text("MAX 9999",{365,23,154,20},11,muted,false,true,Align::Right);

    // A restrained metal housing and a separate knurled reset wheel.
    p.rounded({27,71,427,151},28,rgb(0x000000,.025),rgb(0x000000,.025));
    p.rounded({28,68,425,150},27,rgb(0x000000,.05),rgb(0x000000,.05));
    p.rounded({29,65,423,151},26,rgb(dark?0x59616a:0xffffff),rgb(dark?0x292e34:0xc0c6cb),rgb(dark?0x626971:0xb7bec4),1);
    p.rounded({32,68,417,145},23,rgb(dark?0x434a52:0xf1f4f5),rgb(dark?0x333941:0xd2d7da),rgb(dark?0x737a83:0xffffff,.42),1);
    p.line(49,71,430,71,rgb(0xffffff,dark?.11:.8));
    p.line(49,210,430,210,rgb(0x000000,dark?.25:.13));

    // The black bezel contains four independent cylindrical reels.
    p.rounded({49,86,382,111},12,rgb(dark?0x0d1115:0x6b747c),rgb(dark?0x1b2026:0x879098),rgb(dark?0x13171c:0x919aa1),1);
    p.rounded({52,89,376,105},10,rgb(0x080b0f),rgb(0x252a30),rgb(0x050709),1);
    for(int i=0;i<4;++i) {
        double x=62+i*90.5;
        Rect cell{x,97,84,89};
        p.rounded(cell,5,rgb(0x1b2026),rgb(0x20262c),rgb(0x090c10),1);
        // Curved lighting on the drums, kept clear at the digit baseline.
        p.rounded({x+1,98,82,43},4,rgb(0x171b20),rgb(0x383e44));
        p.rounded({x+1,141,82,44},4,rgb(0x383e44),rgb(0x12161b));
        p.clip({x+2,99,80,85});
        double pos=displayPosition(i,now);
        int center=static_cast<int>(std::floor(pos));
        for(int k=-1;k<=2;++k) {
            int n=center+k;int digit=(n%10+10)%10;
            double dy=(n-pos)*83;
            if(std::abs(dy)>115) continue;
            double alpha=1-.5*std::min(std::abs(dy)/83.,1.);
            p.text(std::to_string(digit),{x+1,98+dy,82,88},70,rgb(0xf5f6f3,alpha),true,true,Align::Center);
        }
        // Glass shading with a fine mechanical seam.
        p.rounded({x+1,98,82,19},0,rgb(0x000000,.38),rgb(0x000000,0));
        p.rounded({x+1,167,82,19},0,rgb(0x000000,0),rgb(0x000000,.42));
        p.line(x+2,141.5,x+82,141.5,rgb(0x000000,.28),1);
        p.line(x+2,142.5,x+82,142.5,rgb(0xffffff,.045),1);
        p.unclip();
        p.line(x+5,99,x+79,99,rgb(0xffffff,.07),1);
    }

    double pulse=std::clamp(1-(now-pressedAt)/.16,0.,1.);
    if(pulse>0) p.rounded({52,89,376,105},10,rgb(0xffffff,.022*pulse),rgb(0xffffff,.022*pulse),rgb(0xffffff,.08*pulse),1);

    // Reset has one purpose and never propagates a click to the counter.
    const double down=pressedReset?1.5:0;
    const double rotation=resetSpin.angle(now);
    p.rounded({448,123,15,39},3,rgb(dark?0x101419:0x828b93),rgb(dark?0x232930:0xb3bac0),rgb(dark?0x15191e:0x8b949c),1);
    p.ellipse({444,111+down,69,69},rgb(0x000000,.09),rgb(0x000000,.09));
    p.ellipse({443,107+down,70,70},rgb(dark?0x68727c:0xfafbfc),rgb(dark?0x252d35:0x9da6ae),rgb(dark?0x171d23:0x89939c),1);
    for(int n=0;n<40;++n) {
        double angle=n*6.283185307179586/40+rotation;
        p.line(478+std::cos(angle)*31,142+down+std::sin(angle)*31,
               478+std::cos(angle)*33,142+down+std::sin(angle)*33,
               rgb(dark?0x12191f:0x727d87,.46),1);
    }
    Color face1=rgb(dark?0x565f69:0xf0f3f5), face2=rgb(dark?0x38414a:0xc7cfd6);
    if(hoverReset) {face1=mix(face1,rgb(0xffffff),.10);face2=mix(face2,rgb(0xffffff),.08);}
    if(pressedReset) {face1=mix(face1,rgb(0x000000),.12);face2=mix(face2,rgb(0x000000),.10);}
    p.ellipse({449,113+down,58,58},face1,face2,rgb(0xffffff,dark?.15:.65),1);
    // The supplied SVG stays a vector at every scale and turns with the knob.
    Color arrow=rgb(dark?0xe1e6eb:0x404c57);
    resetSymbol(p,478,142+down,24,rotation,arrow);
    if(focusReset) p.ellipse({439,103+down,78,78},rgb(0x000000,0),rgb(0x000000,0),rgb(dark?0xb9cadb:0x576d82),1.5);
    p.text("RESET",{442,188,73,18},10,muted,true,false);

    std::string hint=counter.value==maximum?"Limit reached. Reset to start again.":"Click anywhere or press Space";
    if(saveError) hint="Count could not be saved";
    p.text(hint,{28,240,504,20},12,muted,false,false);
}
}
