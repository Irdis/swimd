#include <stdbool.h>
#include <string.h>

#include "git2.h"
#include "lua.h"
#include "lauxlib.h"

#include "swimd_bitset.h"
#include "swimd_heap.h"
#include "swimd_thread.h"
#include "swimd_log.h"
#include "swimd_watch.h"
#include "swimd_scanner.h"

#define NOB_IMPLEMENTATION
#undef ERROR
#include "nob.h"

#ifdef _WIN32
    #define EXPORT __declspec(dllexport)
#else
    #define EXPORT __attribute__((visibility("default")))
#endif

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

static int swimd_scanner_lua_init(lua_State *L) {
    if (swimd_scanner_initialized) {
        const char *err = "already initialized";
        swimd_log_append(SWIMD_WARN, "%s, %s", __func__, err);

        return luaL_error(L, err);
    }

    const char *log_path = lua_gettop(L) == 0 || lua_isnil(L, 1)
        ? NULL : luaL_checkstring(L, 1);

    swimd_scanner_global_init(log_path);

    swimd_log_append(SWIMD_INFO, "Initializing");

    swimd_scanner_scanners_glob_init();

    swimd_log_append(SWIMD_INFO, "Initializing completed");
    return 0;
}

static int swimd_scanner_lua_shutdown(lua_State *L) {
    if (!swimd_scanner_initialized) {
        const char *err = "not initialized";
        swimd_log_append(SWIMD_WARN, "%s, %s", __func__, err);

        return luaL_error(L, err);
    }
    swimd_scanner_shutdown();
    return 0;
}

static int swimd_scanner_lua_setup_workspace(lua_State *L) {
    const char *err = NULL;

    if (!swimd_scanner_initialized) {
        err = "not initialized";
        goto err;
    }

    const char *workspace = luaL_checkstring(L, 1);
    int workspace_length = strlen(workspace);
    if (workspace_length > SCANNER_MAX_PATH_LENGTH) {
        err = "workspace length exceeds max size "STR(SCANNER_MAX_PATH_LENGTH);
        goto err;
    }
err:
    if (err != NULL) {
        swimd_log_append(SWIMD_WARN, "%s, %s", __func__, err);
        return luaL_error(L, err);
    }

    swimd_log_append(SWIMD_INFO, "Setting up workspace path %s", workspace);

    swimd_crit_lock(&swimd_scanner_scanners_request_lock);

    bool res = swimd_scanner_scanners_setup_path(workspace);

    swimd_crit_unlock(&swimd_scanner_scanners_request_lock);

    lua_pushboolean(L, res);

    if (res) {
        swimd_log_append(SWIMD_INFO, "Workspace path setup" );
    } else {
        swimd_log_append(SWIMD_WARN, "Failed to setup workspace path %s", workspace);
    }
    return 1;
}

static int swimd_scanner_lua_is_git_repo(lua_State *L) {
    if (!swimd_scanner_initialized) {
        const char *err = "not initialized";
        swimd_log_append(SWIMD_WARN, "%s, %s", __func__, err);

        return luaL_error(L, err);
    }

    git_repository *repo = NULL;
    SwimdScanner *scanner = &swimd_scanner_scanners[SCANNER_GIT];
    int repo_result = git_repository_open_ext(&repo,
            scanner->scan_path, 0, NULL);
    git_repository_free(repo);
    lua_pushboolean(L, repo_result != GIT_ENOTFOUND);
    return 1;
}

static int swimd_scanner_lua_refresh_workspace(lua_State *L) {
    if (!swimd_scanner_initialized) {
        const char *err = "not initialized";
        swimd_log_append(SWIMD_WARN, "%s, %s", __func__, err);

        return luaL_error(L, err);
    }

    swimd_log_append(SWIMD_INFO, "Refreshing workspace, requested from lua");
    swimd_crit_lock(&swimd_scanner_scanners_request_lock);

    bool success = swimd_scanner_scanners_refresh_path();
    lua_pushboolean(L, success);

cleanup:
    swimd_crit_unlock(&swimd_scanner_scanners_request_lock);
    swimd_log_append(SWIMD_INFO, "Refresh requested");
    return 1;
}

static int swimd_scanner_lua_is_refreshing(lua_State *L) {
    if (!swimd_scanner_initialized) {
        const char *err = "not initialized";
        swimd_log_append(SWIMD_WARN, "%s, %s", __func__, err);

        return luaL_error(L, err);
    }

    swimd_log_append(SWIMD_INFO, "Quering refresh status");
    lua_newtable(L);

    lua_pushstring(L, "details");
    lua_newtable(L);
    bool is_refreshing = false;
    for (int i = 0; i < SCANNER_COUNT; i++) {
        int scanner_refresh_count;
        bool scanner_refreshing = swimd_scanner_scan_is_refreshing(&swimd_scanner_scanners[i], &scanner_refresh_count);
        is_refreshing |= scanner_refreshing;

        lua_pushnumber(L, i + 1);

        lua_newtable(L);

        lua_pushstring(L, "refreshing");
        lua_pushboolean(L, scanner_refreshing);
        lua_settable(L, -3);

        lua_pushstring(L, "count");
        lua_pushinteger(L, scanner_refresh_count);
        lua_settable(L, -3);

        lua_settable(L, -3);

    }
    lua_settable(L, -3);

    lua_pushstring(L, "refreshing");
    lua_pushboolean(L, is_refreshing);
    lua_settable(L, -3);

    swimd_log_append(SWIMD_INFO, "Refresh status received");
    return 1;
}

static int swimd_scanner_lua_process_input(lua_State *L) {
    const char *err = NULL;

    if (!swimd_scanner_initialized) {
        err = "not initialized";
        goto err;
    }

    const char *input = luaL_checkstring(L, 1);
    size_t input_len = strlen(input);
    if (input_len > SCANNER_MAX_NAME_LENGTH) {
        err = "input len exceeds max size "STR(SCANNER_MAX_NAME_LENGTH);
        goto err;
    }

    int max_size = luaL_checknumber(L, 2);
    if (max_size < 0) {
        err = "invalid max size";
        goto err;
    }

    int scanner_index = luaL_checknumber(L, 3);
    if (scanner_index < 0 || scanner_index >= SCANNER_COUNT) {
        err = "invalid scanner index";
        goto err;
    }

err:
    if (err != NULL) {
        swimd_log_append(SWIMD_WARN, "%s, %s", __func__, err);
        return luaL_error(L, err);
    }

    swimd_crit_lock(&swimd_scanner_scanners_request_lock);

    SwimdScannerProcessInputResult result = {0};
    SwimdScanner *scanner = &swimd_scanner_scanners[scanner_index];

    swimd_scanner_scan_process_input(input, max_size, &result, scanner);
    lua_newtable(L);

    lua_pushstring(L, "scan_in_progress");
    lua_pushboolean(L, result.scan_in_progress);
    lua_settable(L, -3);

    lua_pushstring(L, "scanner_initialized");
    lua_pushboolean(L, result.scanner_initialized);
    lua_settable(L, -3);

    lua_pushstring(L, "scanned_items_count");
    lua_pushinteger(L, result.scanned_items_count);
    lua_settable(L, -3);

    lua_pushstring(L, "items");
    lua_newtable(L);
    if (!result.scan_in_progress) {
        for (int i = 0; i < result.items_length; i++) {
            SwimdScannerProcessInputResultItem item = result.items[i];

            lua_pushnumber(L, i + 1);

            lua_newtable(L);
            lua_pushstring(L, "name");
            lua_pushstring(L, item.name);
            lua_settable(L, -3);

            lua_pushstring(L, "score");
            lua_pushinteger(L, item.score);
            lua_settable(L, -3);

            lua_pushstring(L, "path");
            lua_pushstring(L, item.path);
            lua_settable(L, -3);

            lua_settable(L, -3);
        }
    }
    lua_settable(L, -3);

    swimd_scanner_scan_process_input_free(&result);

    swimd_crit_unlock(&swimd_scanner_scanners_request_lock);
    return 1;
}

static int swimd_scanner_lua_sayhello(lua_State *L) {
    if (!swimd_scanner_initialized) {
        const char *err = "not initialized";
        swimd_log_append(SWIMD_WARN, "%s, %s", __func__, err);

        return luaL_error(L, err);
    }

    const char *str = luaL_checkstring(L, 1);
    lua_pushfstring(L, "Hello, %s!", str);
    return 1;
}

static int swimd_scanner_lua_log(lua_State *L) {
    if (!swimd_scanner_initialized) {
        const char *err = "not initialized";
        swimd_log_append(SWIMD_WARN, "%s, %s", __func__, err);

        return luaL_error(L, err);
    }

    const char *str = luaL_checkstring(L, 1);
    swimd_log_append(SWIMD_INFO, "[swimd-lua]: %s", str);
    return 1;
}

EXPORT
int luaopen_swimd(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"init", swimd_scanner_lua_init},
        {"setup_workspace", swimd_scanner_lua_setup_workspace},
        {"refresh_workspace", swimd_scanner_lua_refresh_workspace},
        {"is_refreshing", swimd_scanner_lua_is_refreshing},
        {"is_git_repo", swimd_scanner_lua_is_git_repo},
        {"process_input", swimd_scanner_lua_process_input},
        {"shutdown", swimd_scanner_lua_shutdown},
        {"say_hello", swimd_scanner_lua_sayhello},
        {"log", swimd_scanner_lua_log},

        {NULL, NULL}
    };
    luaL_register(L, "swimd", funcs);

    lua_pushinteger(L, SCANNER_FILES);
    lua_setfield(L, -2, "SCANNER_FILES");

    lua_pushinteger(L, SCANNER_GIT);
    lua_setfield(L, -2, "SCANNER_GIT");

    lua_pushinteger(L, SCANNER_MAX_NAME_LENGTH);
    lua_setfield(L, -2, "SCANNER_MAX_NAME_LENGTH");

    lua_pushinteger(L, SCANNER_MAX_PATH_LENGTH);
    lua_setfield(L, -2, "SCANNER_MAX_PATH_LENGTH");

    return 1;
}
