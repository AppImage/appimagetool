/**************************************************************************
 * 
 * Copyright (c) 2004-24 Simon Peter
 * 
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * 
 **************************************************************************/

#ident "AppImage by Simon Peter, http://appimage.org/"

#ifndef RELEASE_NAME
    #define RELEASE_NAME "continuous build"
#endif

#define _GNU_SOURCE
#include <ftw.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <stdio.h>
#include <argp.h>

#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <libgen.h>

#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <gpgme.h>
#include <assert.h>

#include "util.h"

#include "appimagetool_fetch_runtime.h"
#include "appimagetool_sign.h"

typedef enum {
    fARCH_i686,
    fARCH_x86_64,
    fARCH_armhf,
    fARCH_aarch64
} fARCH;

static gchar const APPIMAGEIGNORE[] = ".appimageignore";
static char _exclude_file_desc[256];

static gboolean list = FALSE;
static gboolean verbose = FALSE;
static gboolean showVersionOnly = FALSE;
static gboolean sign = FALSE;
static gboolean no_appstream = FALSE;
static gboolean no_check_permissions = FALSE;
gchar **remaining_args = NULL;
gchar *updateinformation = NULL;
static gboolean guess_update_information = FALSE;
gchar *sqfs_comp = NULL;
gchar **sqfs_opts = NULL;
gchar *exclude_file = NULL;
gchar *runtime_file = NULL;
gchar *sign_key = NULL;
gchar *pathToMksquashfs = NULL;
gchar *file_url;

// #####################################################################

static void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

/* Generate a squashfs filesystem using mksquashfs on the $PATH 
* execlp(), execvp(), and execvpe() search on the $PATH */
/* Checks of the AppDir before packaging, in two severities.
 *
 * ERRORS stop appimagetool (exit code 1, no AppImage): problems that make the
 * AppImage unusable or that cannot be fixed afterwards: the permissions of the
 * AppDir itself (its top-level directory) and of the files that make an AppDir
 * an AppImage (AppRun, AppRun.wrapped if present, .DirIcon, the .desktop file and the icon file in its
 * top level), and a missing, broken or SVG .DirIcon.
 *
 * WARNINGS do not stop anything (exit code 0) but are collected and printed
 * together at the very end of the output, where they are easy to find in long
 * CI logs, because AppImages with these problems fail the automated tests at
 * https://appimage.github.io. They may become errors in a future version.
 *
 * Modes are only checked, never changed: the packager has to decide how to fix
 * them. Every message names the path, what exactly is wrong and the command
 * that fixes it, because the packager cannot be expected to know how AppImages
 * are mounted and run (by other users than the one who packaged them). */
static int permission_problems = 0;       /* errors */
static GPtrArray *deferred_warnings = NULL; /* formatted warnings, printed at exit */
static gboolean mode_warnings = FALSE;    /* any warning that "chmod -R a+rX" fixes */
static gboolean build_aborted = FALSE;    /* an error stopped appimagetool, no AppImage */
static gchar *appdir_icon_file = NULL;    /* icon file in the top level, found from Icon= */

#define MAX_CRITICAL 32
static struct { dev_t dev; ino_t ino; } critical_files[MAX_CRITICAL];
static int n_critical_files = 0;

static void add_critical_file(const char *path) {
    struct stat st;
    if (n_critical_files < MAX_CRITICAL && path != NULL && stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        critical_files[n_critical_files].dev = st.st_dev;
        critical_files[n_critical_files].ino = st.st_ino;
        n_critical_files++;
    }
}

static gboolean is_critical_file(const struct stat *st) {
    for (int i = 0; i < n_critical_files; i++)
        if (critical_files[i].dev == st->st_dev && critical_files[i].ino == st->st_ino)
            return TRUE;
    return FALSE;
}

static void print_warning_banner(void) {
    if (deferred_warnings == NULL || deferred_warnings->len == 0)
        return;
    guint n = deferred_warnings->len;
    fflush(stdout);
    fprintf(stderr, "\n"
            "################################################################################\n"
            "#  appimagetool: %u WARNING%s ABOUT YOUR APPDIR - PLEASE FIX %s\n"
            "################################################################################\n"
            "If these are not fixed, this AppImage would very likely FAIL the automated tests\n"
            "at https://appimage.github.io, and may crash with 'Permission\n"
            "denied' or similar errors for users other than you.\n",
            n, n == 1 ? "" : "S", n == 1 ? "IT" : "THEM");
    if (build_aborted)
        fprintf(stderr, "(These come in addition to the ERROR(S) above, which stopped the build.)\n\n");
    else
        fprintf(stderr, "appimagetool exits with code 0 because of warnings only. THIS MAY CHANGE IN A\n"
                        "FUTURE VERSION, when these warnings may become errors that stop the build.\n\n");
    for (guint i = 0; i < n; i++)
        fprintf(stderr, "WARNING %u of %u: %s\n", i + 1, n, (const char *) g_ptr_array_index(deferred_warnings, i));
    if (mode_warnings)
        fprintf(stderr, "To fix all read/execute permission warnings at once, run 'chmod -R a+rX' on the\n"
                        "AppDir and package again (it does not fix setuid/setgid warnings).\n\n");
    if (!build_aborted)
        fprintf(stderr, "(Reminder: %u warning%s above, the AppImage was created anyway.)\n", n,
                n == 1 ? "" : "s");
    fprintf(stderr, "################################################################################\n");
    if (getenv("GITHUB_ACTIONS") != NULL) {
        /* Annotations that show up on the summary page of a GitHub Actions run */
        for (guint i = 0; i < n; i++) {
            /* GitHub's escaping of workflow commands: % \r \n */
            gchar *msg = g_strdup((const char *) g_ptr_array_index(deferred_warnings, i));
            gchar *p1 = g_strdelimit(g_strdup(msg), "\r", ' ');
            gchar *p2 = g_strjoinv("%25", g_strsplit(p1, "%", -1));
            gchar *escaped = g_strjoinv("%0A", g_strsplit(p2, "\n", -1));
            printf("::warning title=appimagetool: AppDir problem - this AppImage would FAIL tests at "
                   "appimage.github.io::%s\n", escaped);
            g_free(escaped);
            g_free(p2);
            g_free(p1);
            g_free(msg);
        }
        fflush(stdout);
    }
}

/* Reports one problem. Errors are printed immediately, warnings are kept until
 * the end of the output */
static void report_problem(gboolean is_error, const char *path, const char *what, const char *fix_format, ...) {
    gchar *quoted = g_shell_quote(path);
    va_list args;
    va_start(args, fix_format);
    gchar *fix = g_strdup_vprintf(fix_format, args);
    va_end(args);
    if (is_error) {
        fprintf(stderr, "ERROR: %s\n       Problem: %s\n       Fix:     %s %s\n\n", path, what, fix, quoted);
        permission_problems++;
    } else {
        if (deferred_warnings == NULL) {
            deferred_warnings = g_ptr_array_new_with_free_func(g_free);
            atexit(print_warning_banner);
        }
        g_ptr_array_add(deferred_warnings,
                        g_strdup_printf("%s\n       Problem: %s\n       Fix:     %s %s\n", path, what, fix, quoted));
    }
    g_free(fix);
    g_free(quoted);
}

static void mode_string(mode_t mode, char *out) {
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++)
        out[i] = (mode & (0400 >> i)) ? rwx[i] : '-';
    out[9] = '\0';
}

static void require_mode(gboolean is_error, const char *path, const struct stat *st, mode_t wanted) {
    mode_t mode = st->st_mode & 07777;
    mode_t missing = wanted & ~mode;
    if (missing == 0)
        return;
    char have[10], need[10];
    mode_string(mode, have);
    mode_string(mode | wanted, need);
    const char *kind = S_ISDIR(st->st_mode) ? "directory" : (wanted & 0111) ? "program/script" : "file";
    gchar *what = g_strdup_printf("this %s has mode %04o (%s), so some users (group and/or others) cannot %s it. "
                                  "AppImages are run by users other than the one who packaged them, who would get "
                                  "'Permission denied'. It needs at least mode %04o (%s)",
                                  kind, (unsigned) mode, have,
                                  (missing & 0444) && (missing & 0111) ? "read and execute"
                                  : (missing & 0444) ? (S_ISDIR(st->st_mode) ? "list" : "read") : "execute",
                                  (unsigned) ((mode | wanted) & 0777), need);
    report_problem(is_error, path, what, "chmod %s%s", (missing & 0444) ? "a+r" : "",
                   (missing & 0111) ? ((missing & 0444) ? ",a+x" : "a+x") : "");
    if (!is_error)
        mode_warnings = TRUE;
    g_free(what);
}

static int check_permissions_cb(const char *path, const struct stat *st, int type, struct FTW *ftw) {
    gboolean is_top = ftw->level == 0;
    if (type == FTW_SL || type == FTW_SLN)
        return 0;
    if (type == FTW_DNR || type == FTW_NS) {
        report_problem(is_top, path, "cannot be accessed by the user running appimagetool (unreadable directory or "
                             "missing entry); if it is owned by another user such as root, run as that user "
                             "or change the owner, e.g. with 'sudo chown -R $(id -un):$(id -gn) <AppDir>'. "
                             "Then", "chmod a+rx");
        return 0;
    }
    if (type == FTW_D || type == FTW_DP)
        require_mode(is_top, path, st, 0555);
    else if (S_ISREG(st->st_mode)) {
        require_mode(is_critical_file(st), path, st, (st->st_mode & S_IXUSR) ? 0555 : 0444);
        /* The setuid/setgid bits cannot work inside an AppImage (it is mounted
         * nosuid); such binaries, e.g. browser sandboxes or fusermount, need
         * a different approach by the packager instead of a masked failure */
        if (st->st_mode & (S_ISUID | S_ISGID)) {
            gchar *what = g_strdup_printf("this file has the setuid/setgid bit set (mode %04o). AppImages are "
                                          "mounted with 'nosuid', so this bit has no effect and the program would "
                                          "run without the privileges it expects and fail in confusing ways. "
                                          "Do not bundle programs that rely on it (e.g. chrome-sandbox, "
                                          "fusermount, sudo); use the host's copy or an approach that works "
                                          "unprivileged (e.g. user namespaces). Only if the program works "
                                          "without the bit, you can clear it",
                                          (unsigned) (st->st_mode & 07777));
            report_problem(FALSE, path, what, "chmod u-s,g-s");
            g_free(what);
        }
    }
    return 0;
}

static void check_appdir_permissions(const char *appdir) {
    /* Files that make an AppDir an AppImage: problems with them are errors */
    gchar *apprun_path = g_build_filename(appdir, "AppRun", NULL);
    gchar *diricon_path = g_build_filename(appdir, ".DirIcon", NULL);
    add_critical_file(apprun_path);
    gchar *wrapped_path = g_build_filename(appdir, "AppRun.wrapped", NULL);
    add_critical_file(wrapped_path); /* started by AppRun, so it must be executable, too */
    g_free(wrapped_path);
    add_critical_file(diricon_path);
    add_critical_file(appdir_icon_file);
    GDir *dir = g_dir_open(appdir, 0, NULL);
    const gchar *name;
    while (dir != NULL && (name = g_dir_read_name(dir)) != NULL) {
        if (g_str_has_suffix(name, ".desktop")) {
            gchar *p = g_build_filename(appdir, name, NULL);
            add_critical_file(p);
            g_free(p);
        }
    }
    if (dir != NULL)
        g_dir_close(dir);
    g_free(diricon_path);

    if (nftw(appdir, check_permissions_cb, 64, FTW_PHYS) != 0)
        permission_problems++;
    const char *launchers[] = { "AppRun", "AppRun.wrapped" };
    for (size_t i = 0; i < sizeof(launchers) / sizeof(launchers[0]); i++) {
        gchar *launcher = g_build_filename(appdir, launchers[i], NULL);
        struct stat st;
        if (stat(launcher, &st) == 0) {
            /* A launcher the owner can execute was already checked above */
            if (!(st.st_mode & S_IXUSR))
                require_mode(TRUE, launcher, &st, 0555);
        } else if (i == 0) {
            report_problem(TRUE, launcher, "AppRun does not exist (or is a broken symlink), but every AppDir needs "
                                           "an executable AppRun (a file or a symlink to one). Create it, e.g. a "
                                           "shell script that starts your program, or a symlink to your main "
                                           "executable, and then run",
                           "chmod a+rx");
        }
        g_free(launcher);
    }
    if (permission_problems > 0) {
        gchar *quoted = g_shell_quote(appdir);
        gchar *apprun = g_shell_quote(apprun_path);
        fprintf(stderr, "appimagetool: found %d ERROR(S) in the AppDir, nothing was changed and no AppImage "
                        "was created.\n"
                        "Fix the paths listed above, then run appimagetool again. To fix all read/execute "
                        "permissions at once, run:\n"
                        "    chmod -R a+rX %s\n"
                        "    chmod a+rx %s\n"
                        "If the files belong to root, prefix the chmod commands with sudo or first take ownership with "
                        "this command: sudo chown -R $(id -un):$(id -gn) %s\n"
                        "If you are certain that your AppImage needs these modes, pass --no-check-permissions "
                        "to skip this check.\n", permission_problems, quoted, apprun, quoted);
        build_aborted = TRUE;
        g_free(apprun);
        g_free(quoted);
        g_free(apprun_path);
        exit(1);
    }
    g_free(apprun_path);
}

/* .DirIcon is shown by file managers and used by appimage.github.io; the AppImage is not
 * created if it is missing, a broken symlink, not a regular file, or an SVG. Each
 * message says exactly what is wrong and how to fix it. */
static void diricon_error(const char *diricon_path, const char *what, const char *fix) {
    fprintf(stderr, "ERROR: %s\n       Problem: %s\n       Fix:     %s\n\n", diricon_path, what, fix);
    fprintf(stderr, "appimagetool: no AppImage was created because .DirIcon is not valid.\n");
    build_aborted = TRUE;
    exit(1);
}

static gboolean file_looks_like_svg(const char *path) {
    if (g_str_has_suffix(path, ".svg") || g_str_has_suffix(path, ".svgz"))
        return TRUE;
    char buf[1025];
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return FALSE;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, "<svg") != NULL;
}

static void check_diricon(const char *source, const char *icon_name, const char *icon_file_path) {
    (void) icon_file_path;
    gchar *diricon_path = g_build_filename(source, ".DirIcon", NULL);
    const char *icon = icon_name != NULL ? icon_name : "<Icon= value>";
    gchar *png_path = g_strdup_printf("%s/%s.png", source, icon);
    gchar *q_diricon = g_shell_quote(diricon_path);
    gchar *q_png = g_shell_quote(png_path);
    gchar *fix_new_icon = g_strdup_printf("put a PNG icon (256x256 pixels recommended) at %s, i.e. into the top "
                                          "level of the AppDir with the name from Icon=%s in your .desktop file "
                                          "(e.g. copy it from usr/share/icons/hicolor/256x256/apps/). "
                                          "appimagetool then creates .DirIcon itself",
                                          q_png, icon);
    struct stat lst, st;
    if (lstat(diricon_path, &lst) != 0) {
        gchar *what = g_strdup_printf(".DirIcon does not exist, and no %s.png or %s.xpm was found in the top level of "
                                      "the AppDir (Icon=%s in the .desktop file) to create it from", icon, icon, icon);
        diricon_error(diricon_path, what, fix_new_icon);
    } else if (S_ISLNK(lst.st_mode) && stat(diricon_path, &st) != 0) {
        char target[PATH_MAX];
        ssize_t len = readlink(diricon_path, target, sizeof(target) - 1);
        target[len > 0 ? len : 0] = '\0';
        gchar *what = g_strdup_printf(".DirIcon is a symlink to '%s', but that target does not exist (broken "
                                      "symlink). It must point to a file inside the AppDir; paths outside it, such "
                                      "as /usr/share/..., do not exist on other systems", target);
        gchar *fix = g_strdup_printf("%s. Then delete the broken symlink with 'rm %s'", fix_new_icon, q_diricon);
        diricon_error(diricon_path, what, fix);
    } else if (stat(diricon_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        gchar *what = g_strdup_printf(".DirIcon is a %s, but it must be a PNG image file (or a symlink to one)",
                                      S_ISDIR(st.st_mode) ? "directory" : "special file");
        gchar *fix = g_strdup_printf("delete it with 'rm -r %s', then %s", q_diricon, fix_new_icon);
        diricon_error(diricon_path, what, fix);
    } else if (file_looks_like_svg(diricon_path)) {
        char resolved[PATH_MAX];
        const char *real = realpath(diricon_path, resolved) != NULL ? resolved : diricon_path;
        gchar *q_real = g_shell_quote(real);
        gchar *what = g_strdup_printf(".DirIcon is an SVG image (%s). SVG is hard to parse, so file managers and "
                                      "thumbnailers cannot be expected to handle it; .DirIcon must be a PNG", real);
        gchar *fix = g_strdup_printf("convert it to a PNG with 'rsvg-convert -w 256 -h 256 %s -o %s' (or "
                                     "ImageMagick/Inkscape) and delete the SVG .DirIcon with 'rm %s'; "
                                     "appimagetool then creates .DirIcon from %s.png",
                                     q_real, q_png, q_diricon, icon);
        g_free(q_real);
        diricon_error(diricon_path, what, fix);
    } else {
        /* Not an error, but unusual: warn like the tests at appimage.github.io do */
        unsigned char magic[16] = { 0 };
        FILE *f = fopen(diricon_path, "rb");
        size_t n = f != NULL ? fread(magic, 1, sizeof(magic) - 1, f) : 0;
        if (f != NULL)
            fclose(f);
        if (n < 8 || (memcmp(magic, "\x89PNG", 4) != 0 && memcmp(magic, "/* XPM */", 9) != 0)) {
            if (deferred_warnings == NULL) {
                deferred_warnings = g_ptr_array_new_with_free_func(g_free);
                atexit(print_warning_banner);
            }
            g_ptr_array_add(deferred_warnings,
                            g_strdup_printf("%s\n       Problem: .DirIcon is neither a PNG nor an XPM image, so "
                                            "file managers may not be able to show it\n       Fix:     replace it "
                                            "with a PNG image, e.g. %s\n",
                                            diricon_path, fix_new_icon));
        }
    }
    g_free(fix_new_icon);
    g_free(png_path);
    g_free(q_png);
    g_free(q_diricon);
    g_free(diricon_path);
}

int sfs_mksquashfs(char *source, char *destination, int offset) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("sfs_mksquashfs fork() failed");
        return(-1);
    }

    if (pid > 0) {
        // This is the parent process. Wait for the child to termiante and check its exit status.
        int status;
        if(waitpid(pid, &status, 0) == -1) {
            perror("sfs_mksquashfs waitpid() failed");
            return(-1);
        }
        
        int retcode = WEXITSTATUS(status);
        if (retcode) {
            fprintf(stderr, "mksquashfs (pid %d) exited with code %d\n", pid, retcode);
            return(-1);
        }
        
        return 0;
    } else {
        // we are the child
        gchar* offset_string;
        offset_string = g_strdup_printf("%i", offset);

        guint sqfs_opts_len = sqfs_opts ? g_strv_length(sqfs_opts) : 0;

        int max_num_args = sqfs_opts_len + 22;
        char* args[max_num_args];

        int i = 0;
#ifndef AUXILIARY_FILES_DESTINATION
        args[i++] = "mksquashfs";
#else
        args[i++] = pathToMksquashfs;
#endif
        args[i++] = source;
        args[i++] = destination;
        args[i++] = "-offset";
        args[i++] = offset_string;

        if (sqfs_comp == NULL) {
            sqfs_comp = "zstd";
        }

        args[i++] = "-comp";
        args[i++] = sqfs_comp;

        args[i++] = "-root-owned";
        args[i++] = "-noappend";

        // compression-specific optimization
        if (strcmp(sqfs_comp, "xz") == 0) {
            // https://jonathancarter.org/2015/04/06/squashfs-performance-testing/ says:
            // improved performance by using a 16384 block size with a sacrifice of around 3% more squashfs image space
            args[i++] = "-Xdict-size";
            args[i++] = "100%";
            args[i++] = "-b";
            args[i++] = "16384";
        } else if (strcmp(sqfs_comp, "zstd") == 0) {
            /*
             * > Build with default 128K block size
             * > It used to be 1M but that actually causes much higher startup times.
             * > Some testing might be needed to see if there is some other value that actually improves performance.
             * -- https://github.com/AppImage/appimagetool/issues/64
             */
            args[i++] = "-b";
            args[i++] = "128K";
        }

        // check if ignore file exists and use it if possible
        if (access(APPIMAGEIGNORE, F_OK) >= 0) {
            printf("Including %s", APPIMAGEIGNORE);
            args[i++] = "-wildcards";
            args[i++] = "-ef";

            // avoid warning: assignment discards ‘const’ qualifier
            char* buf = strdup(APPIMAGEIGNORE);
            args[i++] = buf;
        }

        // if an exclude file has been passed on the command line, should be used, too
        if (exclude_file != 0 && strlen(exclude_file) > 0) {
            if (access(exclude_file, F_OK) < 0) {
                printf("WARNING: exclude file %s not found!", exclude_file);
                return -1;
            }

            args[i++] = "-wildcards";
            args[i++] = "-ef";
            args[i++] = exclude_file;
        }

        // don't override time if user sets it
        if (!getenv("SOURCE_DATE_EPOCH")) {
            args[i++] = "-mkfs-time";
            args[i++] = "0";
        }

        for (guint sqfs_opts_idx = 0; sqfs_opts_idx < sqfs_opts_len; ++sqfs_opts_idx) {
            args[i++] = sqfs_opts[sqfs_opts_idx];
        }

        args[i++] = 0;

        if (verbose) {
            printf("mksquashfs commandline: ");
            for (char** t = args; *t != 0; t++) {
                printf("%s ", *t);
            }
            printf("\n");
        }

#ifndef AUXILIARY_FILES_DESTINATION
        execvp("mksquashfs", args);
        perror("execvp(\"mksquashfs\") failed");
#else
        execvp(pathToMksquashfs, args);
        fprintf(stderr, "execvp(\"%s\") failed: %s\n", pathToMksquashfs, strerror(errno));
#endif
        return -1; // exec never returns
    }
    return 0;
}

/* Validate desktop file using desktop-file-validate on the $PATH
* execlp(), execvp(), and execvpe() search on the $PATH */
int validate_desktop_file(char *file) {
    int statval;
    int child_pid;
    child_pid = fork();
    if(child_pid == -1)
    {
        printf("could not fork! \n");
        return 1;
    }
    else if(child_pid == 0)
    {
        execlp("desktop-file-validate", "desktop-file-validate", file, NULL);
    }
    else
    {
        waitpid(child_pid, &statval, WUNTRACED | WCONTINUED);
        if(WIFEXITED(statval)){
            return(WEXITSTATUS(statval));
        }
    }
    return -1;
}

/* Generate a squashfs filesystem
* The following would work if we link to mksquashfs.o after we renamed 
* main() to mksquashfs_main() in mksquashfs.c but we don't want to actually do
* this because squashfs-tools is not under a permissive license
* i *nt sfs_mksquashfs(char *source, char *destination) {
* char *child_argv[5];
* child_argv[0] = NULL;
* child_argv[1] = source;
* child_argv[2] = destination;
* child_argv[3] = "-root-owned";
* child_argv[4] = "-noappend";
* mksquashfs_main(5, child_argv);
* }
*/

/* in-place modification of the string, and assuming the buffer pointed to by
* line is large enough to hold the resulting string*/
static void replacestr(char *line, const char *search, const char *replace)
{
    char *sp = NULL;
    
    if ((sp = strstr(line, search)) == NULL) {
        return;
    }
    int search_len = strlen(search);
    int replace_len = strlen(replace);
    int tail_len = strlen(sp+search_len);
    
    memmove(sp+replace_len,sp+search_len,tail_len+1);
    memcpy(sp, replace, replace_len);
    
    /* Do it recursively again until no more work to do */
    
    if ((sp = strstr(line, search))) {
        replacestr(line, search, replace);
    }
}

int count_archs(bool* archs) {
    int countArchs = 0;
    int i;
    for (i = 0; i < 4; i++) {
        countArchs += archs[i];
    }
    return countArchs;
}

gchar* archToName(fARCH arch) {
    switch (arch) {
        case fARCH_aarch64:
            return "aarch64";
        case fARCH_armhf:
            return "armhf";
        case fARCH_i686:
            return "i686";
        case fARCH_x86_64:
            return "x86_64";
    }
}

gchar* getArchName(bool* archs) {
    if (archs[fARCH_i686])
        return archToName(fARCH_i686);
    else if (archs[fARCH_x86_64])
        return archToName(fARCH_x86_64);
    else if (archs[fARCH_armhf])
        return archToName(fARCH_armhf);
    else if (archs[fARCH_aarch64])
        return archToName(fARCH_aarch64);
    else
        return "all";
}

void extract_arch_from_e_machine_field(int16_t e_machine, const gchar* sourcename, bool* archs) {
    if (e_machine == 3) {
        archs[fARCH_i686] = 1;
        if(verbose)
            fprintf(stderr, "%s used for determining architecture %s\n", sourcename, archToName(fARCH_i686));
    }

    if (e_machine == 62) {
        archs[fARCH_x86_64] = 1;
        if(verbose)
            fprintf(stderr, "%s used for determining architecture %s\n", sourcename, archToName(fARCH_x86_64));
    }

    if (e_machine == 40) {
        archs[fARCH_armhf] = 1;
        if(verbose)
            fprintf(stderr, "%s used for determining architecture %s\n", sourcename, archToName(fARCH_armhf));
    }

    if (e_machine == 183) {
        archs[fARCH_aarch64] = 1;
        if(verbose)
            fprintf(stderr, "%s used for determining architecture %s\n", sourcename, archToName(fARCH_aarch64));
    }
}

void extract_arch_from_text(gchar *archname, const gchar* sourcename, bool* archs) {
    if (archname) {
        archname = g_strstrip(archname);
        if (archname) {
            replacestr(archname, "-", "_");
            replacestr(archname, " ", "_");
            if (g_ascii_strncasecmp("i386", archname, 5) == 0
                    || g_ascii_strncasecmp("i486", archname, 5) == 0
                    || g_ascii_strncasecmp("i586", archname, 5) == 0
                    || g_ascii_strncasecmp("i686", archname, 5) == 0
                    || g_ascii_strncasecmp("intel_80386", archname, 12) == 0
                    || g_ascii_strncasecmp("intel_80486", archname, 12) == 0
                    || g_ascii_strncasecmp("intel_80586", archname, 12) == 0
                    || g_ascii_strncasecmp("intel_80686", archname, 12) == 0
                    ) {
                archs[fARCH_i686] = 1;
                if (verbose)
                    fprintf(stderr, "%s used for determining architecture i386\n", sourcename);
            } else if (g_ascii_strncasecmp("x86_64", archname, 7) == 0) {
                archs[fARCH_x86_64] = 1;
                if (verbose)
                    fprintf(stderr, "%s used for determining architecture x86_64\n", sourcename);
            } else if (g_ascii_strncasecmp("arm", archname, 4) == 0 ||
                       g_ascii_strncasecmp("armhf", archname, 6) == 0) {
                archs[fARCH_armhf] = 1;
                if (verbose)
                    fprintf(stderr, "%s used for determining architecture ARM\n", sourcename);
            } else if (g_ascii_strncasecmp("arm_aarch64", archname, 12) == 0 ||
                       g_ascii_strncasecmp("aarch64", archname, 8) == 0) {
                archs[fARCH_aarch64] = 1;
                if (verbose)
                    fprintf(stderr, "%s used for determining architecture ARM aarch64\n", sourcename);
            }
        }
    }
}

int16_t read_elf_e_machine_field(const gchar* file_path) {
    int16_t e_machine = 0x00;
    FILE* file = 0;
    file = fopen(file_path, "rb");
    if (file) {
        fseek(file, 0x12, SEEK_SET);
        fread(&e_machine, sizeof(e_machine), 1, file);
        fclose(file);
        e_machine = GINT16_FROM_LE(e_machine);
    }

    return e_machine;
}

void guess_arch_of_file(const gchar *archfile, bool* archs) {
    int16_t e_machine_field = read_elf_e_machine_field(archfile);
    extract_arch_from_e_machine_field(e_machine_field, archfile, archs);
}

void find_arch(const gchar *real_path, const gchar *pattern, bool* archs) {
    GDir *dir;
    gchar *full_name;
    dir = g_dir_open(real_path, 0, NULL);
    if (dir != NULL) {
        const gchar *entry;
        while ((entry = g_dir_read_name(dir)) != NULL) {
            full_name = g_build_filename(real_path, entry, NULL);
            if (g_file_test(full_name, G_FILE_TEST_IS_SYMLINK)) {
            } else if (g_file_test(full_name, G_FILE_TEST_IS_DIR)) {
                find_arch(full_name, pattern, archs);
            } else if (g_file_test(full_name, G_FILE_TEST_IS_EXECUTABLE) || g_pattern_match_simple(pattern, entry) ) {
                guess_arch_of_file(full_name, archs);
            }
        }
        g_dir_close(dir);
    }
    else {
        g_warning("%s: %s", real_path, g_strerror(errno));
    }
}

gchar* find_first_matching_file_nonrecursive(const gchar *real_path, const gchar *pattern) {
    GDir *dir;
    gchar *full_name;
    dir = g_dir_open(real_path, 0, NULL);
    if (dir != NULL) {
        const gchar *entry;
        while ((entry = g_dir_read_name(dir)) != NULL) {
            full_name = g_build_filename(real_path, entry, NULL);
            if (g_file_test(full_name, G_FILE_TEST_IS_REGULAR)) {
                if(g_pattern_match_simple(pattern, entry))
                    return(full_name);
            }
        }
        g_dir_close(dir);
    }
    else { 
        g_warning("%s: %s", real_path, g_strerror(errno));
    }
    return NULL;
}

gchar* get_desktop_entry(GKeyFile *kf, char *key) {
    gchar *value = g_key_file_get_string (kf, "Desktop Entry", key, NULL);
    if (! value){
        fprintf(stderr, "%s entry not found in desktop file\n", key);
    }
    return value;
}

bool readFile(char* filename, size_t* size, char** buffer) {
    FILE* f = fopen(filename, "rb");
    if (f==NULL) {
        *buffer = 0;
        *size = 0;
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *indata = malloc(fsize);
    fread(indata, fsize, 1, f);
    fclose(f);
    *size = (int)fsize;
    *buffer = indata; 
    return TRUE;
}

/* run a command outside the current appimage, block environs like LD_LIBRARY_PATH */
int run_external(const char *filename, char *const argv []) {
    int pid = fork();
    if (pid < 0) {
        g_print("run_external: fork failed");
        // fork failed
        exit(1);
    } else if (pid == 0) {
        // blocks env defined in resources/AppRun
        unsetenv("LD_LIBRARY_PATH");
        unsetenv("PYTHONPATH");
        unsetenv("XDG_DATA_DIRS");
        unsetenv("PERLLIB");
        unsetenv("GSETTINGS_SCHEMA_DIR");
        unsetenv("QT_PLUGIN_PATH");
        // runs command
        execv(filename, argv);
        // execv(3) returns, indicating error
        g_print("run_external: subprocess execv(3) got error %s", g_strerror(errno));
        exit(1);
    } else {
        int wstatus;
        if (waitpid(pid, &wstatus, 0) == -1) {
            g_print("run_external: wait failed");
            return -1;
        }
        if (WIFEXITED(wstatus) && (WEXITSTATUS(wstatus) == 0)) {
            return 0;
        } else {
            g_print("run_external: subprocess exited with status %d", WEXITSTATUS(wstatus));
            return 1;
        }
    }
}

// #####################################################################

static GOptionEntry entries[] =
{
    { "list", 'l', 0, G_OPTION_ARG_NONE, &list, "List files in SOURCE AppImage", NULL },
    { "updateinformation", 'u', 0, G_OPTION_ARG_STRING, &updateinformation, "Embed update information STRING; if zsyncmake is installed, generate zsync file", NULL },
    { "guess", 'g', 0, G_OPTION_ARG_NONE, &guess_update_information, "Guess update information based on GitHub or GitLab environment variables", NULL },
    { "version", 0, 0, G_OPTION_ARG_NONE, &showVersionOnly, "Show version number", NULL },
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Produce verbose output", NULL },
    { "sign", 's', 0, G_OPTION_ARG_NONE, &sign, "Sign with gpg[2]", NULL },
    { "comp", 0, 0, G_OPTION_ARG_STRING, &sqfs_comp, "Squashfs compression (default: zstd)", NULL },
    { "mksquashfs-opt", 0, 0, G_OPTION_ARG_STRING_ARRAY, &sqfs_opts, "Argument to pass through to mksquashfs; can be specified multiple times", NULL },
    { "no-appstream", 'n', 0, G_OPTION_ARG_NONE, &no_appstream, "Do not check AppStream metadata", NULL },
    { "no-check-permissions", 0, 0, G_OPTION_ARG_NONE, &no_check_permissions, "Do not check that AppDir files are readable and AppRun is executable for everyone", NULL },
    { "exclude-file", 0, 0, G_OPTION_ARG_STRING, &exclude_file, _exclude_file_desc, NULL },
    { "runtime-file", 0, 0, G_OPTION_ARG_STRING, &runtime_file, "Runtime file to use", NULL },
    { "sign-key", 0, 0, G_OPTION_ARG_STRING, &sign_key, "Key ID to use for gpg[2] signatures", NULL},
    { "file-url", 0, 0, G_OPTION_ARG_STRING, &file_url, "URL of the AppImage file, can be relative to zsync, or absolute/full", NULL },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &remaining_args, NULL, NULL },
    { 0,0,0,0,0,0,0 }
};

int
main (int argc, char *argv[])
{

    /* Parse Travis CI environment variables. 
     * https://docs.travis-ci.com/user/environment-variables/#Default-Environment-Variables
     * TRAVIS_COMMIT: The commit that the current build is testing.
     * TRAVIS_REPO_SLUG: The slug (in form: owner_name/repo_name) of the repository currently being built.
     * TRAVIS_TAG: If the current build is for a git tag, this variable is set to the tag’s name.
     * We cannot use g_environ_getenv (g_get_environ() since it is too new for CentOS 6 */
    // char* travis_commit;
    // travis_commit = getenv("TRAVIS_COMMIT");
    char* travis_repo_slug;
    travis_repo_slug = getenv("TRAVIS_REPO_SLUG");
    char* travis_tag;
    travis_tag = getenv("TRAVIS_TAG");
    char* travis_pull_request;
    travis_pull_request = getenv("TRAVIS_PULL_REQUEST");

    /* Parse GitHub Actions environment variables.
     * https://docs.github.com/en/actions/learn-github-actions/variables
     * GITHUB_REPOSITORY: The owner and repository name. For example, octocat/Hello-World.
     * GITHUB_REPOSITORY_OWNER: The repository owner's name. For example, octocat.
     */

    /* https://github.com/probonopd/uploadtool */
    char* github_token;
    github_token = getenv("GITHUB_TOKEN");

    // Construct the repository name from github_repository and github_repository_owner
    // by removing the github_repository_owner from the beginning of github_repository
    // and the slash that follows it
    char* github_repository = getenv("GITHUB_REPOSITORY");
    char* github_repository_owner = getenv("GITHUB_REPOSITORY_OWNER");
    char* github_repository_name = NULL;
    if (github_repository_owner != NULL && github_repository != NULL) {
        char* owner_start = strstr(github_repository, github_repository_owner);
        if (owner_start != NULL) {
            owner_start += strlen(github_repository_owner);
            // Skip the '/'
            if (*owner_start == '/')
                owner_start++;
            github_repository_name = owner_start;
        }
    }

    /* Parse GitLab CI environment variables.
     * https://docs.gitlab.com/ee/ci/variables/#predefined-variables-environment-variables
     * echo "${CI_PROJECT_URL}/-/jobs/artifacts/${CI_COMMIT_REF_NAME}/raw/QtQuickApp-x86_64.AppImage?job=${CI_JOB_NAME}"
     */    
    char* CI_PROJECT_URL;
    CI_PROJECT_URL = getenv("CI_PROJECT_URL");
    char* CI_COMMIT_REF_NAME;
    CI_COMMIT_REF_NAME = getenv("CI_COMMIT_REF_NAME"); // The branch or tag name for which project is built
    char* CI_JOB_NAME;
    CI_JOB_NAME = getenv("CI_JOB_NAME"); // The name of the job as defined in .gitlab-ci.yml
    
    /* Parse OWD environment variable.
     * If it is available then cd there. It is the original CWD prior to running AppRun */
    char* owd_env = NULL;
    owd_env = getenv("OWD");    
    if(NULL!=owd_env){
        int ret;
        ret = chdir(owd_env);
        if (ret != 0){
            fprintf(stderr, "Could not cd into %s\n", owd_env);
            exit(1);
        }
    }

    /* Parse runtime file environment variable.
     * If --runtime-file is specified, it takes precedence over this. */
    runtime_file = getenv("APPIMAGETOOL_RUNTIME_FILE");

    GError *error = NULL;
    GOptionContext *context;

    // initialize help text of argument
    sprintf(_exclude_file_desc, "Uses given file as exclude file for mksquashfs, in addition to %s.", APPIMAGEIGNORE);
    
    context = g_option_context_new ("SOURCE [DESTINATION] - Generate AppImages from existing AppDirs");
    g_option_context_add_main_entries (context, entries, NULL);
    // g_option_context_add_group (context, gtk_get_option_group (TRUE));
    if (!g_option_context_parse (context, &argc, &argv, &error))
    {
        fprintf(stderr, "Option parsing failed: %s\n", error->message);
        exit(1);
    }

    if (file_url && strlen(file_url) == 0) 
        die("--file-url argument is empty");

    fprintf(
        showVersionOnly ? stdout : stderr,
        "appimagetool, %s (git version %s), build %s built on %s\n",
        RELEASE_NAME, GIT_VERSION, BUILD_NUMBER, BUILD_DATE
    );

    // always show version, but exit immediately if only the version number was requested
    if (showVersionOnly)
        exit(0);

    /* Check for dependencies here. Better fail early if they are not present. */
    if(! g_find_program_in_path ("file"))
        die("file command is missing but required, please install it");
#ifndef AUXILIARY_FILES_DESTINATION
    if(! g_find_program_in_path ("mksquashfs"))
        die("mksquashfs command is missing but required, please install it");
#else
    {
        // build path relative to appimagetool binary
        char *appimagetoolDirectory = dirname(realpath("/proc/self/exe", NULL));
        if (!appimagetoolDirectory) {
            g_print("Could not access /proc/self/exe\n");
            exit(1);
        }

        pathToMksquashfs = g_build_filename(appimagetoolDirectory, "..", AUXILIARY_FILES_DESTINATION, "mksquashfs", NULL);

        if (!g_file_test(pathToMksquashfs, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_EXECUTABLE)) {
            g_printf("No such file or directory: %s\n", pathToMksquashfs);
            g_free(pathToMksquashfs);
            exit(1);
        }
    }
#endif
    if(! g_find_program_in_path ("desktop-file-validate"))
        die("desktop-file-validate command is missing, please install it");
    if(! g_find_program_in_path ("zsyncmake"))
        g_print("WARNING: zsyncmake command is missing, please install it if you want to use binary delta updates\n");
    if(! no_appstream)
        if(! g_find_program_in_path ("appstreamcli"))
            g_print("WARNING: appstreamcli command is missing, please install it if you want to use AppStream metadata\n");
    if(! g_find_program_in_path ("gpg2") && ! g_find_program_in_path ("gpg"))
        g_print("WARNING: gpg2 or gpg command is missing, please install it if you want to create digital signatures\n");
    if(! g_find_program_in_path ("sha256sum") && ! g_find_program_in_path ("shasum"))
        g_print("WARNING: sha256sum or shasum command is missing, please install it if you want to create digital signatures\n");
    
    if(!&remaining_args[0])
        die("SOURCE is missing");
    
    /* If the first argument is a directory, then we assume that we should package it */
    if (g_file_test(remaining_args[0], G_FILE_TEST_IS_DIR)) {
        /* Parse VERSION environment variable.
 * We cannot use g_environ_getenv (g_get_environ() since it is too new for CentOS 6
 * Also, if VERSION is not set and -g is called and if git is on the path, use
 * git rev-parse --short HEAD
 * TODO: Might also want to somehow make use of
 * git rev-parse --abbrev-ref HEAD
 * git log -1 --format=%ci */
        gchar* version_env = getenv("VERSION");

        if (guess_update_information) {
            char* gitPath = g_find_program_in_path("git");

            if (gitPath != NULL) {
                if (version_env == NULL) {
                    GError* error = NULL;
                    gchar* out = NULL;

                    char command_line[] = "git rev-parse --short HEAD";

                    // *not* the exit code! must be interpreted via g_spawn_check_exit_status!
                    int exit_status = -1;

                    // g_spawn_command_line_sync returns whether the program succeeded
                    // its return value is buggy, hence we're using g_spawn_check_exit_status to check for errors
                    g_spawn_command_line_sync(command_line, &out, NULL, &exit_status, &error);

                    // g_spawn_command_line_sync might have set error already, in that case we don't want to overwrite
                    if (error != NULL || !g_spawn_check_exit_status(exit_status, &error)) {
                        if (error == NULL) {
                            g_printerr("Failed to run 'git rev-parse --short HEAD, but failed to interpret GLib error state: %d\n", exit_status);
                        } else {
                            g_printerr("Failed to run 'git rev-parse --short HEAD: %s (code %d)\n", error->message, error->code);
                        }
                    } else {
                        version_env = g_strstrip(out);

                        if (version_env != NULL) {
                            g_printerr("NOTE: Using the output of 'git rev-parse --short HEAD' as the version:\n");
                            g_printerr("      %s\n", version_env);
                            g_printerr("      Please set the $VERSION environment variable if this is not intended\n");
                        }
                    }
                }
            }

            free(gitPath);
        }

        char *destination;
        char source[PATH_MAX];
        realpath(remaining_args[0], source);
        
        /* Check if *.desktop file is present in source AppDir */
        gchar *desktop_file = find_first_matching_file_nonrecursive(source, "*.desktop");
        if(desktop_file == NULL){
            die("Desktop file not found, aborting");
        }
        if(verbose)
            fprintf (stdout, "Desktop file: %s\n", desktop_file);

        if(g_find_program_in_path ("desktop-file-validate")) {
            if(validate_desktop_file(desktop_file) != 0){
                fprintf(stderr, "ERROR: Desktop file contains errors. Please fix them. Please see\n");
                fprintf(stderr, "       https://specifications.freedesktop.org/desktop-entry-spec/latest/index.html");
                die("       for more information.");
            }
        }

        /* Read information from .desktop file */
        GKeyFile *kf = g_key_file_new ();
        if (!g_key_file_load_from_file (kf, desktop_file, G_KEY_FILE_KEEP_TRANSLATIONS | G_KEY_FILE_KEEP_COMMENTS, NULL))
            die(".desktop file cannot be parsed");
        if (!get_desktop_entry(kf, "Categories"))
            die(".desktop file is missing a Categories= key");
        
        if(verbose){
            fprintf (stderr,"Name: %s\n", get_desktop_entry(kf, "Name"));
            fprintf (stderr,"Icon: %s\n", get_desktop_entry(kf, "Icon"));
            fprintf (stderr,"Exec: %s\n", get_desktop_entry(kf, "Exec"));
            fprintf (stderr,"Comment: %s\n", get_desktop_entry(kf, "Comment"));
            fprintf (stderr,"Type: %s\n", get_desktop_entry(kf, "Type"));
            fprintf (stderr,"Categories: %s\n", get_desktop_entry(kf, "Categories"));
        }

        /* Determine the architecture */
        bool archs[4] = {0, 0, 0, 0};
        extract_arch_from_text(getenv("ARCH"), "Environmental variable ARCH", archs);
        if (count_archs(archs) != 1) {
            /* If no $ARCH variable is set check a file */
            /* We use the next best .so that we can find to determine the architecture */
            find_arch(source, "*.so.*", archs);
            int countArchs = count_archs(archs);
            if (countArchs != 1) {
                if (countArchs < 1)
                    fprintf(stderr, "Unable to guess the architecture of the AppDir source directory \"%s\"\n", remaining_args[0]);
                else
                    fprintf(stderr, "More than one architectures were found of the AppDir source directory \"%s\"\n", remaining_args[0]);
                fprintf(stderr, "A valid architecture with the ARCH environmental variable should be provided\ne.g. ARCH=x86_64 %s", argv[0]),
                        die(" ...");
            }
        }
        gchar* arch = getArchName(archs);
        fprintf(stderr, "Using architecture %s\n", arch);

        char app_name_for_filename[PATH_MAX];
        {
            const char* const env_app_name = getenv("APPIMAGETOOL_APP_NAME");
            if (env_app_name != NULL) {
                fprintf(stderr, "Using user-specified app name: %s\n", env_app_name);
                strncpy(app_name_for_filename, env_app_name, PATH_MAX);
            } else {
                const gchar* const desktop_file_app_name = get_desktop_entry(kf, "Name");
                sprintf(app_name_for_filename, "%s", desktop_file_app_name);
                replacestr(app_name_for_filename, " ", "_");

                if (verbose) {
                    fprintf(stderr, "Using app name extracted from desktop file: %s\n", app_name_for_filename);
                }
            }
        }
        
        if (remaining_args[1]) {
            destination = remaining_args[1];
        } else {
            /* No destination has been specified, to let's construct one
            * TODO: Find out the architecture and use a $VERSION that might be around in the env */
            char dest_path[PATH_MAX];

            // if $VERSION is specified, we embed it into the filename
            if (version_env != NULL) {
                sprintf(dest_path, "%s-%s-%s.AppImage", app_name_for_filename, version_env, arch);
            } else {
                sprintf(dest_path, "%s-%s.AppImage", app_name_for_filename, arch);
            }

            destination = strdup(dest_path);
            replacestr(destination, " ", "_");
        }

        // if $VERSION is specified, we embed its value into the desktop file
        if (version_env != NULL) {
            g_key_file_set_string(kf, G_KEY_FILE_DESKTOP_GROUP, "X-AppImage-Version", version_env);

            if (!g_key_file_save_to_file(kf, desktop_file, NULL)) {
                fprintf(stderr, "Could not save modified desktop file\n");
                exit(1);
            }
        }

        fprintf (stdout, "%s should be packaged as %s\n", source, destination);
        /* Check if the Icon file is how it is expected */
        gchar* icon_name = get_desktop_entry(kf, "Icon");
        gchar* icon_file_path = NULL;
        gchar* icon_file_png;
        gchar* icon_file_xpm;
        /* No .svg: SVG is hard to parse, so consumers of .DirIcon (file managers,
         * thumbnailers) cannot be expected to handle it reliably; PNG is preferred,
         * XPM is the last resort */
        icon_file_png = g_strdup_printf("%s/%s.png", source, icon_name);
        icon_file_xpm = g_strdup_printf("%s/%s.xpm", source, icon_name);
        if (g_file_test(icon_file_png, G_FILE_TEST_IS_REGULAR)) {
            icon_file_path = icon_file_png;
        } else if(g_file_test(icon_file_xpm, G_FILE_TEST_IS_REGULAR)) {
            icon_file_path = icon_file_xpm;
        }

        /* Prefer a .DirIcon derived from the desktop file; a real file already
         * there is kept, any other pre-existing one (symlink) is replaced */
        gchar *diricon_path = g_build_filename(source, ".DirIcon", NULL);
        gboolean diricon_is_real_file = g_file_test(diricon_path, G_FILE_TEST_IS_REGULAR)
            && !g_file_test(diricon_path, G_FILE_TEST_IS_SYMLINK);
        if (icon_file_path != NULL && !diricon_is_real_file) {
            if (g_file_test(diricon_path, G_FILE_TEST_IS_SYMLINK) && g_unlink(diricon_path) != 0)
                die("Could not remove pre-existing .DirIcon symlink");
            fprintf (stderr, "Creating .DirIcon symlink based on information from desktop file\n");
            if (symlink(basename(icon_file_path), diricon_path) != 0)
                die("Could not symlink .DirIcon");
        }

        /* Errors if .DirIcon is missing, broken, not a file or an SVG; each one
         * says what is wrong and how to fix it */
        appdir_icon_file = icon_file_path != NULL ? g_strdup(icon_file_path) : NULL;
        check_diricon(source, icon_name, icon_file_path);

        /* Check if AppStream upstream metadata is present in source AppDir */
        if(! no_appstream){
            char application_id[PATH_MAX];
            sprintf (application_id,  "%s", basename(desktop_file));
            replacestr(application_id, ".desktop", ".appdata.xml");
            gchar *appdata_path = g_build_filename(source, "/usr/share/metainfo/", application_id, NULL);
            if (! g_file_test(appdata_path, G_FILE_TEST_IS_REGULAR)){
                fprintf (stderr, "WARNING: AppStream upstream metadata is missing, please consider creating it\n");
                fprintf (stderr, "         in usr/share/metainfo/%s\n", application_id);
                fprintf (stderr, "         Please see https://www.freedesktop.org/software/appstream/docs/chap-Quickstart.html#sect-Quickstart-DesktopApps\n");
                fprintf (stderr, "         for more information or use the generator at\n");
                fprintf (stderr, "         https://docs.appimage.org/packaging-guide/optional/appstream.html#using-the-appstream-generator\n");
            } else {
                fprintf (stderr, "AppStream upstream metadata found in usr/share/metainfo/%s\n", application_id);
                /* Use ximion's appstreamcli to make sure that desktop file and appdata match together */
                if(g_find_program_in_path ("appstreamcli")) {
                    char *args[] = {
                        "appstreamcli",
                        "validate-tree",
                        source,
                        NULL
                    };
                    g_print("Trying to validate AppStream information with the appstreamcli tool\n");
                    g_print("In case of issues, please refer to https://github.com/ximion/appstream\n");
                    int ret = run_external(g_find_program_in_path ("appstreamcli"), args);
                    if (ret != 0)
                        die("Failed to validate AppStream information with appstreamcli");
                }
                /* It seems that hughsie's appstream-util does additional validations */
                if(g_find_program_in_path ("appstream-util")) {
                    char *args[] = {
                        "appstream-util",
                        "validate-relax",
                        appdata_path,
                        NULL
                    };
                    g_print("Trying to validate AppStream information with the appstream-util tool\n");
                    g_print("In case of issues, please refer to https://github.com/hughsie/appstream-glib\n");
                    int ret = run_external(g_find_program_in_path ("appstream-util"), args);
                    if (ret != 0)
                        die("Failed to validate AppStream information with appstream-util");
                }
            }
        }
        
        /* Upstream mksquashfs can currently not start writing at an offset,
        * so we need a patched one. https://github.com/plougher/squashfs-tools/pull/13
        * should hopefully change that. */

        fprintf (stderr, "Generating squashfs...\n");
        size_t size = 0;
        char* data = NULL;
        // TODO: just write to the output file directly, we don't really need a memory buffer
        if (runtime_file != NULL) {
            if (!readFile(runtime_file, &size, &data)) {
                die("Unable to load provided runtime file");
            }
        } else {
            if (!fetch_runtime(arch, &size, &data, verbose)) {
                die(
                    "Failed to download runtime file, please download the runtime manually from "
                    "https://github.com/AppImage/type2-runtime/releases and pass it to appimagetool with "
                    "--runtime-file"
                );
            }
        }
        if (verbose)
            printf("Size of the embedded runtime: %d bytes\n", size);
        
        if (!no_check_permissions)
            check_appdir_permissions(source);
        int result = sfs_mksquashfs(source, destination, size);
        if(result != 0)
            die("sfs_mksquashfs error");
        
        fprintf (stderr, "Embedding ELF...\n");
        FILE *fpdst = fopen(destination, "rb+");
        if (fpdst == NULL) {
            die("Not able to open the AppImage for writing, aborting");
        }

        fseek(fpdst, 0, SEEK_SET);
        fwrite(data, size, 1, fpdst);
        fclose(fpdst);
        // TODO: avoid memory buffer (see above)
        free(data);

        fprintf (stderr, "Marking the AppImage as executable...\n");
        if (chmod (destination, 0755) < 0) {
            printf("Could not set executable bit, aborting\n");
            exit(1);
        }
        
        /* If the user has not provided update information but we know this is a CI build,
         * then fill in update information based on well-known CI environment variables */
        if(guess_update_information){

            if(github_repository_name){
                if(!github_token) {
                    printf("Will not guess update information since $GITHUB_TOKEN is missing\n");
                } else {
                    gchar *zsyncmake_path = g_find_program_in_path ("zsyncmake");
                    if(zsyncmake_path){
                        char buf[1024];
                        // gh-releases-zsync|probono|AppImages|latest|Subsurface-*x86_64.AppImage.zsync
                        int ret = snprintf(buf, sizeof(buf), "gh-releases-zsync|%s|%s|latest|%s*-%s.AppImage.zsync", github_repository_owner, github_repository_name, app_name_for_filename, arch);
                        if (ret < 0) {
                            die("snprintf error");
                        } else if (ret >= sizeof(buf)) {
                            die("snprintf buffer overflow");
                        }
                        updateinformation = buf;
                        printf("Guessing update information based on $GITHUB_REPOSITORY=%s\n", github_repository);
                        printf("%s\n", updateinformation);
                    } else {
                        printf("Will not guess update information since zsyncmake is missing\n");
                    }
                }
            } else if(travis_repo_slug){
                if(!github_token) {
                    printf("Will not guess update information since $GITHUB_TOKEN is missing,\n");
                    if(0 != strcmp(travis_pull_request, "false")){
                        printf("please set it in the Travis CI Repository Settings for this project.\n");
                        printf("You can get one from https://github.com/settings/tokens\n");
                    } else {
                        printf("which is expected since this is a pull request\n");
                    }
                } else {
                    gchar *zsyncmake_path = g_find_program_in_path ("zsyncmake");
                    if(zsyncmake_path){
                        char buf[1024];
                        gchar **parts = g_strsplit (travis_repo_slug, "/", 2);
                        /* https://github.com/AppImage/AppImageSpec/blob/master/draft.md#github-releases 
                         * gh-releases-zsync|probono|AppImages|latest|Subsurface*-x86_64.AppImage.zsync */
                        gchar *channel = "continuous";
                            if(travis_tag != NULL){
                                if((strcmp(travis_tag, "") != 0) && (strcmp(travis_tag, "continuous") != 0)) {
                                    channel = "latest";
                                }
                            }
                        int ret = snprintf(buf, sizeof(buf), "gh-releases-zsync|%s|%s|%s|%s*-%s.AppImage.zsync", parts[0], parts[1], channel, app_name_for_filename, arch);
                        if (ret < 0) {
                            die("snprintf error");
                        } else if (ret >= sizeof(buf)) {
                            die("snprintf buffer overflow");
                        }
                        updateinformation = buf;
                        printf("Guessing update information based on $TRAVIS_TAG=%s and $TRAVIS_REPO_SLUG=%s\n", travis_tag, travis_repo_slug);
                        printf("%s\n", updateinformation);
                    } else {
                        printf("Will not guess update information since zsyncmake is missing\n");
                    }
                }
            } else if(CI_COMMIT_REF_NAME){
                // ${CI_PROJECT_URL}/-/jobs/artifacts/${CI_COMMIT_REF_NAME}/raw/QtQuickApp-x86_64.AppImage?job=${CI_JOB_NAME}
                gchar *zsyncmake_path = g_find_program_in_path ("zsyncmake");
                if(zsyncmake_path){
                    char buf[1024];
                    int ret = snprintf(buf, sizeof(buf), "zsync|%s/-/jobs/artifacts/%s/raw/%s-%s.AppImage.zsync?job=%s", CI_PROJECT_URL, CI_COMMIT_REF_NAME, app_name_for_filename, arch, CI_JOB_NAME);
                    if (ret < 0) {
                        die("snprintf error");
                    } else if (ret >= sizeof(buf)) {
                        die("snprintf buffer overflow");
                    }
                    updateinformation = buf;
                    printf("Guessing update information based on $CI_COMMIT_REF_NAME=%s and $CI_JOB_NAME=%s\n", CI_COMMIT_REF_NAME, CI_JOB_NAME);
                    printf("%s\n", updateinformation);
                } else {
                    printf("Will not guess update information since zsyncmake is missing\n");
                }
            }
        }
        
        /* If updateinformation was provided, then we check and embed it */
        if(updateinformation != NULL){
            if(!g_str_has_prefix(updateinformation,"zsync|"))
                if(!g_str_has_prefix(updateinformation,"gh-releases-zsync|"))
                    if(!g_str_has_prefix(updateinformation,"pling-v1-zsync|"))
                        die("The provided updateinformation is not in a recognized format");
                
            gchar **ui_type = g_strsplit_set(updateinformation, "|", -1);
                        
            if(verbose)
                printf("updateinformation type: %s\n", ui_type[0]);
            /* TODO: Further checking of the updateinformation */
            
          
            unsigned long ui_offset = 0;
            unsigned long ui_length = 0;

            bool rv = appimage_get_elf_section_offset_and_length(destination, ".upd_info", &ui_offset, &ui_length);

            if (!rv || ui_offset == 0 || ui_length == 0) {
                die("Could not find section .upd_info in runtime");
            }

            if(verbose) {
                printf("ui_offset: %lu\n", ui_offset);
                printf("ui_length: %lu\n", ui_length);
            }
            if(ui_offset == 0) {
                die("Could not determine offset for updateinformation");
            } else {
                if(strlen(updateinformation)>ui_length)
                    die("updateinformation does not fit into segment, aborting");
                FILE *fpdst2 = fopen(destination, "r+");
                if (fpdst2 == NULL)
                    die("Not able to open the destination file for writing, aborting");
                fseek(fpdst2, ui_offset, SEEK_SET);
                // fseek(fpdst2, ui_offset, SEEK_SET);
                // fwrite(0x00, 1, 1024, fpdst); // FIXME: Segfaults; why?
                // fseek(fpdst, ui_offset, SEEK_SET);
                fwrite(updateinformation, strlen(updateinformation), 1, fpdst2);
                fclose(fpdst2);
            }
        }

        // calculate and embed MD5 digest
        {
            fprintf(stderr, "Embedding MD5 digest\n");

            unsigned long digest_md5_offset = 0;
            unsigned long digest_md5_length = 0;

            bool rv = appimage_get_elf_section_offset_and_length(destination, ".digest_md5", &digest_md5_offset, &digest_md5_length);

            if (!rv || digest_md5_offset == 0 || digest_md5_length == 0) {
                die("Could not find section .digest_md5 in runtime");
            }

            static const unsigned long section_size = 16;

            if (digest_md5_length < section_size) {
                fprintf(
                    stderr,
                    ".digest_md5 section in runtime's ELF header is too small"
                    "(found %lu bytes, minimum required: %lu bytes)\n",
                    digest_md5_length, section_size
                );
                exit(1);
            }

            char digest_buffer[section_size];

            if (!appimage_type2_digest_md5(destination, digest_buffer)) {
                die("Failed to calculate MD5 digest");
            }

            FILE* destinationfp = fopen(destination, "r+");

            if (destinationfp == NULL) {
                die("Failed to open AppImage for updating");
            }

            if (fseek(destinationfp, digest_md5_offset, SEEK_SET) != 0) {
                fclose(destinationfp);
                die("Failed to embed MD5 digest: could not seek to section offset");
            }

            if (fwrite(digest_buffer, sizeof(char), section_size, destinationfp) != section_size) {
                fclose(destinationfp);
                die("Failed to embed MD5 digest: write failed");
            }

            fclose(destinationfp);
        }

        if (sign) {
            if (!sign_appimage(destination, sign_key, verbose)) {
                die("Signing failed, aborting");
            }
        }

        /* If updateinformation was provided, then we also generate the zsync file (after having signed the AppImage) */
        if (updateinformation != NULL) {
            gchar* zsyncmake_path = g_find_program_in_path("zsyncmake");
            if (!zsyncmake_path) {
                fprintf(stderr, "zsyncmake is not installed/bundled, skipping\n");
            } else {
                fprintf(stderr, "zsyncmake is available and updateinformation is provided, "
                                "hence generating zsync file\n");

                // notice for Alpine builds: Alpine's getopt does not parse flags passed after the first parameter, order matters here
                const gchar* zsync_url_arg = file_url ? file_url : basename(destination);
                const gchar* const zsyncmake_command[] = {zsyncmake_path, "-u", zsync_url_arg, destination, NULL};

                if (verbose) {
                    fprintf(stderr, "Running zsyncmake process: ");
                    for (gint j = 0; j < (sizeof(zsyncmake_command) / sizeof(char*) - 1); ++j) {
                        fprintf(stderr, "'%s' ", zsyncmake_command[j]);
                    }
                    fprintf(stderr, "\n");
                }

                GSubprocessFlags flags = G_SUBPROCESS_FLAGS_NONE;

                if (!verbose) {
                    flags = G_SUBPROCESS_FLAGS_STDERR_SILENCE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE;
                }

                GSubprocess* proc = g_subprocess_newv(zsyncmake_command, flags, &error);

                if (proc == NULL) {
                    fprintf(stderr, "ERROR: failed to create zsyncmake process: %s\n", error->message);
                    exit(1);
                }

                if (!g_subprocess_wait_check(proc, NULL, &error)) {
                    fprintf(stderr, "ERROR: zsyncmake returned abnormal exit code: %s\n", error->message);
                    g_object_unref(proc);
                    exit(1);
                }

                g_object_unref(proc);
            }
        }

        fprintf(stderr, "Success\n\n");
        fprintf(stderr, "Please consider submitting your AppImage to AppImageHub, the crowd-sourced\n");
        fprintf(stderr, "central directory of available AppImages, by opening a pull request\n");
        fprintf(stderr, "at https://github.com/AppImage/appimage.github.io\n");

        return 0;
    } else if (g_file_test(remaining_args[0], G_FILE_TEST_IS_REGULAR)) {
        /* If the first argument is a regular file, then we assume that we should unpack it */
        fprintf(stdout, "%s is a file, assuming it is an AppImage and should be unpacked\n", remaining_args[0]);
        die("To be implemented");
        return 1;
    } else {
        fprintf(stderr, "Error: no such file or directory: %s\n", remaining_args[0]);
        return 1;
    }

    // should never be reached
    return 1;
}
