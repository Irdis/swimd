# SWIMD

A fuzzy search Neovim plugin. It supports searching in git repositories and the file system. It uses the Smith-Waterman algorithm with [Inter-sequence Parallelization](https://pmc.ncbi.nlm.nih.gov/articles/PMC8419822/#Sec13). It's available on x64 Linux/Windows.

It's inspired by [frizbee](https://github.com/saghen/frizbee) but written completely from scratch in C for ultimate performance. It overcomes the original bucketing problem in `frizbee` by preserving index (buckets) between scans.

<img width="2271" height="1221" alt="image" src="https://github.com/user-attachments/assets/faae0177-4030-4865-a2a8-5b3a386527aa" />

## Installation

### lazy.nvim:
```lua
{
    "Irdis/swimd",
    dependencies = 'kyazdani42/nvim-web-devicons',
    config = function()
        require('swimd-lua').setup();
    end,
    keys = {
        { "<Leader>ff", function() require('swimd-lua').open_picker_git() end },
        { "<Leader>fF", function() require('swimd-lua').open_picker_files() end },
        { "<Leader>fr", function() require('swimd-lua').refresh() end }
    }
}
