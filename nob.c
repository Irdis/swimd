#define NOB_IMPLEMENTATION
#include "nob.h"

#define CL_CFLAGS "-nologo", "-MT", "-O2", "-ZI"
#define CL_LFLAGS "-DEBUG"

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (!nob_copy_file("libgit2\\git2.dll", "git2.dll")) return 1;

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "cl");
    nob_cmd_append(&cmd, "main.c");
    nob_cmd_append(&cmd, "-LD", CL_CFLAGS);

	nob_cmd_append(&cmd, "-I", "lualib\\include");
	nob_cmd_append(&cmd, "-I", "libgit2\\include");

    nob_cmd_append(&cmd, "-link");
    nob_cmd_append(&cmd, "-LIBPATH:\"lualib\"", "lua51.lib");
    nob_cmd_append(&cmd, "-LIBPATH:\"libgit2\"", "git2.lib");

    nob_cmd_append(&cmd, "-out:swimd.dll");
    nob_cmd_append(&cmd, CL_LFLAGS);

    if (!nob_cmd_run(&cmd)) return 1;

    nob_cmd_append(&cmd, "cl");
    nob_cmd_append(&cmd, "main.c");
    nob_cmd_append(&cmd, "-DDEBUG_PRINT", CL_CFLAGS);

	nob_cmd_append(&cmd, "-I", "lualib\\include");
	nob_cmd_append(&cmd, "-I", "libgit2\\include");

    nob_cmd_append(&cmd, "-link");
    nob_cmd_append(&cmd, "-LIBPATH:\"lualib\"", "lua51.lib");
    nob_cmd_append(&cmd, "-LIBPATH:\"libgit2\"", "git2.lib");

    nob_cmd_append(&cmd, "-out:swimd.exe");
    nob_cmd_append(&cmd, CL_LFLAGS);

    if (!nob_cmd_run(&cmd)) return 1;

    return 0;
}
