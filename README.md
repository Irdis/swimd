# SWIMD

A fuzzy search Neovim plugin. It supports searching in git repositories and the file system. It uses the Smith-Waterman algorithm with [Inter-sequence Parallelization](https://pmc.ncbi.nlm.nih.gov/articles/PMC8419822/#Sec13). It's available on x64 Linux/Windows.

It's inspired by [frizbee](https://github.com/saghen/frizbee) but written completely from scratch in C. It overcomes the original bucketing problem in `frizbee` by preserving index (buckets) between scans.

Before scoring, a SIMD-accelerated prefilter drops any haystack string that doesn't contain enough of the needle's characters. A string passes if it contains at least about 20% of them.

<img width="2271" height="1221" alt="image" src="https://github.com/user-attachments/assets/faae0177-4030-4865-a2a8-5b3a386527aa" />

## Benchmark

Tested on the official [Chromium repository](https://github.com/chromium/chromium) at commit `d467e30b91`.

**System:** Intel Core Ultra 7 258V, 8 CPUs, x86_64.

### swimd
```text
swimd -n linux -bench 10000
    2755 iterations,
    avg: 3.51ms,
    min: 2.70ms,
    max: 5.68ms,
    total: 10003.66ms,
    ingestion: 333.52ms
```

### fzf

```text
fzf --filter linux --bench 10s
  437 iterations
  avg: 22.91ms
  min: 18.32ms
  max: 60.20ms
  total: 10.01s
  items: 517061
  matches: 58645 (11.34%)
  ingestion: 324.65ms
```

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
        { "<Leader>fl", function() require('swimd-lua').i_feel_lucky_git(vim.fn.expand('<cword>')) end },
        { "<Leader>fL", function() require('swimd-lua').i_feel_lucky_files(vim.fn.expand('<cword>')) end },
        { "<Leader>fr", function() require('swimd-lua').refresh() end }
    }
}
```
## Features

1. **Git integration** — searches Git repositories while respecting `.gitignore`.
2. **File system watcher** — tracks directory changes and automatically updates the file list as files are added, removed, or renamed.
3. **"I feel lucky"** — instantly opens the first matching file.

