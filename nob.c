#define NOB_IMPLEMENTATION
#include "nob.h"

// #define DEBUG_MODE 1

#ifdef DEBUG_MODE
#define DEBUG_PRINT 1
#endif // DEBUG_MODE

#define MSVC_CC "cl"
#define MINGW_CC "x86_64-w64-mingw32-gcc"
#define CC "cc"

#define MSVC_CFLAGS "-nologo", "-MT", "-O2", "-ZI", "-std:c11", "-experimental:c11atomics"
#define MSVC_SUBMODULE_CFLAGS "-nologo", "-O2", "-std:c11", "-experimental:c11atomics"

#ifdef DEBUG_MODE
#define MSVC_LFLAGS "-DEBUG"
#endif // DEBUG_MODE

#define MSVC_INCLUDES \
    "-I", "lualib\\include", \
    "-I", "libgit2\\include"
#define MSVC_SCANNER_INCLUDES \
    "-I", "libgit2\\include"
#define MSVC_LINKS \
    "-LIBPATH:\"lualib\"", "lua51.lib", \
    "-LIBPATH:\"libgit2\"", "git2.lib", \
    "build\\swimd_heap.obj", \
    "build\\swimd_bitset.obj", \
    "build\\swimd_thread.obj", \
    "build\\swimd_log.obj", \
    "build\\swimd_watch.obj", \
    "build\\swimd_scanner.obj"

#ifdef DEBUG_MODE
#define CC_CFLAGS "-O0", "-Wreturn-type", "-g"
#else // DEBUG_MODE
#define CC_CFLAGS  "-flto", "-O2", "-Wreturn-type"
#endif // DEBUG_MODE


#ifdef DEBUG_MODE
#define CC_SUBMODULE_CFLAGS "-Wall", "-Wextra", "-Wno-unused-function", "-fPIC", "-g"
#else // DEBUG_MODE
#define CC_SUBMODULE_CFLAGS "-Wall", "-Wextra", "-Wno-unused-function", "-fPIC", "-flto", "-O2"
#endif // DEBUG_MODE


#define MINGW_CC_INCLUDES \
    "-Ilualib/include", \
    "-Ilibgit2/include"
#define MINGW_CC_LINKS  \
    "build/swimd_heap.o", \
    "build/swimd_bitset.o", \
    "build/swimd_thread.o", \
    "build/swimd_log.o", \
    "build/swimd_watch.o", \
    "build/swimd_scanner.o", \
    "-L", "lualib", "-llua51", \
    "-L", "libgit2", "-lgit2"

#define MINGW_OUT_LIB "swimd.dll"
#define MINGW_OUT_EXE "swimd.exe"

#define CC_INCLUDES \
    "-I/usr/include/lua5.1", \
    "-Ilibgit2/include"
#define CC_LINKS  \
    "-llua5.1", \
    "-lgit2", \
    "build/swimd_heap.o", \
    "build/swimd_bitset.o", \
    "build/swimd_thread.o", \
    "build/swimd_log.o", \
    "build/swimd_watch.o", \
    "build/swimd_scanner.o"

#define CC_OUT_LIB "swimd.so"
#define CC_OUT_EXE "swimd"

typedef struct {
    bool mingw_compiler;
    bool is_win;
    bool is_linux;
} SwimdNobOptions;

void cmd_add_debug_print_ifdef(Nob_Cmd *cmd) {
#ifdef DEBUG_PRINT
    nob_cmd_append(cmd, "-DDEBUG_PRINT");
#endif // DEBUG_PRINT
}

void cmd_add_cc(Nob_Cmd *cmd, SwimdNobOptions *opt) {
    if (opt->mingw_compiler) {
        nob_cmd_append(cmd, MINGW_CC);
    } else {
        nob_cmd_append(cmd, CC);
    }
}

void cmd_add_cc_links(Nob_Cmd *cmd, SwimdNobOptions *opt) {
    if (opt->mingw_compiler) {
        nob_cmd_append(cmd, MINGW_CC_LINKS);
    } else {
        nob_cmd_append(cmd, CC_LINKS);
    }
}

void cmd_add_cc_includes(Nob_Cmd *cmd, SwimdNobOptions *opt) {
    if (opt->mingw_compiler) {
        nob_cmd_append(cmd, MINGW_CC_INCLUDES);
    } else {
        nob_cmd_append(cmd, CC_INCLUDES);
    }
}

void cmd_add_o_lib(Nob_Cmd *cmd, SwimdNobOptions *opt) {
    if (opt->mingw_compiler) {
        nob_cmd_append(cmd, "-o", "build/"MINGW_OUT_LIB);
    } else {
        nob_cmd_append(cmd, "-o", "build/"CC_OUT_LIB);
    }
}

void cmd_add_o_test_exe(Nob_Cmd *cmd, SwimdNobOptions *opt) {
    if (opt->mingw_compiler) {
        nob_cmd_append(cmd, "-o", "build/test_"MINGW_OUT_EXE);
    } else {
        nob_cmd_append(cmd, "-o", "build/test_"CC_OUT_EXE);
    }
}

void cmd_add_o_bench_exe(Nob_Cmd *cmd, SwimdNobOptions *opt) {
    if (opt->mingw_compiler) {
        nob_cmd_append(cmd, "-o", "build/bench_"MINGW_OUT_EXE);
    } else {
        nob_cmd_append(cmd, "-o", "build/bench_"CC_OUT_EXE);
    }
}

void cmd_add_o_exe(Nob_Cmd *cmd, SwimdNobOptions *opt) {
    if (opt->mingw_compiler) {
        nob_cmd_append(cmd, "-o", "build/"MINGW_OUT_EXE);
    } else {
        nob_cmd_append(cmd, "-o", "build/"CC_OUT_EXE);
    }
}

void cmd_add_msvc_link_flags(Nob_Cmd *cmd) {
#ifdef MSVC_LFLAGS
    nob_cmd_append(cmd, MSVC_LFLAGS);
#endif // DEBUG_PRINT
}

void parse_options(SwimdNobOptions *opt, int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];

        if (strcmp(arg, "-mingw") == 0) {
            opt->mingw_compiler = true;
        }
    }
#ifdef _WIN32
    opt->is_win = true;
    opt->is_linux = false;
#else
    opt->is_win = false;
    opt->is_linux = true;
#endif

}

bool build_bench(Nob_Cmd *cmd, SwimdNobOptions *opt) {
    if (opt->mingw_compiler || opt->is_linux) {
        cmd_add_cc(cmd, opt);
        nob_cmd_append(cmd, CC_CFLAGS);
        cmd_add_debug_print_ifdef(cmd);
        cmd_add_cc_includes(cmd, opt);
        cmd_add_o_bench_exe(cmd, opt);
        nob_cmd_append(cmd, "bench/swimd_bench.c");
        cmd_add_cc_links(cmd, opt);
        if (!nob_cmd_run(cmd)) return false;
        return true;
    } else {
        nob_cmd_append(cmd, MSVC_CC);
        nob_cmd_append(cmd, "bench\\swimd_bench.c");
        cmd_add_debug_print_ifdef(cmd);
        nob_cmd_append(cmd, MSVC_CFLAGS);

        nob_cmd_append(cmd, MSVC_INCLUDES);
        nob_cmd_append(cmd, "-link");
        nob_cmd_append(cmd, MSVC_LINKS);

        nob_cmd_append(cmd, "-out:build\\bench_swimd.exe");
        cmd_add_msvc_link_flags(cmd);

        if (!nob_cmd_run(cmd)) return false;
        return true;
    }
}

bool build_tests(Nob_Cmd *cmd, SwimdNobOptions *opt) {
    if (opt->mingw_compiler || opt->is_linux) {
        cmd_add_cc(cmd, opt);
        nob_cmd_append(cmd, CC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(cmd);
        nob_cmd_append(cmd, "-c", "tests/swimd_cunit.c");
        nob_cmd_append(cmd, "-o", "build/swimd_cunit.o");
        if (!nob_cmd_run(cmd)) return false;

        cmd_add_cc(cmd, opt);
        nob_cmd_append(cmd, CC_CFLAGS);
        cmd_add_debug_print_ifdef(cmd);
        nob_cmd_append(cmd, "build/swimd_cunit.o");
        cmd_add_cc_includes(cmd, opt);
        cmd_add_o_test_exe(cmd, opt);
        nob_cmd_append(cmd, "tests/swimd_tests.c");
        cmd_add_cc_links(cmd, opt);
        if (!nob_cmd_run(cmd)) return false;
        return true;
    } else {
        nob_cmd_append(cmd, MSVC_CC);
        nob_cmd_append(cmd, MSVC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(cmd);
        nob_cmd_append(cmd, "/c", "tests\\swimd_cunit.c");
        nob_cmd_append(cmd, "/Fo:build\\swimd_cunit.obj");
        if (!nob_cmd_run(cmd)) return false;

        nob_cmd_append(cmd, MSVC_CC);
        nob_cmd_append(cmd, "tests\\swimd_tests.c");
        cmd_add_debug_print_ifdef(cmd);
        nob_cmd_append(cmd, MSVC_CFLAGS);

        nob_cmd_append(cmd, MSVC_INCLUDES);
        nob_cmd_append(cmd, "-link");
        nob_cmd_append(cmd, MSVC_LINKS);
        nob_cmd_append(cmd, "build\\swimd_cunit.obj");

        nob_cmd_append(cmd, "-out:build\\test_swimd.exe");
        cmd_add_msvc_link_flags(cmd);

        if (!nob_cmd_run(cmd)) return false;
        return true;
    }
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    SwimdNobOptions opt = {0};
    parse_options(&opt, argc, argv);

    if (!nob_copy_file("version", "build/version")) return 1;

    if (opt.mingw_compiler || opt.is_win) {
        if (!nob_copy_file("libgit2/git2.dll", "build/git2.dll")) return 1;
    } else {
        if (!nob_copy_file("libgit2/libgit2.so", "build/libgit2.so")) return 1;
    }

    if (opt.mingw_compiler || opt.is_linux) {
        Nob_Cmd cmd = {0};

        cmd_add_cc(&cmd, &opt);
        nob_cmd_append(&cmd, CC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, "-c", "swimd_heap.c");
        nob_cmd_append(&cmd, "-o", "build/swimd_heap.o");
        if (!nob_cmd_run(&cmd)) return 1;

        cmd_add_cc(&cmd, &opt);
        nob_cmd_append(&cmd, CC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, "-c", "swimd_bitset.c");
        nob_cmd_append(&cmd, "-o", "build/swimd_bitset.o");
        if (!nob_cmd_run(&cmd)) return 1;

        cmd_add_cc(&cmd, &opt);
        nob_cmd_append(&cmd, CC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, "-c", "swimd_log.c");
        nob_cmd_append(&cmd, "-o", "build/swimd_log.o");
        if (!nob_cmd_run(&cmd)) return 1;

        cmd_add_cc(&cmd, &opt);
        nob_cmd_append(&cmd, CC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, "-c", "swimd_thread.c");
        nob_cmd_append(&cmd, "-o", "build/swimd_thread.o");
        if (!nob_cmd_run(&cmd)) return 1;

        cmd_add_cc(&cmd, &opt);
        nob_cmd_append(&cmd, CC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, "-c", "swimd_watch.c");
        nob_cmd_append(&cmd, "-o", "build/swimd_watch.o");
        if (!nob_cmd_run(&cmd)) return 1;

        cmd_add_cc(&cmd, &opt);
        nob_cmd_append(&cmd, CC_SUBMODULE_CFLAGS, "-mavx2");
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, CC_INCLUDES);
        nob_cmd_append(&cmd, "-c", "swimd_scanner.c");
        nob_cmd_append(&cmd, "-o", "build/swimd_scanner.o");
        if (!nob_cmd_run(&cmd)) return 1;

        cmd_add_cc(&cmd, &opt);
        nob_cmd_append(&cmd, "-shared", "-fPIC");
        nob_cmd_append(&cmd, CC_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        cmd_add_cc_includes(&cmd, &opt);
        cmd_add_o_lib(&cmd, &opt);
        nob_cmd_append(&cmd, "swimd_lua.c");
        cmd_add_cc_links(&cmd, &opt);
        if (!nob_cmd_run(&cmd)) return 1;

        cmd_add_cc(&cmd, &opt);
        nob_cmd_append(&cmd, CC_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        cmd_add_cc_includes(&cmd, &opt);
        cmd_add_o_exe(&cmd, &opt);
        nob_cmd_append(&cmd, "swimd_cli.c");
        cmd_add_cc_links(&cmd, &opt);
        if (!nob_cmd_run(&cmd)) return 1;

        if (!build_tests(&cmd, &opt)) return 1;
        if (!build_bench(&cmd, &opt)) return 1;

    } else {

        Nob_Cmd cmd = {0};

        nob_cmd_append(&cmd, MSVC_CC);
        nob_cmd_append(&cmd, MSVC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, "/c", "swimd_heap.c");
        nob_cmd_append(&cmd, "/Fo:build\\swimd_heap.obj");
        if (!nob_cmd_run(&cmd)) return 1;

        nob_cmd_append(&cmd, MSVC_CC);
        nob_cmd_append(&cmd, MSVC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, "/c", "swimd_bitset.c");
        nob_cmd_append(&cmd, "/Fo:build\\swimd_bitset.obj");
        if (!nob_cmd_run(&cmd)) return 1;

        nob_cmd_append(&cmd, MSVC_CC);
        nob_cmd_append(&cmd, MSVC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, "/c", "swimd_log.c");
        nob_cmd_append(&cmd, "/Fo:build\\swimd_log.obj");
        if (!nob_cmd_run(&cmd)) return 1;

        nob_cmd_append(&cmd, MSVC_CC);
        nob_cmd_append(&cmd, MSVC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, "/c", "swimd_thread.c");
        nob_cmd_append(&cmd, "/Fo:build\\swimd_thread.obj");
        if (!nob_cmd_run(&cmd)) return 1;

        nob_cmd_append(&cmd, MSVC_CC);
        nob_cmd_append(&cmd, MSVC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, "/c", "swimd_watch.c");
        nob_cmd_append(&cmd, "/Fo:build\\swimd_watch.obj");
        if (!nob_cmd_run(&cmd)) return 1;

        nob_cmd_append(&cmd, MSVC_CC);
        nob_cmd_append(&cmd, MSVC_SUBMODULE_CFLAGS);
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, MSVC_SCANNER_INCLUDES);
        nob_cmd_append(&cmd, "/c", "swimd_scanner.c");
        nob_cmd_append(&cmd, "/Fo:build\\swimd_scanner.obj");
        if (!nob_cmd_run(&cmd)) return 1;

        nob_cmd_append(&cmd, MSVC_CC);
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, "swimd_lua.c");
        nob_cmd_append(&cmd, "-LD", MSVC_CFLAGS);

        nob_cmd_append(&cmd, MSVC_INCLUDES);
        nob_cmd_append(&cmd, "-link");
        nob_cmd_append(&cmd, MSVC_LINKS);

        nob_cmd_append(&cmd, "-out:build\\swimd.dll");
        cmd_add_msvc_link_flags(&cmd);

        if (!nob_cmd_run(&cmd)) return 1;

        nob_cmd_append(&cmd, MSVC_CC);
        nob_cmd_append(&cmd, "swimd_cli.c");
        cmd_add_debug_print_ifdef(&cmd);
        nob_cmd_append(&cmd, MSVC_CFLAGS);

        nob_cmd_append(&cmd, MSVC_INCLUDES);
        nob_cmd_append(&cmd, "-link");
        nob_cmd_append(&cmd, MSVC_LINKS);

        nob_cmd_append(&cmd, "-out:build\\swimd.exe");
        cmd_add_msvc_link_flags(&cmd);

        if (!nob_cmd_run(&cmd)) return 1;

        if (!build_tests(&cmd, &opt)) return 1;
        if (!build_bench(&cmd, &opt)) return 1;
    }
    return 0;
}
