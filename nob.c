#define NOB_IMPLEMENTATION
#include "nob.h"

// #define DEBUG_MODE 1

#ifdef DEBUG_MODE
#define DEBUG_PRINT 1
#endif // DEBUG_MODE

#define MSVC_CFLAGS "-nologo", "-MT", "-O2", "-ZI"
#define MSVC_SUBMODULE_CFLAGS "-nologo"

#ifdef DEBUG_MODE
#define MSVC_LFLAGS "-DEBUG"
#endif // DEBUG_MODE

#define MSVC_INCLUDES \
    "-I", "lualib\\include", \
    "-I", "libgit2\\include"
#define MSVC_LINKS \
    "-LIBPATH:\"lualib\"", "lua51.lib", \
    "-LIBPATH:\"libgit2\"", "git2.lib", \
    "build\\swimd_thread.obj", "build\\swimd_log.obj", "build\\swimd_watch.obj"

#ifdef DEBUG_MODE
#define CC_CFLAGS "-mavx2", "-O0", "-Wreturn-type", "-g"
#else // DEBUG_MODE
#define CC_CFLAGS "-mavx2", "-O2", "-Wreturn-type"
#endif // DEBUG_MODE

#define CC_SUBMODULE_CFLAGS "-Wall", "-Wextra", "-Wno-unused-function", "-fPIC"

#define CC_INCLUDES \
    "-I/usr/include/lua5.1", \
    "-Ilibgit2/include"
#define CC_LINKS  \
    "-llua5.1", \
    "-Lbuild", "-lgit2", \
    "build/swimd_thread.o", \
    "build/swimd_log.o", \
    "build/swimd_watch.o"

void cmd_add_debug_print_ifdef(Nob_Cmd *cmd) {
#ifdef DEBUG_PRINT
    nob_cmd_append(cmd, "-DDEBUG_PRINT");
#endif // DEBUG_PRINT
}

void cmd_add_msvc_link_flags(Nob_Cmd *cmd) {
#ifdef MSVC_LFLAGS
    nob_cmd_append(cmd, MSVC_LFLAGS);
#endif // DEBUG_PRINT
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (!nob_copy_file("version", "build/version")) return 1;

#if !defined(_MSC_VER)
    if (!nob_copy_file("libgit2/libgit2.so", "build/libgit2.so")) return 1;

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd, CC_SUBMODULE_CFLAGS);
    cmd_add_debug_print_ifdef(&cmd);
    nob_cmd_append(&cmd, "-c", "swimd_log.c");
    nob_cmd_append(&cmd, "-o", "build/swimd_log.o");
    if (!nob_cmd_run(&cmd)) return 1;

    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd, CC_SUBMODULE_CFLAGS);
    cmd_add_debug_print_ifdef(&cmd);
    nob_cmd_append(&cmd, "-c", "swimd_thread.c");
    nob_cmd_append(&cmd, "-o", "build/swimd_thread.o");
    cmd_add_debug_print_ifdef(&cmd);
    if (!nob_cmd_run(&cmd)) return 1;

    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd, CC_SUBMODULE_CFLAGS);
    cmd_add_debug_print_ifdef(&cmd);
    nob_cmd_append(&cmd, "-c", "swimd_watch.c");
    nob_cmd_append(&cmd, "-o", "build/swimd_watch.o");
    if (!nob_cmd_run(&cmd)) return 1;

    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd, "-shared", "-fPIC");
    nob_cmd_append(&cmd, CC_CFLAGS);
    cmd_add_debug_print_ifdef(&cmd);
    nob_cmd_append(&cmd, CC_INCLUDES);
    nob_cmd_append(&cmd, CC_LINKS);
    nob_cmd_append(&cmd, "-o", "build/swimd.so");
    nob_cmd_append(&cmd, "main.c");
    if (!nob_cmd_run(&cmd)) return 1;

    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd, CC_CFLAGS);
    cmd_add_debug_print_ifdef(&cmd);
    nob_cmd_append(&cmd, CC_INCLUDES);
    nob_cmd_append(&cmd, CC_LINKS);
    nob_cmd_append(&cmd, "-o", "build/swimd");
    nob_cmd_append(&cmd, "main.c");
    if (!nob_cmd_run(&cmd)) return 1;
#else
    if (!nob_copy_file("libgit2\\git2.dll", "build\\git2.dll")) return 1;

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "cl");
    nob_cmd_append(&cmd, MSVC_SUBMODULE_CFLAGS);
    cmd_add_debug_print_ifdef(&cmd);
    nob_cmd_append(&cmd, "/c", "swimd_log.c");
    nob_cmd_append(&cmd, "/Fo:build\\swimd_log.obj");
    if (!nob_cmd_run(&cmd)) return 1;

    nob_cmd_append(&cmd, "cl");
    nob_cmd_append(&cmd, MSVC_SUBMODULE_CFLAGS);
    cmd_add_debug_print_ifdef(&cmd);
    nob_cmd_append(&cmd, "/c", "swimd_thread.c");
    nob_cmd_append(&cmd, "/Fo:build\\swimd_thread.obj");
    if (!nob_cmd_run(&cmd)) return 1;

    nob_cmd_append(&cmd, "cl");
    nob_cmd_append(&cmd, MSVC_SUBMODULE_CFLAGS);
    cmd_add_debug_print_ifdef(&cmd);
    nob_cmd_append(&cmd, "/c", "swimd_watch.c");
    nob_cmd_append(&cmd, "/Fo:build\\swimd_watch.obj");
    if (!nob_cmd_run(&cmd)) return 1;

    nob_cmd_append(&cmd, "cl");
    cmd_add_debug_print_ifdef(&cmd);
    nob_cmd_append(&cmd, "main.c");
    nob_cmd_append(&cmd, "-LD", MSVC_CFLAGS);

	nob_cmd_append(&cmd, MSVC_INCLUDES);
    nob_cmd_append(&cmd, "-link");
    nob_cmd_append(&cmd, MSVC_LINKS);

    nob_cmd_append(&cmd, "-out:build\\swimd.dll");
    cmd_add_msvc_link_flags(&cmd);

    if (!nob_cmd_run(&cmd)) return 1;

    nob_cmd_append(&cmd, "cl");
    nob_cmd_append(&cmd, "main.c");
    cmd_add_debug_print_ifdef(&cmd);
    nob_cmd_append(&cmd, MSVC_CFLAGS);

	nob_cmd_append(&cmd, MSVC_INCLUDES);
    nob_cmd_append(&cmd, "-link");
    nob_cmd_append(&cmd, MSVC_LINKS);

    nob_cmd_append(&cmd, "-out:build\\swimd.exe");
    cmd_add_msvc_link_flags(&cmd);

    if (!nob_cmd_run(&cmd)) return 1;

#endif // _MSC_VER
    return 0;
}
