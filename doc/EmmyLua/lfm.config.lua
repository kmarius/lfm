---@meta

---@class Lfm.DirSetting
---@field sorttype Lfm.SortOption
---@field dirfirst boolean
---@field reverse boolean
---@field hidden boolean

---@alias Lfm.Color string|integer

---@class Lfm.ColorPair
---@field fg Lfm.Color
---@field bg Lfm.Color

---@class Lfm.ExtColor
---@field ext string[]
---@field color Lfm.ColorPair

---@class Lfm.Colors
---@field patterns Lfm.ExtColor[]
---@field copy Lfm.ColorPair
---@field delete Lfm.ColorPair
---@field dir Lfm.ColorPair
---@field broken Lfm.ColorPair
---@field exec Lfm.ColorPair
---@field search Lfm.ColorPair
---@field normal Lfm.ColorPair
---@field current Lfm.ColorPair

---@class Lfm.Config
---@field loading_indicator_delay number delay in ms after which an indicator will be shown that the current directory is being reloaded/checked (default: 250)
---@field map_clear_delay number delay in ms after which the current key input will be cleared, must be non-negative, 0 disables (default: 10000)
---@field map_suggestion_delay number delay in ms after which key suggestions will be shown, must be non-negative (default: 1000)
---@field histsize number history size, must be non-negative (default: 100)
---@field infoline string|nil infoline string
---@field threads number number of threads in the pool (at least 2, default: nprocs+1)
---@field dir_settings table<Lfm.Path, Lfm.DirSetting>
---@field ratios integer[] assignable
---@field truncatechar string assignable, only the first character is used
---@field linkchars string assignable, must fit into 16 bytes
---@field scrolloff integer assignable
---@field hidden boolean assignable
---@field preview boolean assignable
---@field preview_images boolean assignable
---@field previewer string assignable
---@field icons boolean assignable
---@field icon_map table<string, string> assignable
---@field configpath string
---@field configdir string
---@field luadir string
---@field datadir string
---@field statedir string
---@field runtime_dir string
---@field logpath string
---@field fifopath string
---@field colors Lfm.Colors
---@field inotify_blacklist string[] No inotify watchers will be installed if the path begins with any of these strings.
---@field inotify_timeout number Minimum time in milliseconds between reloads triggered by inotify. Must larger or equal to 100.
---@field inotify_delay number Small delay in milliseconds before relloads are triggered by inotify.
lfm.config = {}
