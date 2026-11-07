/* Copyright (c) 2026 davoda1. See LICENSE for the zlib license. */
/* A native AppImage entry point. No shell, Python, global install or root needed. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void die(const char *message) {
    fprintf(stderr, "Tally Counter AppImage: %s: %s\n", message, strerror(errno));
    exit(1);
}

static char *join(const char *directory, const char *suffix) {
    char *result = NULL;
    if (asprintf(&result, "%s/%s", directory, suffix) < 0) die("out of memory");
    return result;
}

static void setting(const char *name, const char *value) {
    if (setenv(name, value, 1) != 0) die("cannot configure environment");
}

static void path_setting(const char *name, const char *directory, const char *suffix) {
    char *value = join(directory, suffix);
    setting(name, value);
    free(value);
}

static void prepend(const char *name, const char *directory, const char *suffix, const char *fallback) {
    const char *old = getenv(name);
    if (!old || !*old) old = fallback;
    char *prefix = join(directory, suffix);
    char *value = NULL;
    if (asprintf(&value, "%s%s%s", prefix, old && *old ? ":" : "", old ? old : "") < 0)
        die("out of memory");
    setting(name, value);
    free(prefix);
    free(value);
}

/* Loader caches contain absolute paths. Relocate into an anonymous descriptor,
 * inherited by the application, rather than writing into the mounted image or
 * sharing a mutable cache file between concurrent application instances. */
static void relocated_file(const char *appdir, const char *relative, const char *variable) {
    char *path = join(appdir, relative);
    FILE *input = fopen(path, "r");
    free(path);
    if (!input) die("cannot open bundled image-loader cache");
    int fd = memfd_create("tally-counter-pixbuf-loaders", 0);
    if (fd < 0) {
        char temporary[] = "/tmp/tally-counter-pixbuf-XXXXXX";
        fd = mkstemp(temporary);
        if (fd >= 0) unlink(temporary);
    }
    if (fd < 0) die("cannot create private image-loader cache");
    FILE *output = fdopen(dup(fd), "w");
    if (!output) die("cannot write private image-loader cache");
    char *line = NULL;
    size_t capacity = 0;
    while (getline(&line, &capacity, input) >= 0) {
        char *cursor = line, *match;
        while ((match = strstr(cursor, "@APPDIR@")) != NULL) {
            if (fwrite(cursor, 1, (size_t)(match - cursor), output) != (size_t)(match - cursor))
                die("cannot write image-loader cache");
            for (const char *p = appdir; *p; ++p) {
                if (*p == '\\' || *p == '"') fputc('\\', output);
                fputc(*p, output);
            }
            cursor = match + 8;
        }
        fputs(cursor, output);
    }
    free(line);
    if (ferror(input) || ferror(output)) die("cannot relocate image-loader cache");
    fclose(input);
    if (fclose(output) != 0 || lseek(fd, 0, SEEK_SET) < 0) die("cannot finish image-loader cache");
    char descriptor[64];
    snprintf(descriptor, sizeof descriptor, "/proc/self/fd/%d", fd);
    setting(variable, descriptor);
}

int main(int argc, char **argv) {
    (void)argc;
    char executable[PATH_MAX + 1];
    ssize_t size = readlink("/proc/self/exe", executable, sizeof executable - 1);
    if (size <= 0 || size >= (ssize_t)sizeof executable - 1) die("cannot locate AppRun");
    executable[size] = '\0';
    char *slash = strrchr(executable, '/');
    if (!slash) die("invalid AppRun location");
    *slash = '\0';
    const char *appdir = executable;
    setting("APPDIR", appdir);
    /* The counter draws with Cairo; avoid probing host GL drivers unnecessarily. */
    if (!getenv("GDK_GL") && setenv("GDK_GL", "disable", 0) != 0)
        die("cannot configure software rendering");
    prepend("LD_LIBRARY_PATH", appdir, "usr/lib", NULL);
    prepend("XDG_DATA_DIRS", appdir, "usr/share", "/usr/local/share:/usr/share");
    path_setting("GTK_DATA_PREFIX", appdir, "usr");
    path_setting("GTK_EXE_PREFIX", appdir, "usr");
    path_setting("GTK_PATH", appdir, "usr/lib/gtk-3.0");
    path_setting("GSETTINGS_SCHEMA_DIR", appdir, "usr/share/glib-2.0/schemas");
    path_setting("GIO_MODULE_DIR", appdir, "usr/lib/gio/modules");
    path_setting("GIO_EXTRA_MODULES", appdir, "usr/lib/gio/modules");
    path_setting("GDK_PIXBUF_MODULEDIR", appdir, "usr/lib/gdk-pixbuf-2.0/2.10.0/loaders");
    path_setting("FONTCONFIG_FILE", appdir, "usr/etc/fonts/fonts.conf");
    path_setting("ALSA_CONFIG_DIR", appdir, "usr/share/alsa");
    path_setting("ALSA_PLUGIN_DIR", appdir, "usr/lib/alsa-lib");
    relocated_file(appdir, "usr/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache.in", "GDK_PIXBUF_MODULE_FILE");
    const char *audio_config = getenv("ALSA_CONFIG_PATH");
    if (!audio_config || !*audio_config)
        relocated_file(appdir, "usr/share/alsa/tally-counter.conf.in", "ALSA_CONFIG_PATH");
    char *program = join(appdir, "usr/bin/tally-counter");
    argv[0] = program;
    execv(program, argv);
    die("cannot start bundled native executable");
    return 1;
}
