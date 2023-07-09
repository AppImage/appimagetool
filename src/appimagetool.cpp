/**************************************************************************
 * 
 * Copyright (c) 2004-23 Simon Peter
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

#include <stdlib.h>

// TODO: Remove glib dependency in favor of C++
#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

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
#include "elf.h"

// C++
#include <cstdlib>
#include <vector>
#include <iostream>
#include <sstream>
#include <string>

typedef enum {
    fARCH_i686,
    fARCH_x86_64,
    fARCH_armhf,
    fARCH_aarch64
} fARCH;

static gchar const APPIMAGEIGNORE[] = ".appimageignore";
static char _exclude_file_desc[256];

static bool list = false;
static bool verbose = false;
static bool showVersionOnly = false;
static bool sign = false;
static bool no_appstream = false;
gchar **remaining_args = NULL;
gchar *updateinformation = NULL;
static bool guess_update_information = false;
std::string sqfs_comp = "zstd";
gchar **sqfs_opts = NULL;
gchar *exclude_file = NULL;
gchar *runtime_file = NULL;
gchar *sign_key = NULL;
gchar *pathToMksquashfs = NULL;

// #####################################################################

static void die(const char *msg) {
    std::cerr << msg << std::endl;
    exit(1);
}

/* Generate a squashfs filesystem using mksquashfs on the $PATH 
* execlp(), execvp(), and execvpe() search on the $PATH */
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
            std::cerr << "mksquashfs (pid " << pid << ") exited with code " << retcode << std::endl;
            return(-1);
        }
        
        return 0;
    } else {
        // we are the child
        std::string offset_string = std::to_string(offset);

        unsigned int sqfs_opts_len = sqfs_opts ? g_strv_length(sqfs_opts) : 0;

        int max_num_args = sqfs_opts_len + 22;
        std::vector<std::string> args;

        #ifndef AUXILIARY_FILES_DESTINATION
            args.push_back("mksquashfs");
        #else
            args.push_back(pathToMksquashfs);
        #endif
            args.push_back(source);
            args.push_back(destination);
            args.push_back("-offset");
            args.push_back(offset_string);

            int i = args.size();

        args.push_back("-comp");
        args.push_back(sqfs_comp);

        args.push_back("-root-owned");
        args.push_back("-noappend");

        // compression-specific optimization
        if (sqfs_comp == "xz") {
            // https://jonathancarter.org/2015/04/06/squashfs-performance-testing/ says:
            // improved performance by using a 16384 block size with a sacrifice of around 3% more squashfs image space
            args.push_back("-Xdict-size");
            args.push_back("100%");
            args.push_back("-b");
            args.push_back("16384");
        } else if (sqfs_comp == "zstd") {
            /*
             * > Build with 1MiB block size
             * > Using a bigger block size than mksquashfs's default improves read speed and can produce smaller AppImages as well
             * -- https://github.com/probonopd/go-appimage/commit/c4a112e32e8c2c02d1d388c8fa45a9222a529af3
             */
            args.push_back("-b");
            args.push_back("1M");
        }

        // check if ignore file exists and use it if possible
        if (access(APPIMAGEIGNORE, F_OK) >= 0) {
            printf("Including %s", APPIMAGEIGNORE);
            args.push_back("-wildcards");
            args.push_back("-ef");

            // avoid warning: assignment discards ‘const’ qualifier
            char* buf = strdup(APPIMAGEIGNORE);
            args.push_back(buf);
        }

        // if an exclude file has been passed on the command line, should be used, too
        if (exclude_file != 0 && strlen(exclude_file) > 0) {
            if (access(exclude_file, F_OK) < 0) {
                printf("WARNING: exclude file %s not found!", exclude_file);
                return -1;
            }

            args.push_back("-wildcards");
            args.push_back("-ef");
            args.push_back(exclude_file);

        }

        args.push_back("-mkfs-time");
        args.push_back("0");

        for (unsigned int sqfs_opts_idx = 0; sqfs_opts_idx < sqfs_opts_len; ++sqfs_opts_idx) {
            args[i++] = sqfs_opts[sqfs_opts_idx];
        }
                    // Build the command string
                    std::stringstream cmdStream;
                    for (const auto& arg : args) {
                        // Quote each argument to handle spaces and special characters
                        cmdStream << "\"" << arg << "\" ";
                    }
                    if (verbose) {
                        std::cout << "Executing command: " << cmdStream.str() << std::endl;
                    }
                    if (std::system(cmdStream.str().c_str()) != 0) {
                        std::cerr << "ERROR: Failed to execute command: " << cmdStream.str() << std::endl;
                        exit(1);
                    }
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
            std::cout << sourcename << " used for determining architecture " << archToName(fARCH_i686) << std::endl;
    }

    if (e_machine == 62) {
        archs[fARCH_x86_64] = 1;
        if(verbose)
            std::cout << sourcename << " used for determining architecture " << archToName(fARCH_x86_64) << std::endl;
    }

    if (e_machine == 40) {
        archs[fARCH_armhf] = 1;
        if(verbose)
            std::cout << sourcename << " used for determining architecture " << archToName(fARCH_armhf) << std::endl;
    }

    if (e_machine == 183) {
        archs[fARCH_aarch64] = 1;
        if(verbose)
            std::cout << sourcename << " used for determining architecture " << archToName(fARCH_aarch64) << std::endl;
    }
}

void extract_arch_from_text(gchar *archname, const gchar* sourcename, bool* archs) {
    if (archname) {
        archname = g_strstrip(archname);
        if (archname) {
            replacestr(archname, "-", "_");
            replacestr(archname, " ", "_");
            if (g_ascii_strncasecmp("i386", archname, 20) == 0
                    || g_ascii_strncasecmp("i486", archname, 20) == 0
                    || g_ascii_strncasecmp("i586", archname, 20) == 0
                    || g_ascii_strncasecmp("i686", archname, 20) == 0
                    || g_ascii_strncasecmp("intel_80386", archname, 20) == 0
                    || g_ascii_strncasecmp("intel_80486", archname, 20) == 0
                    || g_ascii_strncasecmp("intel_80586", archname, 20) == 0
                    || g_ascii_strncasecmp("intel_80686", archname, 20) == 0
                    ) {
                archs[fARCH_i686] = 1;
                if (verbose)
                    std::cout << sourcename << " used for determining architecture i386" << std::endl;
            } else if (g_ascii_strncasecmp("x86_64", archname, 20) == 0) {
                archs[fARCH_x86_64] = 1;
                if (verbose)
                    std::cout << sourcename << " used for determining architecture x86_64" << std::endl;
            } else if (g_ascii_strncasecmp("arm", archname, 20) == 0) {
                archs[fARCH_armhf] = 1;
                if (verbose)
                    std::cout << sourcename << " used for determining architecture ARM" << std::endl;
            } else if (g_ascii_strncasecmp("arm_aarch64", archname, 20) == 0) {
                archs[fARCH_aarch64] = 1;
                if (verbose)
                    std::cerr << sourcename << " used for determining architecture ARM aarch64\n";
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
        fgets((char*) (&e_machine), 0x02, file);
        fclose(file);
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
        std::cout << key << " entry not found in desktop file" << std::endl;
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

    char *indata = (char*) malloc(fsize);
    fread(indata, fsize, 1, f);
    fclose(f);
    *size = (int)fsize;
    *buffer = indata; 
    return true;
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
    { "guess", 'g', 0, G_OPTION_ARG_NONE, &guess_update_information, "Guess update information based on Travis CI or GitLab environment variables", NULL },
    { "version", 0, 0, G_OPTION_ARG_NONE, &showVersionOnly, "Show version number", NULL },
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Produce verbose output", NULL },
    { "sign", 's', 0, G_OPTION_ARG_NONE, &sign, "Sign with gpg[2]", NULL },
    { "comp", 0, 0, G_OPTION_ARG_STRING, &sqfs_comp, "Squashfs compression (default: zstd", NULL },
    { "mksquashfs-opt", 0, 0, G_OPTION_ARG_STRING_ARRAY, &sqfs_opts, "Argument to pass through to mksquashfs; can be specified multiple times", NULL },
    { "no-appstream", 'n', 0, G_OPTION_ARG_NONE, &no_appstream, "Do not check AppStream metadata", NULL },
    { "exclude-file", 0, 0, G_OPTION_ARG_STRING, &exclude_file, _exclude_file_desc, NULL },
    { "runtime-file", 0, 0, G_OPTION_ARG_STRING, &runtime_file, "Runtime file to use", NULL },
    { "sign-key", 0, 0, G_OPTION_ARG_STRING, &sign_key, "Key ID to use for gpg[2] signatures", NULL},
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &remaining_args, NULL, NULL },
    { 0,0,0, G_OPTION_ARG_NONE, 0,0,0 },
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
    std::string travis_repo_slug;
    char* travis_repo_slug_cstr = getenv("TRAVIS_REPO_SLUG");
    if (travis_repo_slug_cstr != NULL) {
        travis_repo_slug = std::string(travis_repo_slug_cstr);
    }
    std::string travis_tag;
    char* travis_tag_cstr = getenv("TRAVIS_TAG");
    if (travis_tag_cstr != NULL) {
        travis_tag = std::string(travis_tag_cstr);
    }
    std::string travis_pull_request;
    char* travis_pull_request_cstr = getenv("TRAVIS_PULL_REQUEST");
    if (travis_pull_request_cstr != NULL) {
        travis_pull_request = std::string(travis_pull_request_cstr);
    }
    /* https://github.com/probonopd/uploadtool */
    std::string github_token;
    char* github_token_cstr = getenv("GITHUB_TOKEN");
    if (github_token_cstr != NULL) {
        github_token = std::string(github_token_cstr);
    }

    /* Parse GitLab CI environment variables.
     * https://docs.gitlab.com/ee/ci/variables/#predefined-variables-environment-variables
     * echo "${CI_PROJECT_URL}/-/jobs/artifacts/${CI_COMMIT_REF_NAME}/raw/QtQuickApp-x86_64.AppImage?job=${CI_JOB_NAME}"
     */    
    std::string CI_PROJECT_URL;
    char* CI_PROJECT_URL_cstr = getenv("CI_PROJECT_URL");
    if (CI_PROJECT_URL_cstr != NULL) {
        CI_PROJECT_URL = std::string(CI_PROJECT_URL_cstr);
    }
    std::string CI_COMMIT_REF_NAME;
    char* CI_COMMIT_REF_NAME_cstr = getenv("CI_COMMIT_REF_NAME");
    if (CI_COMMIT_REF_NAME_cstr != NULL) {
        CI_COMMIT_REF_NAME = std::string(CI_COMMIT_REF_NAME_cstr);
    }
    std::string CI_JOB_NAME;
    char* CI_JOB_NAME_cstr = getenv("CI_JOB_NAME");
    if (CI_JOB_NAME_cstr != NULL) {
        CI_JOB_NAME = std::string(CI_JOB_NAME_cstr);
    }
    
    /* Parse OWD environment variable.
     * If it is available then cd there. It is the original CWD prior to running AppRun */
    char* owd_env = NULL;
    owd_env = getenv("OWD");    
    if(NULL!=owd_env){
        int ret;
        ret = chdir(owd_env);
        if (ret != 0){
            std::cerr << "Could not cd into " << owd_env << std::endl;
            exit(1);
        }
    }
        
    GError *error = NULL;
    GOptionContext *context;

    // initialize help text of argument
    sprintf(_exclude_file_desc, "Uses given file as exclude file for mksquashfs, in addition to %s.", APPIMAGEIGNORE);
    
    context = g_option_context_new ("SOURCE [DESTINATION] - Generate AppImages from existing AppDirs");
    g_option_context_add_main_entries (context, entries, NULL);
    // g_option_context_add_group (context, gtk_get_option_group (TRUE));
    if (!g_option_context_parse (context, &argc, &argv, &error))
    {
        std::cerr << "Option parsing failed: " << error->message << std::endl;
        exit(1);
    }

    std::cerr << "appimagetool, " << RELEASE_NAME << " (git version " << GIT_VERSION << "), build " << BUILD_NUMBER << " built on " << BUILD_DATE << std::endl;

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
                            std::cerr << "Failed to run 'git rev-parse --short HEAD, but failed to interpret GLib error state: " << exit_status << std::endl;
                        } else {
                            std::cerr << "Failed to run 'git rev-parse --short HEAD: " << error->message << " (code " << error->code << ")" << std::endl;
                        }
                    } else {
                        version_env = g_strstrip(out);

                        if (version_env != NULL) {
                            std::cerr << "NOTE: Using the output of 'git rev-parse --short HEAD' as the version:" << std::endl;
                            std::cerr << "      " << version_env << std::endl;
                            std::cerr << "      Please set the $VERSION environment variable if this is not intended" << std::endl;
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
            std::cout << "Desktop file: " << desktop_file << std::endl;

        if(g_find_program_in_path ("desktop-file-validate")) {
            if(validate_desktop_file(desktop_file) != 0){
                std::cerr << "ERROR: Desktop file contains errors. Please fix them. Please see" << std::endl;
                std::cerr << "       https://standards.freedesktop.org/desktop-entry-spec/1.0/n" << std::endl;
                die("       for more information.");
            }
        }

        /* Read information from .desktop file */
        GKeyFile *kf = g_key_file_new ();
        GKeyFileFlags flags = (GKeyFileFlags) (G_KEY_FILE_KEEP_TRANSLATIONS | G_KEY_FILE_KEEP_COMMENTS);
        if (!g_key_file_load_from_file (kf, desktop_file, flags, NULL))
            die(".desktop file cannot be parsed");
        if (!get_desktop_entry(kf, "Categories"))
            die(".desktop file is missing a Categories= key");
        if(verbose){
            std::cerr << "Name: " << get_desktop_entry(kf, "Name") << std::endl;
            std::cerr << "Icon: " << get_desktop_entry(kf, "Icon") << std::endl;
            std::cerr << "Exec: " << get_desktop_entry(kf, "Exec") << std::endl;
            std::cerr << "Comment: " << get_desktop_entry(kf, "Comment") << std::endl;
            std::cerr << "Type: " << get_desktop_entry(kf, "Type") << std::endl;
            std::cerr << "Categories: " << get_desktop_entry(kf, "Categories") << std::endl;
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
                    std::cerr << "Unable to guess the architecture of the AppDir source directory \"" << remaining_args[0] << "\"" << std::endl;
                else
                    std::cerr << "More than one architectures were found of the AppDir source directory \"" << remaining_args[0] << "\"" << std::endl;
                std::cerr << "A valid architecture with the ARCH environmental variable should be provided\ne.g. ARCH=x86_64 " << argv[0] << std::endl;
                        die(" ...");
            }
        }
        gchar* arch = getArchName(archs);
        std::cerr << "Using architecture " << arch << std::endl;

        char app_name_for_filename[PATH_MAX];
        {
            const char* const env_app_name = getenv("APPIMAGETOOL_APP_NAME");
            if (env_app_name != NULL) {
                std::cerr << "Using user-specified app name: " << env_app_name << std::endl;
                strncpy(app_name_for_filename, env_app_name, PATH_MAX);
            } else {
                const gchar* const desktop_file_app_name = get_desktop_entry(kf, "Name");
                sprintf(app_name_for_filename, "%s", desktop_file_app_name);
                replacestr(app_name_for_filename, " ", "_");

                if (verbose) {
                    std::cerr << "Using app name extracted from desktop file: " << app_name_for_filename << std::endl;
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
                std::cerr << "Could not save modified desktop file" << std::endl;
                exit(1);
            }
        }

        std::cout << source << " should be packaged as " << destination << std::endl;
        /* Check if the Icon file is how it is expected */
        gchar* icon_name = get_desktop_entry(kf, "Icon");
        gchar* icon_file_path = NULL;
        gchar* icon_file_png;
        gchar* icon_file_svg;
        gchar* icon_file_xpm;
        icon_file_png = g_strdup_printf("%s/%s.png", source, icon_name);
        icon_file_svg = g_strdup_printf("%s/%s.svg", source, icon_name);
        icon_file_xpm = g_strdup_printf("%s/%s.xpm", source, icon_name);
        if (g_file_test(icon_file_png, G_FILE_TEST_IS_REGULAR)) {
            icon_file_path = icon_file_png;
        } else if(g_file_test(icon_file_svg, G_FILE_TEST_IS_REGULAR)) {
            icon_file_path = icon_file_svg;
        } else if(g_file_test(icon_file_xpm, G_FILE_TEST_IS_REGULAR)) {
            icon_file_path = icon_file_xpm;
        } else {
            std::cerr << icon_name << "{.png,.svg,.xpm} defined in desktop file but not found" << std::endl;
            std::cerr << "For example, you could put a 256x256 pixel png into" << std::endl;
            gchar *icon_name_with_png = g_strconcat(icon_name, ".png", NULL);
            gchar *example_path = g_build_filename(source, "/", icon_name_with_png, NULL);
            std::cerr << example_path << std::endl;
            exit(1);
        }
       
        /* Check if .DirIcon is present in source AppDir */
        gchar *diricon_path = g_build_filename(source, ".DirIcon", NULL);
        
        if (! g_file_test(diricon_path, G_FILE_TEST_EXISTS)){
            std::cerr << "Deleting pre-existing .DirIcon" << std::endl;
            g_unlink(diricon_path);
        }
        if (! g_file_test(diricon_path, G_FILE_TEST_IS_REGULAR)){
            std::cerr << "Creating .DirIcon symlink based on information from desktop file" << std::endl;
            int res = symlink(basename(icon_file_path), diricon_path);
            if(res)
                die("Could not symlink .DirIcon");
        }
        
        /* Check if AppStream upstream metadata is present in source AppDir */
        if(! no_appstream){
            char application_id[PATH_MAX];
            sprintf (application_id,  "%s", basename(desktop_file));
            replacestr(application_id, ".desktop", ".appdata.xml");
            gchar *appdata_path = g_build_filename(source, "/usr/share/metainfo/", application_id, NULL);
            if (! g_file_test(appdata_path, G_FILE_TEST_IS_REGULAR)){
                    std::cerr << "WARNING: AppStream upstream metadata is missing, please consider creating it" << std::endl;
                    std::cerr << "         in usr/share/metainfo/" << application_id << std::endl;
                    std::cerr << "         Please see https://www.freedesktop.org/software/appstream/docs/chap-Quickstart.html#sect-Quickstart-DesktopApps" << std::endl;
                    std::cerr << "         for more information or use the generator at http://output.jsbin.com/qoqukof." << std::endl;
                } else {
                    std::cerr << "AppStream upstream metadata found in usr/share/metainfo/" << application_id << std::endl;
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

        std::cerr << "Loading runtime file..." << std::endl;
        size_t size = 0;
        char* data = NULL;
        // TODO: just write to the output file directly, we don't really need a memory buffer
        if (runtime_file != NULL) {
            if (!readFile(runtime_file, &size, &data)) {
                die("Unable to load provided runtime file");
            }
        } else {
            if (!fetch_runtime(arch, &size, &data, verbose)) {
                die("Failed to download runtime file");
            }
        }
        if (verbose)
                std::cout << "Size of the embedded runtime: " << size << " bytes" << std::endl;
            
            std::cerr << "Generating squashfs..." << std::endl;
        int result = sfs_mksquashfs(source, destination, size);
        if(result != 0)
            die("sfs_mksquashfs error");
        
        std::cerr << "Embedding ELF..." << std::endl;
        FILE *fpdst = fopen(destination, "rb+");
        if (fpdst == NULL) {
            die("Not able to open the AppImage for writing, aborting");
        }

        fseek(fpdst, 0, SEEK_SET);
        fwrite(data, size, 1, fpdst);
        fclose(fpdst);
        // TODO: avoid memory buffer (see above)
        free(data);

        std::cerr << "Marking the AppImage as executable..." << std::endl;
        if (chmod (destination, 0755) < 0) {
            std::cout << "Could not set executable bit, aborting" << std::endl;
            exit(1);
        }

        std::string update_info;
        
        /* If the user has not provided update information but we know this is a Travis CI build,
         * then fill in update information based on TRAVIS_REPO_SLUG */
        if(guess_update_information){
            if (!travis_repo_slug.empty()) {
                if(github_token.empty()) {
                    printf("Will not guess update information since $GITHUB_TOKEN is missing,\n");
                    if(travis_pull_request != "false"){
                        printf("please set it in the Travis CI Repository Settings for this project.\n");
                        printf("You can get one from https://github.com/settings/tokens\n");
                    } else {
                        printf("which is expected since this is a pull request\n");
                    }
                } else {
                    gchar *zsyncmake_path = g_find_program_in_path ("zsyncmake");
                    if(zsyncmake_path){
                        char buf[1024];
                        std::string delimiter = "/";
                        std::string owner = travis_repo_slug.substr(0, travis_repo_slug.find(delimiter));
                        std::string repo = travis_repo_slug.substr(travis_repo_slug.find(delimiter) + 1);
                        /* https://github.com/AppImage/AppImageSpec/blob/master/draft.md#github-releases 
                         * gh-releases-zsync|probono|AppImages|latest|Subsurface*-x86_64.AppImage.zsync */
                        gchar *channel = "continuous";
                            if ((!travis_tag.empty()) && (travis_tag != "continuous")) {
                                channel = "latest";
                            }
                        std::ostringstream oss;
                        oss << "gh-releases-zsync|" << owner << "|" << repo << "|" << channel << "|" << app_name_for_filename << "*-" << arch << ".AppImage.zsync";
                        update_info = oss.str();
                        updateinformation = buf;
                        std::cout << "Guessing update information based on $TRAVIS_TAG=" << travis_tag << " and $TRAVIS_REPO_SLUG=" << travis_repo_slug << std::endl;
                        std::cout << updateinformation << std::endl;
                    } else {
                        printf("Will not guess update information since zsyncmake is missing\n");
                    }
                }
            } else if(! CI_COMMIT_REF_NAME.empty()){
                // ${CI_PROJECT_URL}/-/jobs/artifacts/${CI_COMMIT_REF_NAME}/raw/QtQuickApp-x86_64.AppImage?job=${CI_JOB_NAME}
                gchar *zsyncmake_path = g_find_program_in_path ("zsyncmake");
                if (zsyncmake_path) {
                    update_info = "zsync|" + CI_PROJECT_URL + "/-/jobs/artifacts/" + CI_COMMIT_REF_NAME + "/raw/" + app_name_for_filename + "-" + arch + ".AppImage.zsync?job=" + CI_JOB_NAME;
                    std::cout << "Guessing update information based on $CI_COMMIT_REF_NAME=" << CI_COMMIT_REF_NAME << " and $CI_JOB_NAME=" << CI_JOB_NAME << std::endl;
                    std::cout << update_info << std::endl;
                } else {
                    std::cout << "Will not guess update information since zsyncmake is missing\n";
                }
            }
        }
        
        /* If updateinformation was provided, then we check and embed it */
        if(!update_info.empty()){
            if(update_info.find("zsync|") != 0)
                if(update_info.find("gh-releases-zsync|") != 0)
                    if(update_info.find("pling-v1-zsync|") != 0)
                        die("The provided update information is not in a recognized format");
                
            std::string ui_type;
            for (int i = 0; i < update_info.length(); i++) {
                if (update_info[i] == '|') {
                    break;
                }
                ui_type += update_info[i];
            }
                        
            if(verbose) {
                std::cout << "updateinformation type: " << ui_type << std::endl;
            }
            /* TODO: Further checking of the updateinformation */
                      
            unsigned long ui_offset = 0;
            unsigned long ui_length = 0;

            bool rv = appimage_get_elf_section_offset_and_length(destination, ".upd_info", &ui_offset, &ui_length);

            if (!rv || ui_offset == 0 || ui_length == 0) {
                die("Could not find section .upd_info in runtime");
            }

            if(verbose) {
                std::cout << "ui_offset: " << ui_offset << std::endl;
                std::cout << "ui_length: " << ui_length << std::endl;
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
            std::cerr << "Embedding MD5 digest" << std::endl;

            unsigned long digest_md5_offset = 0;
            unsigned long digest_md5_length = 0;

            bool rv = appimage_get_elf_section_offset_and_length(destination, ".digest_md5", &digest_md5_offset, &digest_md5_length);

            if (!rv || digest_md5_offset == 0 || digest_md5_length == 0) {
                die("Could not find section .digest_md5 in runtime");
            }

            static const unsigned long section_size = 16;

            if (digest_md5_length < section_size) {
                std::cerr << ".digest_md5 section in runtime's ELF header is too small"
                          << "(found " << digest_md5_length << " bytes, minimum required: " << section_size << " bytes)" << std::endl;
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
                std::cerr << "zsyncmake is not installed/bundled, skipping\n";
            } else {
                std::cerr << "zsyncmake is available and updateinformation is provided, "
                                "hence generating zsync file\n";

                const gchar* const zsyncmake_command[] = {zsyncmake_path, destination, "-u", basename(destination), NULL};

                if (verbose) {
                    std::cerr << "Running zsyncmake process: ";
                    for (gint j = 0; j < (sizeof(zsyncmake_command) / sizeof(char*) - 1); ++j) {
                        std::cerr << "'" << zsyncmake_command[j] << "' ";
                    }
                    std::cerr << "\n";
                }

                GSubprocessFlags flags = G_SUBPROCESS_FLAGS_NONE;

                if (!verbose) {
                    flags = static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDERR_SILENCE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE);
                }
                
                GSubprocess* proc = g_subprocess_newv(zsyncmake_command, flags, &error);

                if (proc == NULL) {
                    std::cerr << "ERROR: failed to create zsyncmake process: " << error->message << "\n";
                    exit(1);
                }

                if (!g_subprocess_wait_check(proc, NULL, &error)) {
                    std::cerr << "ERROR: zsyncmake returned abnormal exit code: " << error->message << "\n";
                    g_object_unref(proc);
                    exit(1);
                }

                g_object_unref(proc);
            }
        }

        std::string absolute_path = std::string(realpath(destination, NULL));
        if (!absolute_path.empty()) {
            std::cerr << "Success: " << absolute_path << std::endl;
            std::cerr << "Please consider submitting your AppImage to AppImageHub, the crowd-sourced\n";
            std::cerr << "central directory of available AppImages, by opening a pull request\n";
            std::cerr << "at https://github.com/AppImage/appimage.github.io\n";
            return 0;
        }
        
    } else {
        std::cerr << "Error: no such file or directory: " << remaining_args[0] << "\n";
        return 1;
    }

    // should never be reached
    return 1;
}
