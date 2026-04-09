# SWIMD

A fuzzy search Neovim plugin. It supports searching in git repositories and the file system. It uses the Smith-Waterman algorithm with [Inter-sequence Parallelization](https://pmc.ncbi.nlm.nih.gov/articles/PMC8419822/#Sec13).

It's inspired by [frizbee](https://github.com/saghen/frizbee) but written completely from scratch in C for ultimate perfomance. It supports x64 Windows/Linux.

## Installation

### lazy.nvim:
```lua
{
    "Irdis/swimd",
    dependencies = { "folke/snacks.nvim" },
    config = function()
        require('swimd-lua').setup();
    end,
    keys = {
        { "<Leader>ff", function() require('swimd-lua').open_picker_git() end },
        { "<Leader>fF", function() require('swimd-lua').open_picker_files() end },
        { "<Leader>fr", function() require('swimd-lua').refresh() end }
    }
}
```
