local M = {}

M.matches = {}
M.input = ""
M.selected = 1

M.display_selected = 1
M.display_window_ind = 1
M.display_window_size = nil

M.ns = nil

M.input_buf = nil
M.list_buf = nil
M.input_win = nil
M.list_win = nil
M.data_callback = nil

M.timer = nil

M.open = function(title, data_callback)
    M.data_callback = data_callback

    local frame_cols = vim.o.columns
    local frame_rows = vim.o.lines
    local win_cols = math.max(math.floor(frame_cols * .8), 20)
    local win_rows = math.max(math.floor(frame_rows * .8), 20)

    M.ns = vim.api.nvim_create_namespace('my_highlight')

    M.input_buf = vim.api.nvim_create_buf(false, true)
    M.list_buf = vim.api.nvim_create_buf(false, true)

    local origin_row = math.max(math.floor((frame_rows - win_rows) / 2.0) - 1, 1)
    local origin_col = math.max(math.floor((frame_cols - win_cols) / 2.0), 1)

    M.input_win = vim.api.nvim_open_win(M.input_buf, true, {
        relative = 'editor', width = win_cols, height = 1,
        row = origin_row, col = origin_col,
        style = 'minimal', border = "rounded",
        title = 'swimd ' .. title, title_pos = "center"
    })

    M.display_window_size = win_rows - 1
    M.list_win = vim.api.nvim_open_win(M.list_buf, false, {
        relative = 'editor', width = win_cols, height = M.display_window_size,
        row = origin_row + 2, col = origin_col,
        style = 'minimal', border = "rounded"
    })

    vim.api.nvim_buf_set_option(M.input_buf, 'buftype', 'acwrite')
    vim.api.nvim_win_set_option(M.list_win, 'wrap', false)

    M.handle_events()

    vim.api.nvim_buf_set_lines(M.input_buf, 0, -1, false, {""})
    vim.api.nvim_win_set_cursor(M.input_win, {1, 0})
    M.update(false)
    vim.cmd.startinsert()
end

M.draw_lines = function (buf, lines)
    local rendered_text = {}
    local rendered_icons = {}
    for _, match in ipairs(lines) do
        if match.is_loading or match.no_matches then
            table.insert(rendered_text, match.name)
            table.insert(rendered_icons, nil)
        else
            local ext = match.name:match("%.([^%.]+)$") or ""
            local icon, hl_name = require("nvim-web-devicons").get_icon(match.name, ext, { default = true })
            table.insert(rendered_text, icon .. " " .. match.name .. " " .. match.path)
            table.insert(rendered_icons, { icon = icon, hl_name = hl_name})
        end
    end
    vim.api.nvim_buf_set_lines(buf, 0, -1, false, rendered_text)
    for i, match in ipairs(lines) do
        if not match.is_loading and not match.no_matches then
            local line_row = i - 1
            local icon_params = rendered_icons[i]
            local icon = icon_params.icon
            local icon_hl = icon_params.hl_name

            vim.api.nvim_buf_set_extmark(buf, M.ns, line_row, 0, {
                hl_group = icon_hl,
                end_col =  #icon
            })
            local path_beg = #icon + 1 + #match.name + 1;
            local path_end = path_beg + #match.path;
            vim.api.nvim_buf_set_extmark(buf, M.ns, line_row, path_beg, {
                hl_group = 'Whitespace',
                end_col = path_end
            })
        end
    end
end

M.update_display_window = function ()
    if M.selected == 0 then
        return
    end
    if M.selected > M.display_window_ind + M.display_window_size - 1 then
        M.display_window_ind = M.selected - M.display_window_size + 1
        M.display_selected = M.display_window_size
        return
    end
    if M.selected < M.display_window_ind then
        M.display_window_ind = M.selected
        M.display_selected = 1
        return
    end
    M.display_selected = M.selected - M.display_window_ind + 1
end

M.update = function (reset_selection)
    local data = M.data_callback(M.input)
    M.matches = data.items
    if #M.matches == 0 then
        M.selected = 0
    elseif reset_selection then
        M.selected = 1
    end

    M.update_display_window()
    local lines = {}
    local lines_start = M.display_window_ind
    local lines_end = math.min(M.display_window_ind + M.display_window_size - 1,
        #M.matches)

    for i = lines_start, lines_end do
        table.insert(lines, M.matches[i])
    end
    if #M.matches == 0 then
        if data.scan_in_progress then
            local scanning = "Scanning " .. data.scanned_items_count .. " ..."
            table.insert(lines, { name = scanning, path = "", is_loading = true })
        else
            local no_matches = "No matches"
            table.insert(lines, { name = no_matches, path = "", no_matches = true })
        end
    end
    M.draw_lines(M.list_buf, lines)

    if M.selected > 0 then
        vim.api.nvim_buf_set_extmark(M.list_buf, M.ns, M.display_selected - 1, 0, {
            hl_group = 'CursorLine',
            end_row = M.display_selected,
            hl_eol = true,
        })
    end

    if data.scan_in_progress then
        M.start_refresh_timer()
    else
        M.stop_refresh_timer()
    end
end

M.start_refresh_timer = function ()
    if M.timer then
        return
    end
    M.timer = vim.loop.new_timer()
    M.timer:start(0, 300, vim.schedule_wrap(function()
        M.update(true)
    end))
end

M.stop_refresh_timer = function ()
    if M.timer then
        M.timer:stop()
        M.timer:close()
        M.timer = nil
    end
end

M.close_all = function ()
    if vim.api.nvim_win_is_valid(M.input_win) then
        vim.api.nvim_win_close(M.input_win, true)
    end
    if vim.api.nvim_win_is_valid(M.list_win) then
        vim.api.nvim_win_close(M.list_win, true)
    end
    if vim.api.nvim_buf_is_valid(M.input_buf) then
        vim.api.nvim_buf_delete(M.input_buf, { force = true })
    end
    if vim.api.nvim_buf_is_valid(M.list_buf) then
        vim.api.nvim_buf_delete(M.list_buf, { force = true })
    end
end

M.handle_events = function ()
    vim.api.nvim_create_autocmd('BufLeave', {
        buffer = M.input_buf,
        callback = function()
            M.close_all()
        end
    })

    vim.api.nvim_buf_set_keymap(M.input_buf, 'i', '<CR>', '', {
        callback = function()
            vim.cmd.stopinsert()
            M.close_all()
            if M.matches[M.selected] and M.matches[M.selected].path then
                vim.cmd('e ' .. M.matches[M.selected].path)
            end
        end
    })

    vim.api.nvim_buf_set_keymap(M.input_buf, 'n', '<Esc>', '', { callback = M.close_all })
    vim.api.nvim_create_autocmd('InsertEnter', {
        buffer = M.input_buf,
        callback = function()
            vim.api.nvim_buf_set_keymap(M.input_buf, 'i', '<C-n>', '', {
                noremap = false,
                callback = function()
                    M.selected = M.selected < #M.matches and M.selected + 1 or 1
                    M.update(false)
                end
            })
            vim.api.nvim_buf_set_keymap(M.input_buf, 'i', '<C-p>', '', {
                noremap = false,
                callback = function()
                    M.selected = M.selected > 1 and M.selected - 1 or #M.matches
                    M.update(false)
                end
            })
        end
    })

    vim.api.nvim_create_autocmd('TextChangedI', {
        buffer = M.input_buf,
        callback = function()
            M.input = vim.api.nvim_get_current_line()
            M.update(true)
        end
    })
end

return M
