// Copyright (c) 2026 davoda1
// SPDX-License-Identifier: Zlib
#pragma once
#include <string>
#include <cstdio>
#include <cctype>
namespace tally {
constexpr int maximum=9999;
inline int parseCount(const std::string& s) {
    std::size_t i=0; while(i<s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if(i==s.size()) return 0;
    int n=0; bool any=false;
    for(;i<s.size() && s[i]>='0' && s[i]<='9';++i) { any=true; n=n*10+(s[i]-'0'); if(n>maximum) return 0; }
    if(!any) return 0;
    while(i<s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return i==s.size()?n:0;
}
struct Counter {
    int value=0;
    bool increment() { if(value==maximum) return false; ++value; return true; }
    bool reset() { bool changed=value!=0; value=0; return changed; }
    void restore(const std::string& s) { value=parseCount(s); }
    std::string digits() const { char b[16]; std::snprintf(b,sizeof(b),"%04d",value); return b; }
};
}
