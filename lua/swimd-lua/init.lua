local M = {}

M.setup = function()
    M.set_cpath();

    local swimd = require("swimd")
    swimd.init(M.log_path())

    local cwd = vim.fn.getcwd()
    swimd.setup_workspace(cwd)
end

M.log_path = function ()
    local source = debug.getinfo(1, "S").source:sub(2)
    local plugin_dir = vim.fn.fnamemodify(source, ":p:h")
    local result = plugin_dir .. '/../../swimd.log';
    return nil
end

M.set_cpath = function ()
    local swimd_path = M.lib_path()
    package.cpath = package.cpath .. ';' .. swimd_path
end

M.lib_path = function ()
    local source = debug.getinfo(1, "S").source:sub(2)
    local plugin_dir = vim.fn.fnamemodify(source, ":p:h")
    local result = plugin_dir .. '/../../?.dll';
    return result
end

M.open_picker = function ()
    local snack = M.configure_snacks()
    Snacks.picker(snack)
end

M.configure_snacks = function()
    return {
        title = 'swimd-lua',
        finder = function(opts, ctx)
            local swimd = require("swimd")
            local res = swimd.process_input(ctx.filter.search, 100)

            local items = {}
            if res.scan_in_progress then
                local item = {
                    text = 'scanning '.. res.scanned_items_count
                }
                items[#items + 1] = item
            else
                for _, swimd_item in ipairs(res.items) do
                    local item = {
                        text = swimd_item.name,
                        file = swimd_item.path,
                        score = swimd_item.score,
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
