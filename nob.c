#define NOB_IMPLEMENTATION
#include "nob.h"

#define MSVC_CFLAGS "-nologo", "-MT", "-O2", "-ZI"
#define MSVC_LFLAGS "-DEBUG"
#define MSVC_INCLUDES \
    "-I", "lualib\\include", \
    "-I", "libgit2\\include"
#define MSVC_LINKS \
    "-LIBPATH:\"lualib\"", "lua51.lib", \
    "-LIBPATH:\"libgit2\"", "git2.lib"

#define CC_CFLAGS "-mavx2"
#define CC_INCLUDES \
    "-I/usr/include/lua5.1", \
    "-Ilibgit2/include"
#define CC_LINKS  \
    "-llua5.1", \
    "-Lbuild", "-lgit2"

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

#if !defined(_MSC_VER)
    if (!nob_copy_file("libgit2/libgit2.so", "build/libgit2.so")) return 1;

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd, "-shared", "-fPIC");
    nob_cmd_append(&cmd, CC_CFLAGS);
    nob_cmd_append(&cmd, CC_INCLUDES);
    nob_cmd_append(&cmd, CC_LINKS);
    nob_cmd_append(&cmd, "-o", "build/swimd.so");
    nob_cmd_append(&cmd, "main.c");
    if (!nob_cmd_run(&cmd)) return 1;

    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd, "-DDEBUG_PRINT");
    nob_cmd_append(&cmd, CC_CFLAGS);
    nob_cmd_append(&cmd, CC_INCLUDES);
    nob_cmd_append(&cmd, CC_LINKS);
    nob_cmd_append(&cmd, "-o", "build/swimd");
    nob_cmd_append(&cmd, "main.c");
    if (!nob_cmd_run(&cmd)) return 1;
#else
    if (!nob_copy_file("libgit2\\git2.dll", "git2.dll")) return 1;

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "cl");
    nob_cmd_append(&cmd, "main.c");
    nob_cmd_append(&cmd, "-LD", MSVC_CFLAGS);

	nob_cmd_append(&cmd, MSVC_INCLUDES);
    nob_cmd_append(&cmd, "-link");
    nob_cmd_append(&cmd, MSVC_LINKS);

    nob_cmd_append(&cmd, "-out:swimd.dll");
    nob_cmd_append(&cmd, MSVC_LFLAGS);

    if (!nob_cmd_run(&cmd)) return 1;

    nob_cmd_append(&cmd, "cl");
    nob_cmd_append(&cmd, "main.c");
    nob_cmd_append(&cmd, "-DDEBUG_PRINT", MSVC_CFLAGS);

	nob_cmd_append(&cmd, MSVC_INCLUDES);
    nob_cmd_append(&cmd, "-link");
    nob_cmd_append(&cmd, MSVC_LINKS);

    nob_cmd_append(&cmd, "-out:swimd.exe");
    nob_cmd_append(&cmd, MSVC_LFLAGS);

    if (!nob_cmd_run(&cmd)) return 1;

#endif // _MSC_VER
    return 0;
}
