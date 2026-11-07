// Copyright (c) 2026 davoda1
// SPDX-License-Identifier: Zlib
#pragma once
#include <string>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tally {
struct Color { double r,g,b,a=1; };
inline Color rgb(unsigned c, double a=1) { return {double((c>>16)&255)/255.0,double((c>>8)&255)/255.0,double(c&255)/255.0,a}; }
inline Color mix(Color a,Color b,double t) { return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,a.a+(b.a-a.a)*t}; }
struct Rect { double x,y,w,h; bool contains(double px,double py) const { return px>=x&&py>=y&&px<x+w&&py<y+h; } };
enum class Align { Left,Center,Right };
enum class PathVerb { Move,Line,Curve,Close };
// Absolute SVG path coordinates. Curves store both controls and the endpoint.
struct PathCommand {
 PathVerb verb;
 double x1=0,y1=0,x2=0,y2=0,x3=0,y3=0;
};
class Painter {
public:
 virtual ~Painter()=default;
 virtual void rect(Rect r,Color color)=0;
 virtual void rounded(Rect r,double radius,Color top,Color bottom,Color stroke={0,0,0,0},double strokeWidth=1)=0;
 virtual void ellipse(Rect r,Color top,Color bottom,Color stroke={0,0,0,0},double strokeWidth=1)=0;
 virtual void line(double x1,double y1,double x2,double y2,Color color,double width=1)=0;
 virtual void text(const std::string& s,Rect r,double size,Color color,bool bold=false,bool mono=false,Align align=Align::Center)=0;
 virtual void fillPath(const PathCommand* commands,std::size_t count,Color color)=0;
 virtual void clip(Rect r)=0;
 virtual void unclip()=0;
};
}
