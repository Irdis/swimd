local M = {}

local SCANNER_GIT = 0
local SCANNER_FILES = 1

M.setup = function()
    M.load_libs();

    local swimd = require("swimd")
    swimd.init(M.log_path())

    local cwd = vim.fn.getcwd()
    swimd.setup_workspace(cwd)
end

M.log_path = function ()
    local source = debug.getinfo(1, "S").source:sub(2)
    local plugin_dir = vim.fn.fnamemodify(source, ":p:h")
    local result = plugin_dir .. '/../../swimd.log'
    return result
end

M.load_libs = function ()
    local swimd_path = M.lib_path()
    local swimd_depenecies = M.dependent_libs()
    for _, deps in ipairs(swimd_depenecies) do
        package.loadlib(deps, '')
    end
    package.cpath = package.cpath .. ';' .. swimd_path
end

M.lib_path = function ()
    local plugin_dir = M.plugin_root()
    local result = plugin_dir .. '/../../?.dll'
    return result
end

M.dependent_libs = function ()
    local plugin_dir = M.plugin_root()
    local result = plugin_dir .. '/../../git2.dll'
    return { result }
end

M.plugin_root = function ()
    local source = debug.getinfo(1, "S").source:sub(2)
    local plugin_dir = vim.fn.fnamemodify(source, ":p:h")
    return plugin_dir
end

M.open_picker_git = function ()
    local snack = M.configure_snacks(SCANNER_GIT)
    Snacks.picker(snack)
end

M.open_picker_files = function ()
    local snack = M.configure_snacks(SCANNER_FILES)
    Snacks.picker(snack)
end

M.configure_snacks = function(scanner)
    return {
        title = 'swimd-lua',
        finder = function(opts, ctx)
            local swimd = require("swimd")
            local res = swimd.process_input(ctx.filter.search, 100, scanner)

            local items = {}
            if res.scan_in_progress then
                local item = {
                    text = 'scanning '.. res.scanned_items_count
                }
                items[#items + 1] = item
            else
                for _, swimd_item in ipairs(res.items) do
                    local item = {
                        file = './' .. swimd_item.path,
                    }
                    items[#items + 1] = item
                end
            end

            return items
        end,
        formatters = {
            file = {
                filename_first = true,
            },
        },
        live = true,
    }
end

M.log = function(msg)
    print("[swimd-lua] " .. msg)
end

return M
