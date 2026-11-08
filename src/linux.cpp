// SPDX-License-Identifier: Zlib
// Copyright (c) 2026 davoda1
// Native GTK 3 frontend. No browser, background service, or network access.
#include "ui.hpp"
#include "linux_audio.hpp"
#include "reset_sound.hpp"

#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <glib/gstdio.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
using tally::Align;
using tally::Color;
using tally::Rect;
constexpr double pi = 3.14159265358979323846;

double seconds() { return static_cast<double>(g_get_monotonic_time()) / 1000000.0; }

class CairoPainter final : public tally::Painter {
    cairo_t* cr;

    void source(Color c) const { cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a); }
    void finish(Rect r, Color top, Color bottom, Color border, double width) {
        cairo_pattern_t* gradient = cairo_pattern_create_linear(r.x, r.y, r.x, r.y + std::max(0.001, r.h));
        cairo_pattern_add_color_stop_rgba(gradient, 0, top.r, top.g, top.b, top.a);
        cairo_pattern_add_color_stop_rgba(gradient, 1, bottom.r, bottom.g, bottom.b, bottom.a);
        cairo_set_source(cr, gradient);
        if (border.a > 0 && width > 0) {
            cairo_fill_preserve(cr);
            source(border);
            cairo_set_line_width(cr, width);
            cairo_stroke(cr);
        } else {
            cairo_fill(cr);
        }
        cairo_pattern_destroy(gradient);
    }

public:
    explicit CairoPainter(cairo_t* context) : cr(context) {
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    }

    void rect(Rect r, Color color) override {
        if (r.w <= 0 || r.h <= 0) return;
        cairo_save(cr);
        cairo_new_path(cr);
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        source(color);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    void rounded(Rect r, double radius, Color top, Color bottom, Color border, double width) override {
        if (r.w <= 0 || r.h <= 0) return;
        radius = std::clamp(radius, 0.0, std::min(r.w, r.h) / 2.0);
        cairo_save(cr);
        cairo_new_path(cr);
        if (radius == 0) {
            cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        } else {
            cairo_arc(cr, r.x + r.w - radius, r.y + radius, radius, -pi / 2, 0);
            cairo_arc(cr, r.x + r.w - radius, r.y + r.h - radius, radius, 0, pi / 2);
            cairo_arc(cr, r.x + radius, r.y + r.h - radius, radius, pi / 2, pi);
            cairo_arc(cr, r.x + radius, r.y + radius, radius, pi, 3 * pi / 2);
            cairo_close_path(cr);
        }
        finish(r, top, bottom, border, width);
        cairo_restore(cr);
    }

    void ellipse(Rect r, Color top, Color bottom, Color border, double width) override {
        if (r.w <= 0 || r.h <= 0) return;
        cairo_save(cr);
        cairo_new_path(cr);
        cairo_save(cr);
        cairo_translate(cr, r.x + r.w / 2, r.y + r.h / 2);
        cairo_scale(cr, r.w / 2, r.h / 2);
        cairo_arc(cr, 0, 0, 1, 0, 2 * pi);
        cairo_restore(cr);
        finish(r, top, bottom, border, width);
        cairo_restore(cr);
    }

    void line(double x1, double y1, double x2, double y2, Color color, double width) override {
        if (width <= 0) return;
        cairo_save(cr);
        cairo_new_path(cr);
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
        cairo_set_line_width(cr, width);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        source(color);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    void text(const std::string& value, Rect r, double size, Color color, bool bold,
              bool mono, Align align) override {
        if (value.empty() || size <= 0) return;
        cairo_save(cr);
        PangoLayout* layout = pango_cairo_create_layout(cr);
        PangoFontDescription* font = pango_font_description_new();
        pango_font_description_set_family(font, mono ? "DejaVu Sans Mono, monospace" : "Sans");
        pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
        pango_font_description_set_weight(font, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
        pango_layout_set_font_description(layout, font);
        pango_layout_set_single_paragraph_mode(layout, TRUE);
        pango_layout_set_text(layout, value.c_str(), static_cast<int>(value.size()));
        PangoRectangle logical;
        pango_layout_get_pixel_extents(layout, nullptr, &logical);
        double x = r.x - logical.x;
        if (align == Align::Center) x += (r.w - logical.width) / 2;
        else if (align == Align::Right) x += r.w - logical.width;
        const double y = r.y + (r.h - logical.height) / 2 - logical.y;
        cairo_move_to(cr, x, y);
        source(color);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(font);
        g_object_unref(layout);
        cairo_restore(cr);
    }

    void fillPath(const tally::PathCommand* commands, std::size_t count, Color color) override {
        if (!commands || count == 0) return;
        cairo_save(cr);
        cairo_new_path(cr);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& command = commands[i];
            switch (command.verb) {
            case tally::PathVerb::Move:
                cairo_move_to(cr, command.x1, command.y1);
                break;
            case tally::PathVerb::Line:
                cairo_line_to(cr, command.x1, command.y1);
                break;
            case tally::PathVerb::Curve:
                cairo_curve_to(cr, command.x1, command.y1, command.x2, command.y2,
                               command.x3, command.y3);
                break;
            case tally::PathVerb::Close:
                cairo_close_path(cr);
                break;
            }
        }
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        source(color);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    void clip(Rect r) override {
        cairo_save(cr);
        cairo_new_path(cr);
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        cairo_clip(cr);
    }

    void unclip() override { cairo_restore(cr); }
};

struct Application {
    GtkApplication* app = nullptr;
    GtkWidget* window = nullptr;
    GtkWidget* area = nullptr;
    tally::View view;
    tally::ClickAudio audio;
    std::string directory;
    std::string stateFile;
    std::string geometryFile;
    int normalWidth = 560, normalHeight = 280, normalX = 0, normalY = 0;
    bool normalPosition = false;
    bool lastMaximized = false;
    bool geometrySaved = false;
    guint geometrySource = 0;
    guint animationSource = 0;
    guint keyReleaseSource = 0;
    bool spaceDown = false;
    guint32 spaceReleasedAt = 0;
    guint16 spaceReleasedKey = 0;
    bool resetArmed = false;

    ~Application() {
        stopSources();
        if (GtkSettings* settings = gtk_settings_get_default())
            g_signal_handlers_disconnect_by_data(settings, this);
    }

    void stopSources() {
        if (geometrySource) { g_source_remove(geometrySource); geometrySource = 0; }
        if (animationSource) { g_source_remove(animationSource); animationSource = 0; }
        if (keyReleaseSource) { g_source_remove(keyReleaseSource); keyReleaseSource = 0; }
    }

    tally::Layout layout() const {
        return view.layout(gtk_widget_get_allocated_width(area), gtk_widget_get_allocated_height(area));
    }

    void updateAccessibleName() const {
        if (!area) return;
        const std::string name = "Tally counter: " + std::to_string(view.counter.value) + " of 9999";
        atk_object_set_name(gtk_widget_get_accessible(area), name.c_str());
    }

    void load() {
        gchar* path = g_build_filename(g_get_user_config_dir(), "tally-counter", nullptr);
        directory = path;
        g_free(path);
        path = g_build_filename(directory.c_str(), "count.txt", nullptr);
        stateFile = path;
        g_free(path);
        // A counter state is tiny; a malformed or unexpectedly large file is ignored.
        std::array<char, 64> bytes{};
        std::string saved;
        if (FILE* file = g_fopen(stateFile.c_str(), "rb")) {
            const std::size_t n = std::fread(bytes.data(), 1, bytes.size(), file);
            if (n < bytes.size() && !std::ferror(file)) saved.assign(bytes.data(), n);
            std::fclose(file);
        }
        view.restore(saved, seconds());
        path = g_build_filename(directory.c_str(), "window.ini", nullptr);
        geometryFile = path;
        g_free(path);
        loadGeometry();
    }

    void save() {
        const std::string contents = std::to_string(view.counter.value) + "\n";
        GError* error = nullptr;
        // GLib writes a temporary file and replaces the old file atomically.
        // A private directory keeps this per-user state out of shared locations.
        bool ok = g_mkdir_with_parents(directory.c_str(), 0700) == 0;
        if (ok) ok = g_file_set_contents(stateFile.c_str(), contents.c_str(),
                                        static_cast<gssize>(contents.size()), &error);
        view.saveError = !ok;
        if (error) g_error_free(error);
    }

    static bool canPositionWindow() {
#ifdef GDK_WINDOWING_X11
        return GDK_IS_X11_DISPLAY(gdk_display_get_default());
#else
        return false;
#endif
    }

    void loadGeometry() {
        normalWidth = 560;
        normalHeight = 280;
        normalPosition = false;
        lastMaximized = false;
        geometrySaved = false;
        std::array<char, 4096> contents{};
        std::size_t size = 0;
        bool readable = false;
        if (FILE* file = g_fopen(geometryFile.c_str(), "rb")) {
            size = std::fread(contents.data(), 1, contents.size(), file);
            readable = size < contents.size() && !std::ferror(file);
            std::fclose(file);
        }
        GKeyFile* keys = g_key_file_new();
        if (readable && g_key_file_load_from_data(keys, contents.data(), size, G_KEY_FILE_NONE, nullptr)) {
            auto integer = [keys](const char* name, int fallback) {
                GError* error = nullptr;
                const int value = g_key_file_get_integer(keys, "Window", name, &error);
                if (!error) return value;
                g_error_free(error);
                return fallback;
            };
            const int width = integer("width", 560), height = integer("height", 280);
            if (width >= 400 && width <= 16384 && height >= 220 && height <= 16384) {
                normalWidth = width;
                normalHeight = height;
            }
            const int x = integer("x", 200000), y = integer("y", 200000);
            normalPosition = canPositionWindow() &&
                g_key_file_get_boolean(keys, "Window", "position_saved", nullptr) &&
                x >= -131072 && x <= 131072 && y >= -131072 && y <= 131072;
            if (normalPosition) { normalX = x; normalY = y; }
            lastMaximized = g_key_file_get_boolean(keys, "Window", "maximized", nullptr);
        }
        g_key_file_unref(keys);
        clampGeometry();
    }

    void clampGeometry() {
        GdkDisplay* display = gdk_display_get_default();
        if (!display || gdk_display_get_n_monitors(display) < 1) return;
        GdkMonitor* monitor = normalPosition
            ? gdk_display_get_monitor_at_point(display, normalX + normalWidth / 2, normalY + normalHeight / 2)
            : gdk_display_get_primary_monitor(display);
        if (!monitor) monitor = gdk_display_get_monitor(display, 0);
        GdkRectangle work{};
        gdk_monitor_get_workarea(monitor, &work);
        // Reserve room for the native WM frame when an old window was larger
        // than this monitor. Ordinary saved sizes are restored exactly.
        normalWidth = std::clamp(normalWidth, 400, std::max(400, work.width - 24));
        normalHeight = std::clamp(normalHeight, 220, std::max(220, work.height - 48));
        if (normalPosition) {
            normalX = std::clamp(normalX, work.x, std::max(work.x, work.x + work.width - normalWidth - 24));
            normalY = std::clamp(normalY, work.y, std::max(work.y, work.y + work.height - normalHeight - 48));
        }
    }

    GdkWindowState windowState() const {
        GdkWindow* native = window ? gtk_widget_get_window(window) : nullptr;
        return native ? gdk_window_get_state(native) : static_cast<GdkWindowState>(0);
    }

    void captureNormalGeometry() {
        if (!window || !gtk_widget_get_mapped(window)) return;
        const auto state = windowState();
        if (state & (GDK_WINDOW_STATE_ICONIFIED | GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN)) return;
        int width = 0, height = 0;
        gtk_window_get_size(GTK_WINDOW(window), &width, &height);
        if (width < 400 || height < 220 || width > 16384 || height > 16384) return;
        normalWidth = width;
        normalHeight = height;
        if (canPositionWindow()) {
            gtk_window_get_position(GTK_WINDOW(window), &normalX, &normalY);
            normalPosition = true;
        }
    }

    static gboolean captureGeometryLater(gpointer user) {
        auto& self = *static_cast<Application*>(user);
        self.geometrySource = 0;
        self.captureNormalGeometry();
        return G_SOURCE_REMOVE;
    }

    static gboolean configured(GtkWidget*, GdkEventConfigure*, gpointer user) {
        auto& self = *static_cast<Application*>(user);
        if (self.geometrySource) g_source_remove(self.geometrySource);
        // Debounce WM transitions so maximized dimensions never replace the
        // normal dimensions. No file is written until the window closes.
        self.geometrySource = g_timeout_add(100, captureGeometryLater, &self);
        return FALSE;
    }

    static gboolean stateChanged(GtkWidget*, GdkEventWindowState* event, gpointer user) {
        auto& self = *static_cast<Application*>(user);
        if (!(event->new_window_state & (GDK_WINDOW_STATE_ICONIFIED | GDK_WINDOW_STATE_FULLSCREEN)))
            self.lastMaximized = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
        if (event->new_window_state & (GDK_WINDOW_STATE_ICONIFIED | GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN)) {
            if (self.geometrySource) { g_source_remove(self.geometrySource); self.geometrySource = 0; }
        } else {
            if (self.geometrySource) g_source_remove(self.geometrySource);
            self.geometrySource = g_timeout_add(100, captureGeometryLater, &self);
        }
        return FALSE;
    }

    void saveGeometry() {
        if (geometrySaved) return;
        captureNormalGeometry();
        GKeyFile* keys = g_key_file_new();
        g_key_file_set_integer(keys, "Window", "width", normalWidth);
        g_key_file_set_integer(keys, "Window", "height", normalHeight);
        g_key_file_set_integer(keys, "Window", "x", normalX);
        g_key_file_set_integer(keys, "Window", "y", normalY);
        g_key_file_set_boolean(keys, "Window", "position_saved", normalPosition);
        g_key_file_set_boolean(keys, "Window", "maximized", lastMaximized);
        gsize size = 0;
        gchar* contents = g_key_file_to_data(keys, &size, nullptr);
        if (contents && g_mkdir_with_parents(directory.c_str(), 0700) == 0)
            g_file_set_contents(geometryFile.c_str(), contents, static_cast<gssize>(size), nullptr);
        g_free(contents);
        g_key_file_unref(keys);
        geometrySaved = true;
    }

    static gboolean closing(GtkWidget*, GdkEvent*, gpointer user) {
        static_cast<Application*>(user)->saveGeometry();
        return FALSE;
    }

    static gboolean animationTick(gpointer user) {
        auto& self = *static_cast<Application*>(user);
        if (self.area) gtk_widget_queue_draw(self.area);
        if (!self.area || !self.view.animating(seconds())) {
            self.animationSource = 0;
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }

    void redraw() {
        if (!area) return;
        gtk_widget_queue_draw(area);
        if (!animationSource && view.animating(seconds()))
            animationSource = g_timeout_add(16, animationTick, this);
    }

    void increment() {
        if (view.increment(seconds())) {
            save();
            audio.trigger();
        }
        updateAccessibleName();
        redraw();
    }

    void reset() {
        const double now = seconds();
        const bool startsReset = view.counter.value != 0 || !view.resetSpin.moving(now);
        view.reset(now);
        if (startsReset) {
            try {
                auto samples = tally::reset_sound::render(view.resetSpin.seconds,
                    view.resetSpin.from, view.resetSpin.to);
                if (!samples.empty()) {
                    // The visual and audio use the same reset trajectory.
                    view.resetSpin.started = seconds();
                    audio.triggerReset(std::move(samples));
                }
            } catch (...) {
                // Missing audio resources never prevent resetting or saving.
            }
        }
        // Save even if already zero, so Reset can also retry a failed save.
        save();
        updateAccessibleName();
        redraw();
    }

    void readTheme() {
        if (!window) return;
        gboolean preferDark = FALSE;
        gchar* name = nullptr;
        if (GtkSettings* settings = gtk_settings_get_default())
            g_object_get(settings, "gtk-application-prefer-dark-theme", &preferDark,
                         "gtk-theme-name", &name, nullptr);
        std::string theme = name ? name : "";
        g_free(name);
        std::transform(theme.begin(), theme.end(), theme.begin(),
                       [](unsigned char c) { return static_cast<char>(g_ascii_tolower(c)); });
        GdkRGBA background{}, foreground{};
        GtkStyleContext* context = gtk_widget_get_style_context(window);
        // GTK 3's window background follows the selected system theme.
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        gtk_style_context_get_background_color(context, GTK_STATE_FLAG_NORMAL, &background);
        G_GNUC_END_IGNORE_DEPRECATIONS
        gtk_style_context_get_color(context, GTK_STATE_FLAG_NORMAL, &foreground);
        auto luminance = [](const GdkRGBA& c) { return .2126 * c.red + .7152 * c.green + .0722 * c.blue; };
        const bool darkBackground = background.alpha > .1
            ? luminance(background) < .45 : luminance(foreground) > .6;
        view.dark = preferDark || theme.find("dark") != std::string::npos || darkBackground;
        redraw();
    }

    static gboolean draw(GtkWidget* widget, cairo_t* cr, gpointer user) {
        auto& self = *static_cast<Application*>(user);
        CairoPainter painter(cr);
        self.view.paint(painter, gtk_widget_get_allocated_width(widget),
                        gtk_widget_get_allocated_height(widget), seconds());
        return TRUE;
    }

    static gboolean buttonPress(GtkWidget* widget, GdkEventButton* event, gpointer user) {
        auto& self = *static_cast<Application*>(user);
        // GTK emits an extra 2BUTTON/3BUTTON event after ordinary press events.
        // Only the ordinary presses count; a double-click therefore adds exactly two.
        if (event->button != 1 || event->type != GDK_BUTTON_PRESS) return FALSE;
        gtk_widget_grab_focus(widget);
        self.view.focusReset = false;
        self.resetArmed = self.layout().inReset(event->x, event->y);
        self.view.pressedReset = self.resetArmed;
        if (self.resetArmed) self.redraw();
        else self.increment();
        return TRUE;
    }

    static gboolean buttonRelease(GtkWidget*, GdkEventButton* event, gpointer user) {
        auto& self = *static_cast<Application*>(user);
        if (event->button != 1) return FALSE;
        const bool shouldReset = self.resetArmed && self.layout().inReset(event->x, event->y);
        self.resetArmed = false;
        self.view.pressedReset = false;
        if (shouldReset) self.reset();
        else self.redraw();
        return TRUE;
    }

    static gboolean motion(GtkWidget*, GdkEventMotion* event, gpointer user) {
        auto& self = *static_cast<Application*>(user);
        const bool hover = self.layout().inReset(event->x, event->y);
        const bool pressed = self.resetArmed && hover;
        if (hover != self.view.hoverReset || pressed != self.view.pressedReset) {
            self.view.hoverReset = hover;
            self.view.pressedReset = pressed;
            self.redraw();
        }
        return TRUE;
    }

    static gboolean leave(GtkWidget*, GdkEventCrossing*, gpointer user) {
        auto& self = *static_cast<Application*>(user);
        self.view.hoverReset = false;
        self.view.pressedReset = false;
        self.redraw();
        return FALSE;
    }

    static gboolean completeKeyRelease(gpointer user) {
        auto& self = *static_cast<Application*>(user);
        self.keyReleaseSource = 0;
        self.spaceDown = false;
        return G_SOURCE_REMOVE;
    }

    static gboolean keyPress(GtkWidget*, GdkEventKey* event, gpointer user) {
        auto& self = *static_cast<Application*>(user);
        const bool modified = (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_META_MASK)) != 0;
        if (event->keyval == GDK_KEY_space || event->keyval == GDK_KEY_KP_Space) {
            // Key release is deferred to idle: legacy X11 auto-repeat release/press
            // pairs remain one physical hold, as do normal repeated key presses.
            if (self.keyReleaseSource) {
                g_source_remove(self.keyReleaseSource);
                self.keyReleaseSource = 0;
                // Genuine rapid presses may also be queued before idle. Only an
                // equal-time, equal-key pair is an X11 synthetic repeat pair.
                if (event->time != self.spaceReleasedAt || event->hardware_keycode != self.spaceReleasedKey)
                    self.spaceDown = false;
            }
            const bool firstPress = !self.spaceDown;
            self.spaceDown = true;
            if (firstPress && !modified) self.increment();
            return modified ? FALSE : TRUE;
        }
        if (!modified && (event->keyval == GDK_KEY_Tab || event->keyval == GDK_KEY_ISO_Left_Tab)) {
            self.view.focusReset = !self.view.focusReset;
            gtk_widget_grab_focus(self.area);
            self.redraw();
            return TRUE;
        }
        if (!modified && self.view.focusReset &&
            (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)) {
            self.reset();
            return TRUE;
        }
        return FALSE;
    }

    static gboolean keyRelease(GtkWidget*, GdkEventKey* event, gpointer user) {
        auto& self = *static_cast<Application*>(user);
        if (event->keyval != GDK_KEY_space && event->keyval != GDK_KEY_KP_Space) return FALSE;
        self.spaceReleasedAt = event->time;
        self.spaceReleasedKey = event->hardware_keycode;
        if (!self.keyReleaseSource)
            self.keyReleaseSource = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, completeKeyRelease, &self, nullptr);
        return TRUE;
    }

    static gboolean focusOut(GtkWidget*, GdkEventFocus*, gpointer user) {
        auto& self = *static_cast<Application*>(user);
        if (self.keyReleaseSource) {
            g_source_remove(self.keyReleaseSource);
            self.keyReleaseSource = 0;
        }
        self.spaceDown = false;
        self.resetArmed = false;
        self.view.pressedReset = false;
        self.redraw();
        return FALSE;
    }

    static void destroyed(GtkWidget*, gpointer user) {
        auto& self = *static_cast<Application*>(user);
        self.saveGeometry();
        self.stopSources();
        self.area = nullptr;
        self.window = nullptr;
    }

    static void styleChanged(GtkWidget*, gpointer user) {
        static_cast<Application*>(user)->readTheme();
    }

    static void themeChanged(GObject*, GParamSpec*, gpointer user) {
        static_cast<Application*>(user)->readTheme();
    }

    static void activate(GtkApplication* app, gpointer user) {
        auto& self = *static_cast<Application*>(user);
        if (self.window) {
            gtk_window_present(GTK_WINDOW(self.window));
            return;
        }
        self.load();
        self.window = gtk_application_window_new(app);
        gtk_window_set_title(GTK_WINDOW(self.window), "Tally Counter");
        gtk_window_set_default_size(GTK_WINDOW(self.window), self.normalWidth, self.normalHeight);
        if (self.normalPosition) gtk_window_move(GTK_WINDOW(self.window), self.normalX, self.normalY);
        if (self.lastMaximized) gtk_window_maximize(GTK_WINDOW(self.window));
        gtk_window_set_icon_name(GTK_WINDOW(self.window), "tally-counter");
        self.area = gtk_drawing_area_new();
        gtk_widget_set_can_focus(self.area, TRUE);
        gtk_widget_add_events(self.area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                            GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);
        gtk_container_add(GTK_CONTAINER(self.window), self.area);
        GdkGeometry geometry{};
        geometry.min_width = 400;
        geometry.min_height = 220;
        gtk_window_set_geometry_hints(GTK_WINDOW(self.window), self.area, &geometry, GDK_HINT_MIN_SIZE);
        AtkObject* accessible = gtk_widget_get_accessible(self.area);
        atk_object_set_role(accessible, ATK_ROLE_PANEL);
        atk_object_set_description(accessible,
            "Click the counter or press Space to add one. Holding Space counts once. "
            "Tab selects the reset knob; Enter resets to zero. Space always adds one. "
            "The maximum is 9999. The count is saved automatically.");
        self.updateAccessibleName();
        g_signal_connect(self.area, "draw", G_CALLBACK(draw), &self);
        g_signal_connect(self.area, "button-press-event", G_CALLBACK(buttonPress), &self);
        g_signal_connect(self.area, "button-release-event", G_CALLBACK(buttonRelease), &self);
        g_signal_connect(self.area, "motion-notify-event", G_CALLBACK(motion), &self);
        g_signal_connect(self.area, "leave-notify-event", G_CALLBACK(leave), &self);
        g_signal_connect(self.window, "key-press-event", G_CALLBACK(keyPress), &self);
        g_signal_connect(self.window, "key-release-event", G_CALLBACK(keyRelease), &self);
        g_signal_connect(self.window, "focus-out-event", G_CALLBACK(focusOut), &self);
        g_signal_connect(self.window, "configure-event", G_CALLBACK(configured), &self);
        g_signal_connect(self.window, "window-state-event", G_CALLBACK(stateChanged), &self);
        g_signal_connect(self.window, "delete-event", G_CALLBACK(closing), &self);
        g_signal_connect(self.window, "destroy", G_CALLBACK(destroyed), &self);
        g_signal_connect(self.window, "style-updated", G_CALLBACK(styleChanged), &self);
        if (GtkSettings* settings = gtk_settings_get_default()) {
            g_signal_connect(settings, "notify::gtk-theme-name", G_CALLBACK(themeChanged), &self);
            g_signal_connect(settings, "notify::gtk-application-prefer-dark-theme", G_CALLBACK(themeChanged), &self);
        }
        gtk_widget_show_all(self.window);
        gtk_widget_grab_focus(self.area);
        self.readTheme();
    }
};
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::puts("Tally Counter 1.0.1");
        return 0;
    }
    g_set_application_name("Tally Counter");
    g_set_prgname("tally-counter");
    Application state;
    state.app = gtk_application_new("local.tallycounter.TallyCounter", static_cast<GApplicationFlags>(0));
    g_signal_connect(state.app, "activate", G_CALLBACK(Application::activate), &state);
    const int result = g_application_run(G_APPLICATION(state.app), argc, argv);
    g_object_unref(state.app);
    state.app = nullptr;
    return result;
}
