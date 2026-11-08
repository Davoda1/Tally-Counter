// SPDX-License-Identifier: Zlib
// Copyright (c) 2026 davoda1.
// Native Windows frontend. All drawing and state remain local to this process.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ui.hpp"
#include "click_sound.hpp"
#include "reset_sound.hpp"

namespace {

constexpr wchar_t windowClass[] = L"TallyCounterWindow";
constexpr wchar_t windowTitle[] = L"Tally Counter";
constexpr UINT_PTR animationTimer = 1;

double nowSeconds() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value;
    }();
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return frequency.QuadPart ? double(value.QuadPart) / double(frequency.QuadPart)
                             : double(GetTickCount64()) / 1000.0;
}

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                       int(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(std::size_t(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()),
                        result.data(), size);
    return result;
}

Gdiplus::Color color(tally::Color c) {
    const auto channel = [](double v) {
        return BYTE(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
    };
    return {channel(c.a), channel(c.r), channel(c.g), channel(c.b)};
}

Gdiplus::RectF box(tally::Rect r) {
    return {Gdiplus::REAL(r.x), Gdiplus::REAL(r.y), Gdiplus::REAL(r.w), Gdiplus::REAL(r.h)};
}

void roundedPath(Gdiplus::GraphicsPath& path, tally::Rect r, double radius) {
    const auto b = box(r);
    const Gdiplus::REAL d = Gdiplus::REAL(std::clamp(radius, 0.0, std::min(r.w, r.h) * .5) * 2.0);
    if (d <= 0) {
        path.AddRectangle(b);
        return;
    }
    path.AddArc(b.X, b.Y, d, d, 180, 90);
    path.AddArc(b.GetRight() - d, b.Y, d, d, 270, 90);
    path.AddArc(b.GetRight() - d, b.GetBottom() - d, d, d, 0, 90);
    path.AddArc(b.X, b.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}

class WindowsPainter final : public tally::Painter {
    Gdiplus::Graphics& graphics;
    std::vector<Gdiplus::GraphicsState> clips;

public:
    explicit WindowsPainter(Gdiplus::Graphics& graphics) : graphics(graphics) {}

    void rect(tally::Rect r, tally::Color c) override {
        if (r.w <= 0 || r.h <= 0) return;
        Gdiplus::SolidBrush brush(color(c));
        graphics.FillRectangle(&brush, box(r));
    }

    void rounded(tally::Rect r, double radius, tally::Color top, tally::Color bottom,
                 tally::Color stroke, double strokeWidth) override {
        if (r.w <= 0 || r.h <= 0) return;
        Gdiplus::GraphicsPath path;
        roundedPath(path, r, radius);
        Gdiplus::LinearGradientBrush brush(box(r), color(top), color(bottom),
                                          Gdiplus::LinearGradientModeVertical);
        brush.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        graphics.FillPath(&brush, &path);
        if (stroke.a > 0 && strokeWidth > 0) {
            Gdiplus::Pen pen(color(stroke), Gdiplus::REAL(strokeWidth));
            graphics.DrawPath(&pen, &path);
        }
    }

    void ellipse(tally::Rect r, tally::Color top, tally::Color bottom,
                 tally::Color stroke, double strokeWidth) override {
        if (r.w <= 0 || r.h <= 0) return;
        Gdiplus::LinearGradientBrush brush(box(r), color(top), color(bottom),
                                          Gdiplus::LinearGradientModeVertical);
        brush.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        graphics.FillEllipse(&brush, box(r));
        if (stroke.a > 0 && strokeWidth > 0) {
            Gdiplus::Pen pen(color(stroke), Gdiplus::REAL(strokeWidth));
            graphics.DrawEllipse(&pen, box(r));
        }
    }

    void line(double x1, double y1, double x2, double y2, tally::Color c,
              double width) override {
        Gdiplus::Pen pen(color(c), Gdiplus::REAL(width));
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawLine(&pen, Gdiplus::REAL(x1), Gdiplus::REAL(y1),
                           Gdiplus::REAL(x2), Gdiplus::REAL(y2));
    }

    void text(const std::string& text, tally::Rect r, double size, tally::Color c,
              bool bold, bool mono, tally::Align align) override {
        if (text.empty() || size <= 0) return;
        const std::wstring string = wide(text);
        Gdiplus::FontFamily requested(mono ? L"Consolas" : L"Segoe UI");
        const auto* family = requested.GetLastStatus() == Gdiplus::Ok ? &requested
                          : mono ? Gdiplus::FontFamily::GenericMonospace()
                                 : Gdiplus::FontFamily::GenericSansSerif();
        Gdiplus::Font font(family, Gdiplus::REAL(size),
                            bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                            Gdiplus::UnitPixel);
        Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
        format.SetFormatFlags(format.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap
                                                     | Gdiplus::StringFormatFlagsNoClip);
        format.SetAlignment(align == tally::Align::Left ? Gdiplus::StringAlignmentNear
                          : align == tally::Align::Right ? Gdiplus::StringAlignmentFar
                                                        : Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush brush(color(c));
        graphics.DrawString(string.c_str(), int(string.size()), &font, box(r), &format, &brush);
    }

    void fillPath(const tally::PathCommand* commands, std::size_t count,
                  tally::Color c) override {
        if (!commands || count == 0) return;
        Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
        Gdiplus::PointF current{}, start{};
        bool begun = false;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& command = commands[i];
            const Gdiplus::PointF first{Gdiplus::REAL(command.x1), Gdiplus::REAL(command.y1)};
            switch (command.verb) {
            case tally::PathVerb::Move:
                path.StartFigure();
                current = start = first;
                begun = true;
                break;
            case tally::PathVerb::Line:
                if (begun) {
                    path.AddLine(current, first);
                    current = first;
                }
                break;
            case tally::PathVerb::Curve:
                if (begun) {
                    const Gdiplus::PointF second{Gdiplus::REAL(command.x2), Gdiplus::REAL(command.y2)};
                    const Gdiplus::PointF end{Gdiplus::REAL(command.x3), Gdiplus::REAL(command.y3)};
                    path.AddBezier(current, first, second, end);
                    current = end;
                }
                break;
            case tally::PathVerb::Close:
                if (begun) {
                    path.CloseFigure();
                    current = start;
                }
                break;
            }
        }
        Gdiplus::SolidBrush brush(color(c));
        graphics.FillPath(&brush, &path);
    }

    void clip(tally::Rect r) override {
        clips.push_back(graphics.Save());
        graphics.SetClip(box(r), Gdiplus::CombineModeIntersect);
    }

    void unclip() override {
        if (clips.empty()) return;
        graphics.Restore(clips.back());
        clips.pop_back();
    }
};

struct Application {
    tally::View view;
    HWND window = nullptr;
    UINT dpi = 96;
    bool timerRunning = false;
    bool mouseDown = false;
    bool pressWasReset = false;
    bool trackingMouse = false;
    std::wstring stateDirectory;
    std::wstring statePath;
    std::wstring geometryPath;
    // PlaySound uses this memory asynchronously. Keep it alive until playback
    // is explicitly stopped before the next reset buffer replaces it.
    std::vector<unsigned char> resetWave;
} app;

template<typename Function>
Function userFunction(const char* name) {
    const FARPROC address = GetProcAddress(GetModuleHandleW(L"user32.dll"), name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address), "Unexpected Windows function-pointer size");
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

UINT systemDpi() {
    using Function = UINT (WINAPI*)();
    const auto function = userFunction<Function>("GetDpiForSystem");
    return function ? function() : 96;
}

UINT dpiForWindow(HWND window) {
    using Function = UINT (WINAPI*)(HWND);
    const auto function = userFunction<Function>("GetDpiForWindow");
    return function ? std::max(function(window), UINT(96)) : systemDpi();
}

void adjustWindowRectangle(RECT& r, DWORD style, DWORD exStyle, UINT dpi) {
    using Function = BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    const auto function = userFunction<Function>("AdjustWindowRectExForDpi");
    if (function) function(&r, style, FALSE, exStyle, dpi);
    else AdjustWindowRectEx(&r, style, FALSE, exStyle);
}

bool ensureStateDirectory() {
    if (app.stateDirectory.empty()) return false;
    if (CreateDirectoryW(app.stateDirectory.c_str(), nullptr)) return true;
    const DWORD error = GetLastError();
    const DWORD attributes = GetFileAttributesW(app.stateDirectory.c_str());
    return error == ERROR_ALREADY_EXISTS && attributes != INVALID_FILE_ATTRIBUTES
           && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::string readStateFile(const std::wstring& path, DWORD maximum) {
    if (path.empty()) return {};
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string data;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= maximum) {
        data.resize(std::size_t(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, data.data(), DWORD(data.size()), &read, nullptr)
            || read != data.size()) data.clear();
    }
    CloseHandle(file);
    return data;
}

void loadState() {
    PWSTR roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &roaming))) {
        app.stateDirectory = std::wstring(roaming) + L"\\TallyCounter";
        app.statePath = app.stateDirectory + L"\\count.txt";
        app.geometryPath = app.stateDirectory + L"\\window.ini";
        CoTaskMemFree(roaming);
    }
    app.view.restore(readStateFile(app.statePath, 64), nowSeconds());
}

bool writeStateFile(const std::wstring& path, const std::string& data) {
    if (path.empty() || !ensureStateDirectory()) return false;
    const std::wstring temporary = path + L".tmp." + std::to_wstring(GetCurrentProcessId());
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, data.data(), DWORD(data.size()), &written, nullptr)
              && written == data.size();
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    if (!CloseHandle(file)) ok = false;
    if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}

bool saveState() {
    return writeStateFile(app.statePath, std::to_string(app.view.counter.value) + "\n");
}

struct WindowGeometry {
    RECT normal{};
    UINT dpi = 96;
    bool maximized = false;
    bool valid = false;
};

WindowGeometry loadWindowGeometry() {
    const std::string data = readStateFile(app.geometryPath, 512);
    constexpr std::array<std::string_view, 7> keys{
        "version", "x", "y", "width", "height", "dpi", "maximized"
    };
    std::array<int, keys.size()> values{};
    unsigned seen = 0;
    for (std::size_t offset = 0; offset < data.size();) {
        const std::size_t end = data.find('\n', offset);
        std::string_view line(data.data() + offset,
                               (end == std::string::npos ? data.size() : end) - offset);
        offset = end == std::string::npos ? data.size() : end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        const auto separator = line.find('=');
        if (separator == std::string_view::npos) return {};
        const auto key = std::find(keys.begin(), keys.end(), line.substr(0, separator));
        if (key == keys.end()) return {};
        const std::size_t index = std::size_t(key - keys.begin());
        const unsigned flag = 1u << index;
        if (seen & flag) return {};
        const char* first = line.data() + separator + 1;
        const char* last = line.data() + line.size();
        const auto parsed = std::from_chars(first, last, values[index]);
        if (parsed.ec != std::errc{} || parsed.ptr != last) return {};
        seen |= flag;
    }
    if (seen != (1u << keys.size()) - 1 || values[0] != 1
        || values[1] < -131072 || values[1] > 131072
        || values[2] < -131072 || values[2] > 131072
        || values[3] < 100 || values[3] > 32768
        || values[4] < 100 || values[4] > 32768
        || values[5] < 96 || values[5] > 768
        || values[6] < 0 || values[6] > 1) return {};
    return {{values[1], values[2], values[1] + values[3], values[2] + values[4]},
            UINT(values[5]), values[6] != 0, true};
}

bool restoredWindowBounds(const WindowGeometry& saved, UINT dpi, RECT& bounds) {
    if (!saved.valid) return false;
    const HMONITOR monitor = MonitorFromRect(&saved.normal, MONITOR_DEFAULTTONEAREST);
    MONITORINFO information{};
    information.cbSize = sizeof(information);
    if (!monitor || !GetMonitorInfoW(monitor, &information)) return false;
    const RECT& work = information.rcWork;
    const LONG availableWidth = work.right - work.left;
    const LONG availableHeight = work.bottom - work.top;
    if (availableWidth <= 0 || availableHeight <= 0) return false;
    RECT minimum{0, 0, MulDiv(400, int(dpi), 96), MulDiv(220, int(dpi), 96)};
    adjustWindowRectangle(minimum, WS_OVERLAPPEDWINDOW, 0, dpi);
    // Keep the logical size across scale changes and fit the current monitor.
    // On an unusually small work area, visibility takes precedence over minimum size.
    const LONG width = std::clamp(LONG(MulDiv(saved.normal.right - saved.normal.left,
                                              int(dpi), int(saved.dpi))),
                                  std::min(minimum.right - minimum.left, availableWidth),
                                  availableWidth);
    const LONG height = std::clamp(LONG(MulDiv(saved.normal.bottom - saved.normal.top,
                                               int(dpi), int(saved.dpi))),
                                   std::min(minimum.bottom - minimum.top, availableHeight),
                                   availableHeight);
    const LONG x = std::clamp(saved.normal.left, work.left, work.right - width);
    const LONG y = std::clamp(saved.normal.top, work.top, work.bottom - height);
    bounds = {x, y, x + width, y + height};
    return true;
}

bool saveWindowGeometry() {
    if (!app.window) return false;
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (!GetWindowPlacement(app.window, &placement)) return false;
    RECT normal = placement.rcNormalPosition;
    if (!IsIconic(app.window) && !IsZoomed(app.window)) {
        if (!GetWindowRect(app.window, &normal)) return false;
    } else {
        // WINDOWPLACEMENT uses workspace coordinates; store screen coordinates
        // so a taskbar on the top or left cannot shift the window on each launch.
        MONITORINFO information{};
        information.cbSize = sizeof(information);
        if (!GetMonitorInfoW(MonitorFromWindow(app.window, MONITOR_DEFAULTTONEAREST),
                             &information)) return false;
        OffsetRect(&normal, information.rcWork.left - information.rcMonitor.left,
                            information.rcWork.top - information.rcMonitor.top);
    }
    const LONG width = normal.right - normal.left;
    const LONG height = normal.bottom - normal.top;
    if (width <= 0 || height <= 0) return false;
    const bool maximized = IsZoomed(app.window)
        || (IsIconic(app.window) && (placement.flags & WPF_RESTORETOMAXIMIZED));
    const std::string data = "version=1\nx=" + std::to_string(normal.left)
        + "\ny=" + std::to_string(normal.top)
        + "\nwidth=" + std::to_string(width) + "\nheight=" + std::to_string(height)
        + "\ndpi=" + std::to_string(dpiForWindow(app.window))
        + "\nmaximized=" + (maximized ? "1\n" : "0\n");
    return writeStateFile(app.geometryPath, data);
}

void refresh() {
    InvalidateRect(app.window, nullptr, FALSE);
    if (app.view.animating(nowSeconds()) && !app.timerRunning) {
        app.timerRunning = SetTimer(app.window, animationTimer, 16, nullptr) != 0;
    }
}

void action(bool reset) {
    const double now = nowSeconds();
    const bool startsReset = reset && (app.view.counter.value != 0
                                       || !app.view.resetSpin.moving(now));
    const bool changed = reset ? app.view.reset(now) : app.view.increment(now);
    if (changed && !reset) {
        // PlaySound replaces an earlier click, avoiding queued or stacked voices.
        // The embedded WAV has static lifetime for asynchronous playback.
        PlaySoundW(reinterpret_cast<LPCWSTR>(tally::click_sound::wav.data()), nullptr,
                    SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
    if (startsReset) {
        try {
            const auto& spin = app.view.resetSpin;
            auto wave = tally::reset_sound::makeWav(
                tally::reset_sound::render(spin.seconds, spin.from, spin.to));
            if (!wave.empty()) {
                // Stop before releasing any buffer the audio API may still
                // reference. A new tally uses the unchanged click above and
                // replaces this reset sound immediately, without a queue.
                PlaySoundW(nullptr, nullptr, 0);
                app.resetWave = std::move(wave);
                // Synthesis must not let the visual rewind get ahead of audio.
                app.view.resetSpin.started = nowSeconds();
                PlaySoundW(reinterpret_cast<LPCWSTR>(app.resetWave.data()), nullptr,
                            SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
            }
        } catch (...) {
            // Audio is optional; allocation/device failures must not affect
            // the reset, its saved value, or the UI.
        }
    }
    // A reset always persists zero, including recovery from a corrupt saved file.
    if (changed || reset || app.view.saveError) app.view.saveError = !saveState();
    refresh();
}

void updateTheme() {
    DWORD light = 1, bytes = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,
                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                  L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
    app.view.dark = light == 0;
    if (app.window) {
        const BOOL dark = app.view.dark;
        if (FAILED(DwmSetWindowAttribute(app.window, 20, &dark, sizeof(dark))))
            DwmSetWindowAttribute(app.window, 19, &dark, sizeof(dark));
        refresh();
    }
}

tally::Layout layout() {
    RECT r{};
    GetClientRect(app.window, &r);
    const double scale = double(app.dpi) / 96.0;
    return app.view.layout((r.right - r.left) / scale, (r.bottom - r.top) / scale);
}

bool insideClient(int x, int y) {
    RECT r{};
    GetClientRect(app.window, &r);
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

bool overReset(int x, int y) {
    const double scale = double(app.dpi) / 96.0;
    return layout().inReset(x / scale, y / scale);
}

bool modifiersHeld() {
    return (GetKeyState(VK_SHIFT) & 0x8000) || (GetKeyState(VK_CONTROL) & 0x8000)
        || (GetKeyState(VK_MENU) & 0x8000) || (GetKeyState(VK_LWIN) & 0x8000)
        || (GetKeyState(VK_RWIN) & 0x8000);
}

void cancelMousePress() {
    app.mouseDown = false;
    app.pressWasReset = false;
    app.view.pressedReset = false;
}

void paintWindow(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT r{};
    GetClientRect(window, &r);
    const int width = r.right - r.left, height = r.bottom - r.top;
    if (width > 0 && height > 0) {
        HDC memory = CreateCompatibleDC(dc);
        HBITMAP bitmap = memory ? CreateCompatibleBitmap(dc, width, height) : nullptr;
        HGDIOBJ original = bitmap ? SelectObject(memory, bitmap) : nullptr;
        HDC target = bitmap ? memory : dc;
        {
            Gdiplus::Graphics graphics(target);
            graphics.SetPageUnit(Gdiplus::UnitPixel);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            const double scale = double(app.dpi) / 96.0;
            graphics.ScaleTransform(Gdiplus::REAL(scale), Gdiplus::REAL(scale));
            WindowsPainter painter(graphics);
            app.view.paint(painter, width / scale, height / scale, nowSeconds());
            graphics.Flush(Gdiplus::FlushIntentionSync);
        }
        if (bitmap) {
            BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
            SelectObject(memory, original);
            DeleteObject(bitmap);
        }
        if (memory) DeleteDC(memory);
    }
    EndPaint(window, &paint);
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        app.window = window;
        app.dpi = dpiForWindow(window);
        updateTheme();
        return 0;
    case WM_PAINT:
        paintWindow(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        refresh();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = app.dpi ? app.dpi : 96;
        RECT minimum{0, 0, MulDiv(400, int(dpi), 96), MulDiv(220, int(dpi), 96)};
        adjustWindowRectangle(minimum, WS_OVERLAPPEDWINDOW, 0, dpi);
        info->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
        return 0;
    }
    case WM_DPICHANGED: {
        app.dpi = HIWORD(wParam);
        const auto* r = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                      SWP_NOZORDER | SWP_NOACTIVATE);
        refresh();
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        updateTheme();
        return 0;
    case WM_TIMER:
        if (wParam == animationTimer) {
            InvalidateRect(window, nullptr, FALSE);
            if (!app.view.animating(nowSeconds())) {
                KillTimer(window, animationTimer);
                app.timerRunning = false;
            }
        }
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(window);
        app.mouseDown = true;
        app.pressWasReset = overReset(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        app.view.pressedReset = app.pressWasReset;
        app.view.focusReset = false;
        SetCapture(window);
        refresh();
        return 0;
    }
    case WM_LBUTTONUP: {
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        const bool wasDown = app.mouseDown;
        const bool beganAtReset = app.pressWasReset;
        const bool endedAtReset = overReset(x, y);
        cancelMousePress();
        if (GetCapture() == window) ReleaseCapture();
        if (wasDown && insideClient(x, y)) {
            if (beganAtReset && endedAtReset) action(true);
            else if (!beganAtReset && !endedAtReset) action(false);
        }
        refresh();
        return 0;
    }
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        cancelMousePress();
        refresh();
        return 0;
    case WM_MOUSEMOVE: {
        if (!app.trackingMouse) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            app.trackingMouse = TrackMouseEvent(&tracking) != FALSE;
        }
        const bool hover = overReset(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        const bool pressed = app.mouseDown && app.pressWasReset && hover;
        if (hover != app.view.hoverReset || pressed != app.view.pressedReset) {
            app.view.hoverReset = hover;
            app.view.pressedReset = pressed;
            refresh();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        app.trackingMouse = false;
        app.view.hoverReset = false;
        app.view.pressedReset = false;
        refresh();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_SPACE && !modifiersHeld()) {
            if (!(lParam & (LPARAM(1) << 30))) action(false);
            return 0;
        }
        if (wParam == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            if (!(lParam & (LPARAM(1) << 30))) {
                app.view.focusReset = !app.view.focusReset;
                refresh();
            }
            return 0;
        }
        if (wParam == VK_RETURN && !modifiersHeld()) {
            if (app.view.focusReset && !(lParam & (LPARAM(1) << 30))) action(true);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            app.view.focusReset = false;
            refresh();
            return 0;
        }
        break;
    case WM_CHAR:
        // This window has no text-entry control. Suppress default key beeps.
        return 0;
    case WM_KILLFOCUS:
        app.view.focusReset = false;
        cancelMousePress();
        if (GetCapture() == window) ReleaseCapture();
        refresh();
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    case WM_CLOSE:
        saveState();
        saveWindowGeometry();
        DestroyWindow(window);
        return 0;
    case WM_QUERYENDSESSION:
        saveState();
        saveWindowGeometry();
        return TRUE;
    case WM_ENDSESSION:
        if (wParam) PlaySoundW(nullptr, nullptr, 0);
        return 0;
    case WM_DESTROY:
        KillTimer(window, animationTimer);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void enableDpiAwareness() {
    using Function = BOOL (WINAPI*)(HANDLE);
    const auto function = userFunction<Function>("SetProcessDpiAwarenessContext");
    if (function) function(reinterpret_cast<HANDLE>(-4)); // Per-monitor v2.
    else SetProcessDPIAware();
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    enableDpiAwareness();
    HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\TallyCounter.SingleInstance.1");
    if (!instanceMutex) {
        MessageBoxW(nullptr, L"Tally Counter could not start. Please try opening it again.",
                     windowTitle, MB_OK | MB_ICONERROR);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        for (int attempt = 0; attempt < 10; ++attempt) {
            HWND previous = FindWindowW(windowClass, nullptr);
            if (previous) {
                if (IsIconic(previous)) ShowWindow(previous, SW_RESTORE);
                SetForegroundWindow(previous);
                break;
            }
            Sleep(50);
        }
        CloseHandle(instanceMutex);
        return 0;
    }

    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR graphicsToken = 0;
    if (Gdiplus::GdiplusStartup(&graphicsToken, &startup, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"Tally Counter could not initialize Windows graphics.",
                     windowTitle, MB_OK | MB_ICONERROR);
        CloseHandle(instanceMutex);
        return 1;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    app.dpi = systemDpi();
    loadState();
    const WindowGeometry savedGeometry = loadWindowGeometry();
    updateTheme();

    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    // Without CS_DBLCLKS, a double-click is exactly two ordinary click pairs.
    type.style = CS_HREDRAW | CS_VREDRAW;
    type.lpfnWndProc = windowProcedure;
    type.hInstance = instance;
    type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    type.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                16, 16, LR_SHARED));
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.lpszClassName = windowClass;

    HWND window = nullptr;
    bool restoringGeometry = false;
    if (RegisterClassExW(&type)) {
        RECT size{0, 0, MulDiv(560, int(app.dpi), 96), MulDiv(280, int(app.dpi), 96)};
        adjustWindowRectangle(size, WS_OVERLAPPEDWINDOW, 0, app.dpi);
        RECT bounds{};
        restoringGeometry = restoredWindowBounds(savedGeometry, app.dpi, bounds);
        window = CreateWindowExW(0, windowClass, windowTitle, WS_OVERLAPPEDWINDOW,
                                  restoringGeometry ? bounds.left : CW_USEDEFAULT,
                                  restoringGeometry ? bounds.top : CW_USEDEFAULT,
                                  restoringGeometry ? bounds.right - bounds.left : size.right - size.left,
                                  restoringGeometry ? bounds.bottom - bounds.top : size.bottom - size.top,
                                  nullptr, nullptr, instance, nullptr);
        if (window && restoringGeometry
            && restoredWindowBounds(savedGeometry, dpiForWindow(window), bounds)) {
            // The hidden window now knows the destination monitor's actual DPI.
            SetWindowPos(window, nullptr, bounds.left, bounds.top,
                         bounds.right - bounds.left, bounds.bottom - bounds.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    int result = 1;
    if (window) {
        const int showState = restoringGeometry
            ? (savedGeometry.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL) : show;
        ShowWindow(window, showState);
        if (restoringGeometry && IsIconic(window))
            ShowWindow(window, savedGeometry.maximized ? SW_SHOWMAXIMIZED : SW_RESTORE);
        UpdateWindow(window);
        MSG message{};
        BOOL received = 0;
        while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        result = received == 0 ? int(message.wParam) : 1;
    } else {
        MessageBoxW(nullptr, L"Tally Counter could not create its window.",
                     windowTitle, MB_OK | MB_ICONERROR);
    }
    PlaySoundW(nullptr, nullptr, 0);
    if (SUCCEEDED(com)) CoUninitialize();
    Gdiplus::GdiplusShutdown(graphicsToken);
    CloseHandle(instanceMutex);
    return result;
}
