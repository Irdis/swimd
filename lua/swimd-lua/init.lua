local M = {}

M.setup = function()
    M.setup_libs()
    M.load_libs()

    local swimd = require("swimd")
    swimd.init(M.log_path())

    local cwd = vim.fn.getcwd()
    swimd.setup_workspace(cwd)
end

M.setup_libs = function ()
    if M.swimd_exist_and_version_match() then
        return
    end
    M.log('Updating binaries...')
    local plugin_root = M.plugin_root()
    local build_folder = plugin_root .. 'build/'
    local release_folder = plugin_root .. 'release/'
    if (M.is_linux()) then
        release_folder = release_folder .. 'linux/'
    else
        release_folder = release_folder .. 'win/'
    end
    M.clean_dir(build_folder)
    M.copy_folder_content(release_folder, build_folder);
    M.log('Binaries updated')
end

M.swimd_exist_and_version_match = function ()
    local plugin_root = M.plugin_root()

    local original_version_file = plugin_root .. 'build/version'
    if not M.file_exists(original_version_file) then
        return false
    end

    local original_version = vim.fn.readfile(original_version_file)

    local new_version_file = plugin_root .. 'version'
    local new_version = vim.fn.readfile(new_version_file)

    return original_version[1] == new_version[1]
end

M.log_path = function ()
    local plugin_root = M.plugin_root()
    local result = plugin_root .. 'swimd.log'
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
    local plugin_root = M.plugin_root()
    local result = plugin_root .. 'build/'
    if M.is_linux() then
        result = result .. '?.so'
    else
        result = result .. '?.dll'
    end
    return result
end

M.dependent_libs = function ()
    local plugin_root = M.plugin_root()
    local result = plugin_root .. 'build/'
    if M.is_linux() then
        result = result .. 'libgit2.so'
    else
        result = result .. 'git2.dll'
    end
    return { result }
end

M.plugin_root = function ()
    return M.plugin_dir() .. '../../';
end

M.plugin_dir = function ()
    local source = debug.getinfo(1, "S").source:sub(2)
    local plugin_dir = vim.fn.fnamemodify(source, ":p:h")
    return plugin_dir .. '/'
end

M.open_picker_git = function ()
    local swimd = require("swimd")
    local snack = M.configure_snacks(swimd.SCANNER_GIT, "git")
    Snacks.picker(snack)
end

M.refresh = function ()
    local swimd = require("swimd")
    swimd.refresh_workspace()
    M.log("refreshing... not going to say when it's over :(")
end

M.open_picker_files = function ()
    local swimd = require("swimd")
    local snack = M.configure_snacks(swimd.SCANNER_FILES, "files")
    Snacks.picker(snack)
end

M.is_linux = function ()
    local os_name = vim.loop.os_uname().sysname
    return os_name == "Linux"
end

M.configure_snacks = function(scanner, scanner_name)
    return {
        title = 'swimd-lua ' .. scanner_name,
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
        preview = "none",
        layout = { preview = false },
        formatters = {
            file = {
                filename_first = true,
            },
        },
        live = true,
    }
end

M.file_exists = function (path)
    return vim.loop.fs_stat(path) ~= nil
end

M.folder_exists = function (path)
    return vim.loop.fs_stat(path) ~= nil
end

M.log = function(msg)
    print("[swimd-lua] " .. msg)
end

M.clean_dir = function (path)
    local files = vim.fn.readdir('.')
    for _, file in ipairs(files) do
        if file ~= "." and file ~= ".." then
            local file_path = path .. '/' .. file
            os.remove(file_path)
        end
    end
end

M.copy_folder_content = function (source_path, dest_path)
    local files = vim.fn.readdir(source_path)
    for _, file in ipairs(files) do
        if file ~= "." and file ~= ".." then
            local source_file_path = source_path .. file
            local dest_file_path = dest_path .. file
            M.copy_file(source_file_path, dest_file_path)
        end
    end
end

M.copy_file = function(source_path, dest_path)
    local inputFile = io.open(source_path, "rb")
    if not inputFile then return nil, "Source file not found" end

    local outputFile = io.open(dest_path, "wb")
    if not outputFile then
        inputFile:close()
        return nil, "Cannot open destination file"
    end

    outputFile:write(inputFile:read("a"))

    outputFile:close()
    inputFile:close()
end

return M
